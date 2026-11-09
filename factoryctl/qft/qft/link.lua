---The QFT agent and the factoryctl server: the agent decides by itself (safety stays here), the
---server keeps its settings and shows what it reports. Settings are the mode and, per recipe, its
---trigger, whether its inputs are counted, and per output the target and priority (deficit) or
---the weight (surplus). What a recipe is made of comes from qft/catalog.lua, its catalysts from
---config/catalysts.lua: that is the build, not a setting.
---
---Like the pumping agent: settings carry a version; newer ones from the server are applied and
---saved, so a restart without the server keeps them; until the server has any, the agent offers
---its own (the recipes as built: nothing set). Pure: HTTP, the file and the clock come in as deps.
local json = require('fc.json')

local link = {}
link.__index = link

local kModes = { auto = true, deficit = true, surplus = true }

---@class qft.LinkSettings
---@field version integer # The server's; 0: never had any from it.
---@field mode qft.Mode
---@field recipes { id: string, enabled: boolean?, trigger: qft.Trigger?, outputs: { circuit: integer, target: number?, priority: number?, weight: number? }[] }[]

---The settings the recipes stand for: what the agent offers a server that has none.
---@param recipes table<string, qft.Recipe>
---@param mode qft.Mode
---@return qft.LinkSettings
function link.defaults(recipes, mode)
    local ids = {}
    for id in pairs(recipes) do ids[#ids + 1] = id end
    table.sort(ids)
    local out = { version = 0, mode = mode, recipes = {} }
    for _, id in ipairs(ids) do
        local outputs = {}
        for _, o in ipairs(recipes[id].outputs) do
            outputs[#outputs + 1] = { circuit = o.circuit, target = o.target, priority = o.priority,
                weight = o.weight }
        end
        table.sort(outputs, function(a, b) return a.circuit < b.circuit end)
        out.recipes[#out.recipes + 1] = { id = id, enabled = true, trigger = recipes[id].trigger,
            outputs = outputs }
    end
    return out
end

local function positive(v) return v == nil or (type(v) == 'number' and v > 0) end

---Checks settings from the server or the saved file. Bad numbers refuse the whole table.
---@param raw any
---@return qft.LinkSettings? settings
---@return string? err
function link.check(raw)
    if type(raw) ~= 'table' then
        return nil, 'not a table'
    end
    if not kModes[raw.mode] then
        return nil, 'mode: not auto, deficit or surplus'
    end
    local out = { version = math.tointeger(raw.version) or 0, mode = raw.mode, recipes = {} }
    for i, r in ipairs(type(raw.recipes) == 'table' and raw.recipes or {}) do
        if type(r) ~= 'table' or type(r.id) ~= 'string' then
            return nil, ('recipes[%d]: no id'):format(i)
        end
        if r.enabled ~= nil and type(r.enabled) ~= 'boolean' then
            return nil, ('recipes[%d]: enabled must be true or false'):format(i)
        end
        if r.trigger ~= nil and r.trigger ~= 'deficit' and r.trigger ~= 'surplus' then
            return nil, ('recipes[%d]: trigger must be deficit or surplus'):format(i)
        end
        local outputs = {}
        for j, o in ipairs(type(r.outputs) == 'table' and r.outputs or {}) do
            local circuit = math.tointeger(o.circuit)
            if not circuit or circuit < 1 then
                return nil, ('recipes[%d].outputs[%d]: circuit'):format(i, j)
            end
            if not (positive(o.target) and positive(o.priority)) then
                return nil, ('recipes[%d].outputs[%d]: target and priority must be > 0'):format(i, j)
            end
            if o.weight ~= nil and not (type(o.weight) == 'number' and o.weight >= 0) then
                return nil, ('recipes[%d].outputs[%d]: weight must be >= 0'):format(i, j)
            end
            outputs[#outputs + 1] = { circuit = circuit, target = o.target, priority = o.priority,
                weight = o.weight }
        end
        out.recipes[#out.recipes + 1] = { id = r.id, enabled = r.enabled ~= false,
            trigger = r.trigger or 'deficit', outputs = outputs }
    end
    return out
end

---Puts the settings into the recipes in place: the trigger; a deficit output takes target and
---priority (no target: not tracked), a surplus output its weight; the inputs are always
---counted (an old `countInputs` in the settings is ignored); `enabled = false` switches the
---recipe off (recipe.off: plan treats it as disabled, a running one finishes its cycle and
---yields). A recipe the settings do not name stays as it is; a recipe or
---circuit this QFT does not have (config/catalysts.lua) is warned of and skipped.
---@param recipes table<string, qft.Recipe>
---@param s qft.LinkSettings
---@param warn fun(fmt: string, ...)
function link.apply(recipes, s, warn)
    for _, entry in ipairs(s.recipes) do
        local recipe = recipes[entry.id]
        if not recipe then
            warn('settings: no recipe %s on this QFT (config/catalysts.lua); skipped', entry.id)
        else
            recipe.off = entry.enabled == false
            recipe.trigger = entry.trigger or 'deficit'
            local byCircuit = {}
            for _, o in ipairs(recipe.outputs) do byCircuit[o.circuit] = o end
            local given = {}
            for _, o in ipairs(entry.outputs) do
                if not byCircuit[o.circuit] then
                    warn('settings: %s has no output %d; skipped', entry.id, o.circuit)
                end
                given[o.circuit] = o
            end
            for circuit, out in pairs(byCircuit) do
                local o = given[circuit] or {}
                if recipe.trigger == 'surplus' then
                    out.weight = o.weight
                else
                    out.target, out.priority = o.target, o.priority
                end
            end
        end
    end
end

---What the server shows of the recipes: names, tier, catalysts, inputs and outputs (the server
---adds a recipe it does not know to its settings).
---@param recipes table<string, qft.Recipe>
function link.catalog(recipes)
    local defaults = link.defaults(recipes, 'auto')
    local out = {}
    for i, d in ipairs(defaults.recipes) do
        local r = recipes[d.id]
        local outputs = {}
        for _, o in ipairs(r.outputs) do
            outputs[#outputs + 1] = { circuit = o.circuit, kind = o.resource.kind,
                label = o.resource.label, name = o.resource.name }
        end
        table.sort(outputs, function(a, b) return a.circuit < b.circuit end)
        local inputs = {}
        for j, input in ipairs(r.inputs or {}) do
            inputs[j] = { kind = input.resource.kind, label = input.resource.label,
                amount = input.amount }
        end
        out[i] = { id = d.id, name = r.name or d.id, tier = r.tier, catalysts = r.catalysts,
            outputs = outputs, inputs = inputs }
    end
    return out
end

---Where the server's settings are kept for restarts.
---@class qft.SettingsStore
---@field read fun(): string?
---@field write fun(text: string): boolean?, string?
---@field encode fun(v: table): string
---@field decode fun(text: string): any

---@class qft.LinkDeps
---@field post fun(path: string, body: string): string?, string?
---@field decode fun(text: string): any
---@field store qft.SettingsStore
---@field recipes table<string, qft.Recipe> # Normalized; settings are applied to them in place.
---@field mode qft.Mode # The default mode (config / --mode).
---@field name string # The setup: the server's key.
---@field coords { x: number, y: number, z: number }? # Of the controller: the server checks them.
---@field run string # This run of the agent.
---@field logs table? # fc.logbuf
---@field warn fun(fmt: string, ...)?
---@field info fun(fmt: string, ...)?

---@param deps qft.LinkDeps
---@return table link
function link.new(deps)
    local l = setmetatable({ deps = deps, warn = deps.warn or function() end,
        info = deps.info or function() end, catalogSent = false, offerWanted = false,
        synced = false }, link)
    l.offer = link.defaults(deps.recipes, deps.mode) -- before anything is applied
    l.settings = l.offer
    local text = deps.store.read()
    if text then
        local ok, raw = pcall(deps.store.decode, text)
        local s, err = link.check(ok and raw or nil)
        if s then
            l:take(s, 'saved')
        else
            l.warn('saved settings refused (%s): none until the server sends them', tostring(err))
        end
    end
    return l
end

---Applies settings and remembers them.
function link:take(s, from)
    link.apply(self.deps.recipes, s, self.warn)
    self.settings = s
    self.info('settings v%d (%s): mode %s, %d recipes', s.version, from, s.mode, #s.recipes)
end

---@class qft.Report
---@field phase string
---@field mode string
---@field want string? # The recipe chosen this tick.
---@field running table? # qft.Decision running now.
---@field cycleSeconds number?
---@field states table<string, qft.RecipeState>?
---@field plasma { neptunium: number?, fermium: number? }

local function recipeStates(states)
    local ids, out = {}, {}
    for id in pairs(states or {}) do ids[#ids + 1] = id end
    table.sort(ids)
    for _, id in ipairs(ids) do
        local st = states[id]
        local outputs = {}
        for _, o in ipairs(st.outputs) do
            outputs[#outputs + 1] = { circuit = o.circuit, amount = o.amount, target = o.target }
        end
        out[#out + 1] = { id = id, trigger = st.trigger, low = st.low, done = st.done,
            disabled = st.disabled == true, urgency = st.urgency, cycles = st.cycles,
            outputs = outputs }
    end
    return out
end

---One poll: reports, takes newer settings. Returns the reply (its `poll` is the next interval),
---or nil and why.
---@param report qft.Report
function link:poll(report)
    local d = self.deps
    local running = report.running
    local body = {
        setup = { name = d.name, coords = d.coords },
        run = d.run,
        -- -1 until a poll of this run got through: the server sends its settings whatever the
        -- saved version says. A copy saved by an older agent may lack fields it did not know.
        settings = self.synced and self.settings.version or -1,
        state = { phase = report.phase, mode = report.mode, want = report.want,
            recipe = running and running.recipe, fermium = running and running.fermium or false,
            neptunium = running and running.neptunium, cycleSeconds = report.cycleSeconds },
        recipes = recipeStates(report.states),
        plasma = report.plasma,
        log = d.logs and d.logs:entries() or {},
    }
    if self.settings.version == 0 or self.offerWanted then
        body.offer = self.offer
    end
    if not self.catalogSent then
        body.catalog = link.catalog(d.recipes)
    end
    local text, err = d.post('/agent/v1/qft/poll', json.encode(body))
    local ok, reply = false, nil
    if text then
        ok, reply = pcall(d.decode, text)
    end
    if not text or not ok or type(reply) ~= 'table' or reply.ok == false then
        return nil, err or (ok and type(reply) == 'table' and reply.error) or text
    end
    if d.logs then
        d.logs:confirm()
    end
    self.catalogSent = true
    local fresh = not self.synced -- the first reply of a run: taken even at the same version
    self.synced = true
    self.offerWanted = reply.offer == true
    if reply.settings then
        local s, serr = link.check(reply.settings)
        if not s then
            self.warn('settings from the server refused: %s', tostring(serr))
        elseif s.version ~= self.settings.version or fresh then
            self:take(s, 'server')
            local saved, werr = d.store.write(d.store.encode(s))
            if not saved then
                self.warn('settings not saved: %s', tostring(werr))
            end
            reply.changed = true
        end
    end
    return reply
end

return link
