---Pure decision logic: what the network holds and which recipe should run.
---No component calls here, so everything can be tested locally with plain tables.
local resource = require('qft.resource')

local plan = {}

plan.defaults = {
    -- A recipe may start only when some tracked output fell below startRatio * target;
    -- once running it keeps going until every tracked output reaches target (hysteresis).
    startRatio = 0.8,
    -- Plasma is enabled only with stock for this many seconds of use...
    plasmaStartSeconds = 600,
    -- ...and kept while the stock covers the current cycle plus this margin (seconds).
    -- Running out mid-cycle shuts the QFT down until a manual restart.
    plasmaKeepSeconds = 60,
}

---Plasma use of the QFT while focusing, L/s (GT litres = mB). Wiki: 4 * shielding * sqrt(parallels).
---@param shieldingTier integer
---@param parallels integer
---@return number
function plan.plasmaRate(shieldingTier, parallels)
    return 4 * shieldingTier * math.sqrt(parallels)
end

---@class qft.PlasmaArgs
---@field recipe qft.Recipe
---@field shieldingTier integer
---@field stock number # Plasma in the source network, mB.
---@field running boolean # This recipe is running with this plasma right now.
---@field cycleSeconds number? # Length of the current cycle, when known.
---@field opts { plasmaStartSeconds: number, plasmaKeepSeconds: number }?

---Should the recipe run with this plasma? The reason is for the log.
---@param a qft.PlasmaArgs
---@return boolean use
---@return string reason
function plan.usePlasma(a)
    local opts = a.opts or plan.defaults
    if a.shieldingTier < a.recipe.tier then
        return false, ('shielding tier %d < recipe tier %d: no effect'):format(
            a.shieldingTier, a.recipe.tier)
    end
    local rate = plan.plasmaRate(a.shieldingTier, a.recipe.catalysts)
    local seconds
    if a.running then
        seconds = (a.cycleSeconds or 0) + opts.plasmaKeepSeconds
    else
        seconds = opts.plasmaStartSeconds
    end
    local need = rate * seconds
    local verdict = a.stock >= need and 'enough' or 'not enough'
    return a.stock >= need, ('%s: %.0f mB, need %.0f (%.1f L/s for %.0fs)'):format(
        verdict, a.stock, need, rate, seconds)
end

-- Surplus recipes run on raw materials: they may start with stock for startCycles cycles and
-- stop below stopCycles. A running one hands the machine to another surplus recipe only when
-- that one has surplusSwitchRatio times more cycles in stock: every switch costs a stop.
plan.defaults.startCycles = 2
plan.defaults.stopCycles = 1
plan.defaults.surplusSwitchRatio = 2

---@class qft.OutputState
---@field resource qft.Resource
---@field circuit integer
---@field amount number
---@field target number
---@field urgency number # 1 - ratio^priority: 0 when full, 1 when empty.

---@class qft.RecipeState
---@field trigger qft.Trigger
---@field outputs qft.OutputState[] # Tracked outputs only (deficit recipes, the ones with a target).
---@field urgency number # Deficit: max urgency over the tracked outputs; 0 when nothing is tracked.
---@field cycles number? # Cycles the raw materials in stock cover; nil when inputs are not tracked.
---@field low boolean # The recipe may start.
---@field done boolean # A running recipe stops.
---@field weights table<integer, number> # Per circuit: how much each output is wanted (Neptunium).
---@field disabled boolean? # No catalysts: the recipe never runs.

