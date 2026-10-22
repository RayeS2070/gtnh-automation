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
    return table.concat(parts, ', ')
end

---Logs when a per-run value changes, so a decision that holds is not repeated every tick.
local function logChange(ctx, key, value, fmt, ...)
    if ctx.last[key] ~= value then
        ctx.last[key] = value
        log.inf(fmt, ...)
    end
end

---Fluids of the source network, read at most once per tick and only when plasma is considered.
---@return qft.Snapshot?
local function sourceIndex(ctx)
    if ctx.source == nil then
        local ok, fluids = pcall(ctx.components.network.source.getFluidsInNetwork)
        if ok and type(fluids) == 'table' then
            ctx.source = resource.snapshot(nil, fluids)
        else
            log.err('source network poll failed: %s', tostring(fluids))
            ctx.source = false
        end
    end
    return ctx.source or nil
end

---Is there enough of this plasma for the recipe?
---@param name 'fermium' | 'neptunium'
---@param running boolean # The recipe is running with this plasma now.
---@return boolean? enough # nil: the stock cannot be read, keep the current choice.
---@return string? reason
local function plasmaEnough(ctx, name, id, running)
    local index = sourceIndex(ctx)
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

    -- Neptunium weighs the outputs by urgency; untracked or full outputs weigh 0.
    local weights = {}
    for _, out in ipairs(st.outputs) do
        weights[out.circuit] = out.urgency
    end
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

---Reads what the tracked outputs need from the products network: fluids, items, or both.
---@return qft.Snapshot? snap
---@return string? err
local function readProduction(ctx)
    local network = ctx.components.network.production
    local items, fluids
    if ctx.track.item then
        local ok, res = pcall(network.getItemsInNetwork)
        if not ok or type(res) ~= 'table' then
            return nil, 'items: ' .. tostring(res)
        end
        items = res
    end
    if ctx.track.fluid then
        local ok, res = pcall(network.getFluidsInNetwork)
        if not ok or type(res) ~= 'table' then
            return nil, 'fluids: ' .. tostring(res)
        end
        fluids = res
    end
    return resource.snapshot(items, fluids)
end

---Which kinds of resources the tracked outputs need, so nothing else is polled.
---@param recipes table<qft.RecipeId, qft.Recipe>
---@return { item: boolean, fluid: boolean }
local function trackedKinds(recipes)
    local kinds = { item = false, fluid = false }
    for _, recipe in pairs(recipes) do
        for _, out in ipairs(recipe.outputs) do
            if out.target then
                kinds[out.resource.kind] = true
            end
        end
    end
    return kinds
end

local function tick(ctx, now)
    local r = ctx.runner
    ctx.source = nil
    local current = r.phase == 'running' and r.running or nil
    -- When the poll fails the decision stays as it was.
    local want = current

    local snap, err = readProduction(ctx)
    if not snap then
        log.err('production network poll failed: %s', err)
    else
        local states, problems, missing = plan.evaluate(ctx.recipes, snap)
        for _, p in ipairs(problems) do
            logOnce(ctx, 'err', p)
        end
        for _, label in ipairs(missing) do
            logOnce(ctx, 'wrn', ('"%s" is not in the production network, counted as 0'):format(label))
        end
        for id, st in pairs(states) do
            if #st.outputs > 0 then
                log.dbg('%s: %s', id, describe(st))
            end
        end

        local id = plan.choose(states, current and current.recipe)
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
local function run(components, recipes)
    log.clock = computer.uptime
    local ctx = {
        components = components,
        recipes = recipes,
        track = trackedKinds(recipes),
        runner = runner.new(components, computer.uptime()),
        reported = {},
        last = {},
        lastWant = false, -- not nil: the first decision is always logged
    }
    log.inf('running, poll every %ds; Ctrl+C to stop', const.kPollPeriod)
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
    run(components, cfg.recipes)
    return 0
end

print('program exit with code: ', main(...))
