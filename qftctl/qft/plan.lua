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

---@class qft.OutputState
---@field resource qft.Resource
---@field circuit integer
---@field amount number
---@field target number
---@field urgency number # 1 - ratio^priority: 0 when full, 1 when empty.

---@class qft.RecipeState
---@field outputs qft.OutputState[] # Tracked outputs only (the ones with a target).
---@field urgency number # Max urgency over the tracked outputs; 0 when nothing is tracked.
---@field low boolean # Some tracked output is below startRatio * target: the recipe may start.
---@field done boolean # Every tracked output reached its target: a running recipe stops.
---@field focus integer? # Circuit of the most urgent output, the Neptunium target later on.

---@param recipes table<string, qft.Recipe> # Outputs carry `resource` (see config.normalize).
---@param snap qft.Snapshot # The products network.
---@param opts { startRatio: number }?
---@return table<string, qft.RecipeState> states
---@return string[] problems # Ambiguous labels; such a recipe is treated as done.
---@return string[] missing # Tracked resources absent from the network (counted as 0).
function plan.evaluate(recipes, snap, opts)
    local startRatio = (opts and opts.startRatio) or plan.defaults.startRatio
    local states, problems, missing = {}, {}, {}

    for id, recipe in pairs(recipes) do
        local st = { outputs = {}, urgency = 0, low = false, done = true }
        local broken = false
        for _, out in ipairs(recipe.outputs) do
            if out.target then
                local amount, err, absent = resource.amount(snap, out.resource)
                if not amount then
                    problems[#problems + 1] = ('%s: %s'):format(id, err)
                    broken = true
                else
                    if absent then
                        missing[#missing + 1] = resource.describe(out.resource)
                    end
                    local ratio = math.min(amount / out.target, 1)
                    local urgency = 1 - ratio ^ (out.priority or 1)
                    st.outputs[#st.outputs + 1] = {
                        resource = out.resource, circuit = out.circuit,
                        amount = amount, target = out.target, urgency = urgency,
                    }
                    if amount < out.target then
                        st.done = false
                    end
                    if amount < startRatio * out.target then
                        st.low = true
                    end
                    if urgency > st.urgency or not st.focus then
                        st.urgency, st.focus = urgency, out.circuit
                    end
                end
            end
        end
        if broken then
            -- Wrong numbers are worse than none: never start it, stop it if it runs.
            st.low, st.done = false, true
        end
        states[id] = st
    end
    table.sort(problems)
    table.sort(missing)
    return states, problems, missing
end

---Which recipe should run now; nil means stop the QFT.
---The running recipe keeps going until done; otherwise the most urgent low recipe wins.
---@param states table<string, qft.RecipeState>
---@param current string? # Recipe running now.
---@return string?
function plan.choose(states, current)
    local cur = current and states[current]
    if cur and not cur.done then
        return current
    end
    local best, bestUrgency
    for id, st in pairs(states) do
        -- Ties go to the smaller id: pairs() order must not decide.
        if st.low and (best == nil or st.urgency > bestUrgency
                or (st.urgency == bestUrgency and id < best)) then
            best, bestUrgency = id, st.urgency
        end
    end
    return best
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
