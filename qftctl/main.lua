-- OpenOS keeps required modules in package.loaded until reboot, so after an update or an edit
-- the old code would keep running. Drop our own modules so every start loads them from disk.
for name in pairs(package.loaded) do
    if name:match('^qft%.') or name:match('^config%.') then
        package.loaded[name] = nil
    end
end

local computer = require('computer')
local event = require('event')
local shell = require('shell')
local config = require('qft.config')
local log = require('qft.log')
local plan = require('qft.plan')
local resource = require('qft.resource')
local runner = require('qft.runner')
local setup = require('qft.setup')
local store = require('qft.store')

local const = {
    kPollPeriod = 10, -- seconds between ticks
    Event = {
        kInterrupted = 'interrupted',
    },
}

---Logs a message once per run: missing labels would otherwise repeat every tick.
local function logOnce(ctx, level, msg)
    if not ctx.reported[msg] then
        ctx.reported[msg] = true
        log[level]('%s', msg)
    end
end

---@param st qft.RecipeState
local function describe(st)
    local parts = {}
    for _, out in ipairs(st.outputs) do
        parts[#parts + 1] = ('%s %.1f%%'):format(out.resource.label, 100 * out.amount / out.target)
    end
    if st.cycles then
        parts[#parts + 1] = ('inputs for %.1f cycles'):format(st.cycles)
    end
    return table.concat(parts, ', ')
end

local kModeNames = {
    deficit = 'maintaining products',
    surplus = 'processing raw materials',
}

---Logs when a per-run value changes, so a decision that holds is not repeated every tick.
local function logChange(ctx, key, value, fmt, ...)
    if ctx.last[key] ~= value then
        ctx.last[key] = value
        log.inf(fmt, ...)
    end
end

---Is there enough of this plasma for the recipe?
---@param name 'fermium' | 'neptunium'
---@param running boolean # The recipe is running with this plasma now.
---@return boolean? enough # nil: the stock cannot be read, keep the current choice.
---@return string? reason
local function plasmaEnough(ctx, name, id, running)
    local index = ctx.source
    if not index then
        return nil
    end
    local stock, err = resource.amount(index,
        { kind = 'fluid', label = ctx.components.plasma[name].fluid })
    if not stock then
        logOnce(ctx, 'err', name .. ': ' .. err)
        return false, err
    end
    local enough, why = plan.usePlasma({
        recipe = ctx.recipes[id], shieldingTier = ctx.components.shieldingTier, stock = stock,
        running = running, cycleSeconds = running and ctx.runner.cycleSeconds or nil,
    })
    log.dbg('%s: %s %s', id, name, why)
    return enough, why
end

---Builds the full decision for the chosen recipe: which plasma, which Neptunium focus.
---@param id string
---@param st qft.RecipeState
---@param current qft.Decision? # What runs now.
---@return qft.Decision
local function decide(ctx, id, st, current)
    local recipe = ctx.recipes[id]
    local delta = ctx.components.shieldingTier - recipe.tier
    if delta < 0 then
        logOnce(ctx, 'inf', ('%s: no plasma, shielding tier %d < recipe tier %d: no effect')
            :format(id, ctx.components.shieldingTier, recipe.tier))
        return { recipe = id, fermium = false }
    end
    local same = current ~= nil and current.recipe == id

    local fermiumNow = same and current.fermium or false
    local fermium, fwhy = plasmaEnough(ctx, 'fermium', id, fermiumNow)
    if fermium == nil then
        fermium = fermiumNow
    end
    logChange(ctx, 'fermium', fermium, '%s: fermium %s%s', id, fermium and 'on' or 'off',
        fwhy and (', ' .. fwhy) or '')

    -- Neptunium weighs the outputs: by urgency (deficit) or by `weight` (surplus);
    -- outputs nobody wants weigh 0.
    local weights = st.weights
    local focusNow = same and current.neptunium or nil
    local focus, nwhy = plan.chooseFocus({
        n = #recipe.outputs, delta = delta, weights = weights,
        fermium = fermium, current = focusNow,
    })
    if focus then
        local enough, swhy = plasmaEnough(ctx, 'neptunium', id, focusNow ~= nil)
        if enough == nil then
            enough = focusNow ~= nil
        end
        if not enough then
            focus, nwhy = nil, swhy or 'neptunium stock unknown'
        end
    end
    logChange(ctx, 'neptunium', focus or false, '%s: neptunium %s, %s', id,
        focus and ('focus ' .. focus) or 'off', nwhy)

    return { recipe = id, fermium = fermium, neptunium = focus }
end

---Reads the kinds of resources the program needs from a network: fluids, items, or both.
---@param network table # me_interface proxy.
---@param kinds { item: boolean, fluid: boolean }
---@return qft.Snapshot? snap
---@return string? err
local function readNetwork(network, kinds)
    local items, fluids
    if kinds.item then
        local ok, res = pcall(network.getItemsInNetwork)
        if not ok or type(res) ~= 'table' then
            return nil, 'items: ' .. tostring(res)
        end
        items = res
    end
    if kinds.fluid then
        local ok, res = pcall(network.getFluidsInNetwork)
        if not ok or type(res) ~= 'table' then
            return nil, 'fluids: ' .. tostring(res)
        end
        fluids = res
    end
    return resource.snapshot(items, fluids)
end

---Which kinds of resources each network has to be asked for, so nothing else is polled:
---products for tracked outputs, raw materials for inputs. Plasma is a fluid of the source network.
---@param recipes table<qft.RecipeId, qft.Recipe>
---@return { production: { item: boolean, fluid: boolean }, source: { item: boolean, fluid: boolean } }
local function trackedKinds(recipes)
    local kinds = { production = { item = false, fluid = false }, source = { item = false, fluid = true } }
    for _, recipe in pairs(recipes) do
        for _, out in ipairs(recipe.outputs) do
            if out.target then
                kinds.production[out.resource.kind] = true
            end
        end
        for _, input in ipairs(recipe.inputs or {}) do
            kinds.source[input.resource.kind] = true
        end
    end
    return kinds
end

local function tick(ctx, now)
    local r = ctx.runner
    local current = r.phase == 'running' and r.running or nil
    -- When a poll fails the decision stays as it was.
    local want = current

    local snap, err = readNetwork(ctx.components.network.production, ctx.track.production)
    local source, serr = readNetwork(ctx.components.network.source, ctx.track.source)
    ctx.source = source
    if not snap then
        log.err('production network poll failed: %s', err)
    elseif not source then
        log.err('source network poll failed: %s', serr)
    else
        local states, problems, missing = plan.evaluate(ctx.recipes, snap, source)
        for _, p in ipairs(problems) do
            logOnce(ctx, 'err', p)
        end
        for _, label in ipairs(missing) do
            logOnce(ctx, 'wrn', ('%s is not in its network, counted as 0'):format(label))
        end
        for id, st in pairs(states) do
            if st.disabled then
                logOnce(ctx, 'inf', ('%s: no catalysts, skipped'):format(id))
            elseif #st.outputs > 0 or st.cycles then
                log.dbg('%s: %s', id, describe(st))
            end
        end

        local id = plan.choose(states, current and current.recipe, { mode = ctx.mode })
        local mode = id and states[id].trigger or 'idle'
        logChange(ctx, 'mode', mode, 'mode: %s', kModeNames[mode] or 'nothing to do')
        if id ~= ctx.lastWant then
            if id then
                log.inf('want %s: %s', id, describe(states[id]))
            else
                log.inf('nothing to produce, QFT stops')
            end
            ctx.lastWant = id
        end
        want = id and decide(ctx, id, states[id], current) or nil
    end

    runner.step(r, want, now)
end

---@param components table # Resolved components.
---@param recipes table<qft.RecipeId, qft.Recipe>
---@param mode qft.Mode
local function run(components, recipes, mode)
    log.clock = computer.uptime
    local ctx = {
        components = components,
        recipes = recipes,
        mode = mode,
        track = trackedKinds(recipes),
        runner = runner.new(components, computer.uptime()),
        reported = {},
        last = {},
        lastWant = false, -- not nil: the first decision is always logged
    }
    log.inf('running in mode %s, poll every %ds; Ctrl+C to stop', mode, const.kPollPeriod)
    repeat
        -- A bug in a tick must not kill the controller; the traceback says where it was.
        local ok, err = xpcall(tick, debug.traceback, ctx, computer.uptime())
        if not ok then
            log.err('tick failed: %s', tostring(err))
        end
    until event.pull(const.kPollPeriod, const.Event.kInterrupted)
    runner.shutdown(ctx.runner)
    log.inf('stopped: QFT finishes its current cycle and stays off')
end

local function main(...)
    local args, opts = shell.parse(...)
    local mode = opts.mode or 'auto'
    if not plan.modes[mode] then
        print('--mode must be auto, deficit or surplus')
        return 1
    end
    local cfg, problems = config.load()
    if not cfg then
        print('config is inconsistent:\n  ' .. table.concat(problems, '\n  '))
        return 1
    end

    if args[1] == 'setup' then
        local ok, unbound = setup.run(cfg.components, { all = opts.all })
        if not ok then
            print('unbound: ' .. table.concat(unbound, ', '))
            return 1
        end
        return 0
    end

    local saved, err = store.load(store.defaultPath)
    if not saved then
        print(err)
        return 1
    end
    local components, missing = store.resolve(cfg.components, saved)
    if not components then
        print('not configured: ' .. table.concat(missing, ', '))
        print('run `main setup` first')
        return 1
    end
    if opts.debug then
        log.setLevel(log.Level.Dbg)
    end
    run(components, cfg.recipes, mode)
    return 0
end

print('program exit with code: ', main(...))
