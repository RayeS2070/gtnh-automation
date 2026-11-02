---The mining agent: finds the modules of this computer, asks the server what each one mines,
---carries it out (mm.module), routes the ores (storage buses) and reports. The server plans; the
---agent keeps its own safety: when the server is away, it holds the modules once an ore it keeps
---fills the buffer before processing.
---Pure: components, HTTP and the clock come in as deps (tests/run.lua fakes them).
local json = require('fc.json')
local module = require('mm.module')

local miner = {}
miner.__index = miner

local kNames = { projectmoduleminert1 = 1, projectmoduleminert2 = 2, projectmoduleminert3 = 3 }
local kDiscover = 60   -- s between looks for modules (adapters come and go)
local kPoll = 10       -- s between polls until the server says
local kRetry = 30      -- s between polls while the server does not answer
local kOffline = 60    -- s without the server before the buffer holds the modules
local kSlotsMax = 63

---@class mm.Route
---@field bus string # address prefix of the storage bus component
---@field side integer # side of its cable the bus is on

---@class mm.Config
---@field name string
---@field drones table<string, { transposer: string, bus: integer, pool: integer, interface: string }>
---@field routes { keep: mm.Route?, void: mm.Route? }?
---@field buffer string? # address prefix of the buffer network's me_controller / interface
---@field pool string? # address prefix of an interface of the drone pool (counting)

---@class mm.Deps
---@field list fun(kind: string): fun(): string? # component.list(kind, true)
---@field proxy fun(address: string): table
---@field post fun(path: string, body: string): string?, string?
---@field decode fun(text: string): any
---@field config mm.Config
---@field run string
---@field logs table? # fc.logbuf
---@field warn fun(fmt: string, ...)?
---@field info fun(fmt: string, ...)?

---@param deps mm.Deps
function miner.new(deps, now)
    local m = setmetatable({
        deps = deps,
        warn = deps.warn or function() end,
        info = deps.info or function() end,
        modules = {},     -- id -> mm.module
        discoverAt = now,
        pollAt = now,
        poll = kPoll,
        lastAnswer = now, -- a fresh start counts as answered: no hold before the first poll
        keep = {},        -- ores of the storage bus whitelist, as the server gave them
        keepKey = nil,    -- what is written in the buses
        bufferLimit = nil,
        routes = {},
        held = false,
        warned = {},     -- config mistakes told once
        pathText = {},   -- the drone path last told, per module
    }, miner)
    return m
end