---How many cycles the stock of every input covers: the scarcest input decides.
---@param recipe qft.Recipe
---@param source qft.Snapshot?
---@return number? cycles # nil on error, and when no input has an amount (nothing is counted).
---@return string? err
---@return string[] missing # Inputs absent from the network (counted as 0).
local function inputCycles(recipe, source)
    local missing = {}
    if not source then
        return nil, 'source network was not read', missing
    end
    local cycles = math.huge
    for _, input in ipairs(recipe.inputs) do
        -- Not counted without an amount: in the bus or the hatch, but supplied without limit.
        if input.amount then
            local amount, err, absent = resource.amount(source, input.resource)
            if not amount then
                return nil, err, missing
            end
            if absent then
                missing[#missing + 1] = resource.describe(input.resource)
            end
            cycles = math.min(cycles, amount / (input.amount * recipe.catalysts))
        end
    end
    if cycles == math.huge then
        return nil, nil, missing -- nothing counted
    end
    return cycles, nil, missing
end

---@param recipe qft.Recipe
---@param production qft.Snapshot
---@param st qft.RecipeState
---@param startRatio number
---@param missing string[] # Appended to.
---@return string? err
local function evaluateOutputs(recipe, production, st, startRatio, missing)
    for _, out in ipairs(recipe.outputs) do
        if out.target then
            local amount, err, absent = resource.amount(production, out.resource)
            if not amount then
                return err
            end
            if absent then
                missing[#missing + 1] = resource.describe(out.resource)
            end
            local ratio = math.min(amount / out.target, 1)
            local urgency = 1 - ratio ^ (out.priority or 1)
            st.outputs[#st.outputs + 1] = {
                resource = out.resource, circuit = out.circuit,
                amount = amount, target = out.target, urgency = urgency,
            }
            st.weights[out.circuit] = urgency
            if amount < out.target then
                st.done = false
            end
            if amount < startRatio * out.target then
                st.low = true
            end
            st.urgency = math.max(st.urgency, urgency)
        end
    end
    return nil
end

---Two kinds of recipes (`trigger`):
---  deficit: maintains stock of products. Starts when a tracked output fell below
---           startRatio * target, runs until every tracked output reached its target.
---           If it has inputs, it needs raw materials for one cycle to start and to keep going.
---  surplus: processes raw materials. Starts with stock for startCycles cycles, runs until
---           less than stopCycles is left. Output `weight` says what Neptunium should favour.
---@param recipes table<string, qft.Recipe> # Outputs and inputs carry `resource` (config.normalize).
---@param production qft.Snapshot # The products network.
---@param source qft.Snapshot? # The raw materials network; needed by recipes with inputs.
---@param opts table?
---@return table<string, qft.RecipeState> states
---@return string[] problems # Ambiguous labels and the like; such a recipe never runs.
---@return string[] missing # Tracked resources absent from a network (counted as 0).
function plan.evaluate(recipes, production, source, opts)
    opts = opts or plan.defaults
    local startRatio = opts.startRatio or plan.defaults.startRatio
    local startCycles = opts.startCycles or plan.defaults.startCycles
    local stopCycles = opts.stopCycles or plan.defaults.stopCycles
    local states, problems, missing = {}, {}, {}

    for id, recipe in pairs(recipes) do
        local trigger = recipe.trigger or 'deficit'
        local st = { trigger = trigger, outputs = {}, urgency = 0, low = false, done = true,
            weights = {} }
        local err
        if recipe.catalysts == 0 or recipe.off then
            -- No catalyst housing for it yet (the QFT would refuse to start it), or switched off
            -- in the settings: never chosen, a running one finishes its cycle and yields.
            st.disabled, st.low, st.done = true, false, true
        elseif recipe.inputs then
            local inputMissing
            st.cycles, err, inputMissing = inputCycles(recipe, source)
            for _, m in ipairs(inputMissing) do
                missing[#missing + 1] = m
            end
        end
        if st.disabled then
            -- Nothing else to evaluate.
        elseif not err and trigger == 'surplus' then
            for _, out in ipairs(recipe.outputs) do
                st.weights[out.circuit] = out.weight or 0
            end
            -- A surplus recipe always counts its inputs (link.apply).
            st.low = (st.cycles or 0) >= startCycles
            st.done = (st.cycles or 0) < stopCycles
        elseif not err then
            err = evaluateOutputs(recipe, production, st, startRatio, missing)
            if st.cycles and st.cycles < stopCycles then
                -- Nothing to make it from: a start would only stall the machine.
                st.low, st.done = false, true
            end
        end
        if err then
            problems[#problems + 1] = ('%s: %s'):format(id, err)
            -- Wrong numbers are worse than none: never start it, stop it if it runs.
            st.low, st.done = false, true
        end
        states[id] = st
    end
    table.sort(problems)
    table.sort(missing)
    return states, problems, missing
end

---The low recipe of a trigger with the highest score; ties go to the smaller id,
---so pairs() order never decides.
---@param states table<string, qft.RecipeState>
---@param trigger qft.Trigger
---@return string?
local function best(states, trigger)
    local bestId, bestScore
    for id, st in pairs(states) do
        if st.low and st.trigger == trigger then
            local score = trigger == 'surplus' and st.cycles or st.urgency
            if bestId == nil or score > bestScore or (score == bestScore and id < bestId) then
                bestId, bestScore = id, score
            end
        end
    end
    return bestId
end

---Which triggers a mode lets run: 'auto' maintains products first and processes raw materials
---when nothing is short; 'deficit' and 'surplus' run only recipes of that trigger.
---@alias qft.Mode 'auto' | 'deficit' | 'surplus'
plan.modes = {
    auto = { deficit = true, surplus = true },
    deficit = { deficit = true },
    surplus = { surplus = true },
}

---Which recipe should run now; nil means stop the QFT.
---Maintaining products comes first: the most urgent low deficit recipe wins, and a running
---deficit recipe keeps going until done. Only when no product is short, surplus recipes
---process raw materials; a running one yields to any deficit, or to a surplus recipe with
---far more stock (surplusSwitchRatio). `opts.mode` limits which triggers may run.
---@param states table<string, qft.RecipeState>
---@param current string? # Recipe running now.
---@param opts { mode: qft.Mode?, surplusSwitchRatio: number? }?
---@return string?
function plan.choose(states, current, opts)
    local switchRatio = (opts and opts.surplusSwitchRatio) or plan.defaults.surplusSwitchRatio
    local allowed = plan.modes[(opts and opts.mode) or 'auto']
    local cur = current and states[current]
    if cur and not allowed[cur.trigger] then
        cur = nil
    end
    if cur and not cur.done and cur.trigger ~= 'surplus' then
        return current
    end
    local deficit = allowed.deficit and best(states, 'deficit') or nil
    if deficit then
        return deficit
    end
    if not allowed.surplus then
        return nil
    end
    local surplus = best(states, 'surplus')
    if cur and not cur.done then
        if surplus and surplus ~= current and states[surplus].cycles >= cur.cycles * switchRatio then
            return surplus
        end
        return current
    end
    return surplus
end

plan.defaults.focusOnGain = 0.10 -- enable Neptunium when the best focus beats no focus by 10%
plan.defaults.focusOffGain = 0.05 -- keep it while the current focus still beats no focus by 5%
plan.defaults.focusSwitchGain = 0.10 -- move the focus when another output beats it by 10%

---Output chances, out of 10000, in NEI order. A 1:1 port of getOutputChances from
---MTEQuantumForceTransformer.java (integer maths as in Java; all values are non-negative,
---so Java's truncating division is Lua's //).
---@param n integer # Number of outputs.
---@param delta integer # Shielding tier - recipe tier; below 0 focusing has no effect.
---@param focus integer? # Circuit (1-based) when Neptunium is on.
---@param fermium boolean
---@return integer[]
function plan.chances(n, delta, focus, fermium)
    local base = 10000 // n
    local out = {}
    for i = 1, n do
        local c = base
        if delta == 0 then
            if focus then
                c = (i == focus) and (c + base // 2 * (n - 1)) or (c // 2)
            end
            if fermium then c = c + (10000 - c) // 4 end
        elseif delta == 1 then
            if focus then
                c = (i == focus) and (c + base * 3 // 4 * (n - 1)) or (c // 4)
            end
            if fermium then c = c + (10000 - c) // 3 end
        elseif delta == 2 or delta == 3 then
            if focus then
                c = (i == focus) and 10000 or 0
            end
            if fermium then c = c + (10000 - c) // 2 end
        end
        out[i] = c
    end
    return out
end

---@param weights number[] # Per output in NEI order; 0 for outputs nobody needs.
---@param chances integer[]
---@return number
local function value(weights, chances)
    local s = 0
    for i, c in ipairs(chances) do
        s = s + (weights[i] or 0) * c
    end
    return s
end

---@class qft.FocusArgs
---@field n integer # Number of outputs of the recipe.
---@field delta integer # Shielding tier - recipe tier.
---@field weights number[] # Per output in NEI order (circuit = index).
---@field fermium boolean # Fermium is used in the same run: it changes the chances.
---@field current integer? # Focus of the run in progress, nil when Neptunium is off.
---@field opts table?

---Which output Neptunium should focus, or nil for no Neptunium. The reason is for the log.
---Neptunium moves probability between outputs, it adds none: it pays only when the outputs
---are needed unequally. Hysteresis: on at focusOnGain, off below focusOffGain, a focus moves
---only when another output beats it by focusSwitchGain.
---@param a qft.FocusArgs
---@return integer? focus
---@return string reason
function plan.chooseFocus(a)
    local opts = a.opts or plan.defaults
    if a.delta < 0 then
        return nil, 'shielding below recipe tier: no effect'
    end
    local none = value(a.weights, plan.chances(a.n, a.delta, nil, a.fermium))
    if none <= 0 then
        return nil, 'nothing needed'
    end

    local best, bestValue
    for i = 1, a.n do
        if (a.weights[i] or 0) > 0 then
            local v = value(a.weights, plan.chances(a.n, a.delta, i, a.fermium))
            if best == nil or v > bestValue then
                best, bestValue = i, v
            end
        end
    end
    if best == nil then
        return nil, 'nothing needed'
    end

    local function gain(v) return v / none - 1 end
    if a.current then
        local curValue = value(a.weights, plan.chances(a.n, a.delta, a.current, a.fermium))
        -- Moving the focus is free (no stop), switching Neptunium off costs a stop:
        -- try the move first.
        if best ~= a.current and bestValue >= curValue * (1 + opts.focusSwitchGain)
                and gain(bestValue) >= opts.focusOffGain then
            return best, ('focus %d gains %+.0f%%, beats %d'):format(
                best, 100 * gain(bestValue), a.current)
        end
        if gain(curValue) < opts.focusOffGain then
            return nil, ('focus %d gains %+.0f%% < %.0f%%'):format(
                a.current, 100 * gain(curValue), 100 * opts.focusOffGain)
        end
        return a.current, ('focus %d gains %+.0f%%'):format(a.current, 100 * gain(curValue))
    end
    if gain(bestValue) < opts.focusOnGain then
        return nil, ('best focus %d gains %+.0f%% < %.0f%%'):format(
            best, 100 * gain(bestValue), 100 * opts.focusOnGain)
    end
    return best, ('focus %d gains %+.0f%%'):format(best, 100 * gain(bestValue))
end

return plan