---The first component of the kind whose address starts with the prefix.
function miner:find(kind, prefix)
    for address in self.deps.list(kind) do
        if not prefix or address:sub(1, #prefix) == prefix then
            return self.deps.proxy(address)
        end
    end
    return nil
end

local function call(proxy, method, ...)
    if not proxy or not proxy[method] then return false, 'no ' .. method end
    return pcall(proxy[method], ...)
end

local function coords(proxy)
    local ok, x, y, z = call(proxy, 'getCoordinates')
    if ok and type(x) == 'table' then x, y, z = x[1] or x.x, x[2] or x.y, x[3] or x.z end
    if ok and math.tointeger(x) and math.tointeger(y) and math.tointeger(z) then
        return math.tointeger(x), math.tointeger(y), math.tointeger(z)
    end
    return nil
end

---Whether a block at dx, dy, dz from a controller is in its module. A module is a wall two
---blocks wide and five high around its controller (the third row), one deep:
---    R B      R optical connector, B stocking bus, H stocking hatch,
---      H      x the controller, D the drone bus, O the output bus
---    x
---    D        Which way it faces OC cannot tell: the width is along x or along z, either side.
---    O
local function inModule(dx, dy, dz)
    return math.abs(dy) <= 2
        and ((dx == 0 and math.abs(dz) <= 1) or (dz == 0 and math.abs(dx) <= 1))
end

---The stocking part of the kind inside the module of the controller at x, y, z, not taken by
---another module; the nearest one if there were several.
function miner:nearest(kind, x, y, z, taken)
    local best, bestD
    for address in self.deps.list(kind) do
        if not taken[address] then
            local proxy = self.deps.proxy(address)
            local px, py, pz = coords(proxy)
            if px and inModule(px - x, py - y, pz - z) then
                local d = math.abs(px - x) + math.abs(py - y) + math.abs(pz - z)
                if not bestD or d < bestD then best, bestD = proxy, d end
            end
        end
    end
    return best
end

---The sides of a transposer from what they hold: a GT machine is the drone bus, an interface the
---pool. nil for a side it does not have, or has twice (a controller and a bus: say it in config).
function miner:sidesOf(transposer)
    local bus, pool, buses = nil, nil, 0
    for side = 0, 5 do
        local ok, name = call(transposer, 'getInventoryName', side)
        if ok and type(name) == 'string' then
            if name == 'gt.blockmachines' then
                bus, buses = side, buses + 1
            elseif name:lower():find('interface', 1, true) then
                pool = pool or side
            end
        end
    end
    return buses == 1 and bus or nil, pool
end

---The interfaces of the drone pool this computer has.
function miner:interfaces()
    local out = {}
    for _, kind in ipairs({ 'fluid_interface', 'me_interface' }) do
        for address in self.deps.list(kind) do out[#out + 1] = self.deps.proxy(address) end
    end
    return out
end

---The drone path of each module: from config.lua (drones, by the controller's coordinates; spaces
---allowed), else found: with one module left and one transposer that has a drone bus and an
---interface, they go together. The sides, when config.lua leaves them out, from what they hold;
---the interface, when it does, the only one there is.
function miner:paths(ids)
    local given, out = {}, {}
    for key, path in pairs(self.deps.config.drones or {}) do
        given[(tostring(key):gsub('%s', ''))] = path
    end
    local known = {}
    for _, id in ipairs(ids) do known[id] = true end
    for key in pairs(given) do
        if not known[key] and not self.warned[key] then
            self.warned[key] = true
            self.warn('config.lua drones[%q]: no mining module there; the modules are at %s'
                .. ' (the controllers\' coordinates)', key, table.concat(ids, ' '))
        end
    end
    local used, missing = {}, {}
    for _, id in ipairs(ids) do
        local g = given[id]
        if g then
            local t = self:find('transposer', g.transposer)
            if t then used[t.address] = true end
            out[id] = { transposer = t, bus = g.bus, pool = g.pool, interface = g.interface,
                from = 'config.lua' }
        else
            missing[#missing + 1] = id
        end
    end
    if #missing == 1 then
        local candidates = {}
        for address in self.deps.list('transposer') do
            if not used[address] then
                local t = self.deps.proxy(address)
                local bus, pool = self:sidesOf(t)
                if bus and pool then candidates[#candidates + 1] = t end
            end
        end
        if #candidates == 1 then
            out[missing[1]] = { transposer = candidates[1], from = 'found' }
        end
    end
    local interfaces = self:interfaces()
    for _, path in pairs(out) do
        if path.transposer and not (path.bus and path.pool) then
            local bus, pool = self:sidesOf(path.transposer)
            path.bus, path.pool = path.bus or bus, path.pool or pool
        end
        if path.interface then
            path.interface = self:find('fluid_interface', path.interface)
                or self:find('me_interface', path.interface)
        elseif #interfaces == 1 then
            path.interface = interfaces[1]
        end
    end
    return out
end

local function why(path)
    if not path then return 'no drone path: give it one in config.lua (drones)' end
    if not path.transposer then return 'no drone path: no such transposer' end
    if not (path.bus and path.pool) then return 'no drone path: say the transposer\'s sides' end
    if not path.interface then return 'no drone path: which interface? (drones[...].interface)' end
    return nil
end

function miner:discover(now)
    local found, taken = {}, {}
    for address in self.deps.list('gt_machine') do
        local proxy = self.deps.proxy(address)
        local okName, name = call(proxy, 'getName')
        local tier = okName and type(name) == 'string' and kNames[name:lower()]
        if tier then
            local x, y, z = coords(proxy)
            if x then
                local id = ('%d,%d,%d'):format(x, y, z)
                found[id] = { proxy = proxy, tier = tier, x = x, y = y, z = z }
            else
                self.warn('mining module %s: no coordinates', address:sub(1, 8))
            end
        end
    end
    local ids = {}
    for id in pairs(found) do ids[#ids + 1] = id end
    table.sort(ids)
    local paths = self:paths(ids)
    local pool = self.deps.config.pool
    if type(pool) == 'string' and pool:find('[^%x%-]') and not self.warned.pool then
        self.warned.pool = true
        self.warn('config.lua pool = %q: an address prefix of an interface, not coordinates', pool)
    end
    for _, id in ipairs(ids) do
        local f = found[id]
        local path = paths[id]
        local parts = {
            controller = f.proxy,
            bus = self:nearest('me_inputbus', f.x, f.y, f.z, taken),
            hatch = self:nearest('me_inputhatch', f.x, f.y, f.z, taken),
        }
        if parts.bus then taken[parts.bus.address] = true end
        if parts.hatch then taken[parts.hatch.address] = true end
        local missing = why(path)
        if not missing then
            parts.transposer, parts.interface = path.transposer, path.interface
            parts.busSide, parts.poolSide = path.bus, path.pool
        end
        local m = self.modules[id]
        if m then
            m.parts = parts -- an adapter placed anew has a new address
        else
            self.modules[id] = module.new(id, f.tier, parts, now, self.warn)
        end
        local text = missing or ('drone path %s: transposer %s, bus side %d, pool side %d'):format(
            path.from, path.transposer.address:sub(1, 8), path.bus, path.pool)
        if self.pathText[id] ~= text then
            self.pathText[id] = text
            self.info('mining module T%d at %s: %s', f.tier, id, text)
        end
    end
    for id in pairs(self.modules) do
        if not found[id] then
            self.warn('mining module at %s is gone', id)
            self.modules[id] = nil
            self.pathText[id] = nil
        end
    end
    self.discoverAt = now + kDiscover
end

---Drones in the pool, by tier.
function miner:pool()
    local cfg = self.deps.config
    local proxy = self:find('fluid_interface', cfg.pool) or self:find('me_interface', cfg.pool)
        or (not cfg.pool and self:interfaces()[1]) or nil
    if not proxy then return {} end
    local ok, stacks = call(proxy, 'getItemsInNetwork', { name = module.kDrone })
    local out = {}
    if ok and type(stacks) == 'table' then
        for _, s in ipairs(stacks) do
            if type(s) == 'table' and s.name == module.kDrone and math.tointeger(s.damage) then
                out[#out + 1] = { tier = math.tointeger(s.damage), count = math.floor(s.size or 0) }
            end
        end
    end
    return out
end

---The kept ores in the buffer before processing.
function miner:buffer()
    local cfg = self.deps.config
    if not cfg.buffer then return {} end
    local proxy = self:find('me_controller', cfg.buffer) or self:find('fluid_interface', cfg.buffer)
        or self:find('me_interface', cfg.buffer)
    if not proxy then return {} end
    local out = {}
    for _, ore in ipairs(self.keep) do
        local ok, stack = call(proxy, 'getItemInNetwork', ore.name, ore.damage)
        out[#out + 1] = { name = ore.name, damage = ore.damage,
            size = ok and type(stack) == 'table' and math.floor(stack.size or 0) or 0 }
    end
    return out
end

---Resolves the storage buses and the slots both have.
function miner:resolveRoutes()
    local slots
    for _, role in ipairs({ 'keep', 'void' }) do
        local r = self.deps.config.routes and self.deps.config.routes[role]
        if r then
            local proxy = self:find('me_storagebus', r.bus)
            local entry = self.routes[role] or {}
            entry.proxy, entry.side = proxy, r.side
            self.routes[role] = entry
            local ok, n = call(proxy, 'getStorageSlotSize', r.side)
            if ok and math.tointeger(n) then
                entry.size = math.tointeger(n)
                slots = math.min(slots or kSlotsMax, entry.size)
            elseif proxy then
                self.warn('storage bus %s (%s): %s', role, r.bus, tostring(n))
            else
                self.warn('storage bus %s: no me_storagebus %s', role, r.bus)
            end
        end
    end
    return slots
end

-- Written when no ore is kept: an empty storage bus takes everything (the keeping one would send
-- every ore to processing), one with only this takes nothing - and the inverted one everything.
local kNothing = { name = 'minecraft:bedrock', damage = 0 }

---Writes the whitelist into the buses: the keeping one routes these ores to processing, the void
---one (an Inverter Card) everything else to the trash.
function miner:route()
    local list = #self.keep > 0 and self.keep or { kNothing }
    local parts = {}
    for _, o in ipairs(list) do parts[#parts + 1] = module.key(o) end
    local k = table.concat(parts, ' ')
    if k == self.keepKey then return end
    local written = 0
    for role, r in pairs(self.routes) do
        if r.proxy and r.size then
            local last = math.max(r.written or r.size, #list)
            for slot = 1, math.min(last, r.size) do
                local ore = list[slot]
                local ok, err
                if ore then
                    ok, err = call(r.proxy, 'setStorageConfiguration', r.side, slot,
                        { name = ore.name, damage = ore.damage })
                else
                    ok, err = call(r.proxy, 'setStorageConfiguration', r.side, slot)
                end
                if not ok then
                    self.warn('storage bus %s slot %d: %s', role, slot, tostring(err))
                    return
                end
            end
            r.written = math.min(#list, r.size)
            written = written + 1
        end
    end
    self.keepKey = k
    if written > 0 then
        self.info('routing: %d ore%s kept', #self.keep, #self.keep == 1 and '' or 's')
    end
end

function miner:report()
    local modules = {}
    local ids = {}
    for id in pairs(self.modules) do ids[#ids + 1] = id end
    table.sort(ids)
    for _, id in ipairs(ids) do
        local m = self.modules[id]
        local x, y, z = id:match('^(-?%d+),(-?%d+),(-?%d+)$')
        local r = m:report()
        modules[#modules + 1] = { x = tonumber(x), y = tonumber(y), z = tonumber(z), tier = m.tier,
            state = r.state, active = r.active, drone = r.drone, assignment = r.assignment,
            error = r.error }
    end
    local body = { setup = self.deps.config.name, run = self.deps.run, modules = modules,
        pool = self:pool(), buffer = self:buffer(),
        log = self.deps.logs and self.deps.logs:entries() or {} }
    local slots = self:resolveRoutes()
    if slots then body.routing = { slots = slots } end
    return body
end

function miner:ask(now)
    local text, err = self.deps.post('/agent/v1/mining/poll', json.encode(self:report()))
    local ok, reply = false, nil
    if text then ok, reply = pcall(self.deps.decode, text) end
    if not text or not ok or type(reply) ~= 'table' or reply.ok == false then
        local why = err or (ok and type(reply) == 'table' and reply.error) or text
        if not self.away then self.warn('server: %s', tostring(why)) end
        self.away = true
        self.pollAt = now + kRetry
        return
    end
    if self.away then self.info('server is back') end
    self.away = false
    self.lastAnswer = now
    if self.deps.logs then self.deps.logs:confirm() end
    self.poll = math.tointeger(reply.poll) or kPoll
    self.pollAt = now + self.poll
    self.keep = type(reply.keep) == 'table' and reply.keep or {}
    self.bufferLimit = type(reply.buffer) == 'table' and tonumber(reply.buffer.limit) or nil
    local wanted = {}
    for _, a in ipairs(type(reply.modules) == 'table' and reply.modules or {}) do
        local id = ('%d,%d,%d'):format(a.x or 0, a.y or 0, a.z or 0)
        wanted[id] = not a.idle and a or false
    end
    for id, m in pairs(self.modules) do
        -- A module the server did not plan (new since the report) waits for the next poll.
        if wanted[id] ~= nil then m:assign(wanted[id] or nil, now) end
    end
    self:route()
end

---Without the server, a kept ore at its limit in the buffer holds every module: the factory is
---behind and the server cannot move them elsewhere.
function miner:guard(now)
    if now - self.lastAnswer < kOffline or not self.bufferLimit then
        if self.held then
            self.held = false
            for _, m in pairs(self.modules) do m:resume(now) end
        end
        return
    end
    local full = false
    for _, b in ipairs(self:buffer()) do full = full or b.size >= self.bufferLimit end
    if full and not self.held then
        self.warn('the buffer is full and the server away: modules held')
        for _, m in pairs(self.modules) do m:hold(now) end
    elseif not full and self.held then
        for _, m in pairs(self.modules) do m:resume(now) end
    end
    self.held = full
end

---Does what is due; returns when it wants to be called again (an uptime).
function miner:step(now)
    if now >= self.discoverAt then self:discover(now) end
    if now >= self.pollAt then
        self:ask(now)
        self:guard(now)
    end
    local nextAt = math.min(self.discoverAt, self.pollAt)
    for _, m in pairs(self.modules) do
        nextAt = math.min(nextAt, m:step(now))
    end
    return math.max(nextAt - now, 0.05)
end

---Stops every module after its operation: the agent ends.
function miner:stop(now)
    for _, m in pairs(self.modules) do m:stop(now) end
end

return miner
