---When to poll the server next. The agent's loop asks only `trigger:wait(seconds)`:
---  'due'         the poll interval is over;
---  'early'       something the server should hear about now (a craft ended);
---  'interrupted' Ctrl+C.
---
---`polling` is a stopgap for GTNH 2.8.4, which has no events from AE: it looks at the agent's own
---crafting jobs every `step` seconds (a few calls: only the jobs it requested). On 2.9 an `events`
---trigger can take its place without touching the loop: subscribe with
---setItemEventSubscription(true) and wait in event.pull for network_item_changed (deltas of the
---network), returning 'early' when they touch a target or a held order.
local trigger = {}

---@class fc.PollingDeps
---@field jobs table # fc.jobs: update() and results()
---@field pull fun(timeout: number): string? # event.pull(timeout, 'interrupted') in game
---@field time fun(): number # computer.uptime in game
---@field step number? # seconds between looks at the jobs (default 1)
---@field minGap number? # never poll early sooner than this after the last poll (default 2)

---@param deps fc.PollingDeps
function trigger.polling(deps)
    local step, minGap = deps.step or 1, deps.minGap or 2
    local t = {}

    ---@param seconds number # the poll interval the server asked for
    ---@return string # 'due' | 'early' | 'interrupted'
    function t:wait(seconds)
        local start = deps.time()
        while true do
            local elapsed = deps.time() - start
            if elapsed >= seconds then
                return 'due'
            end
            if deps.pull(math.min(step, seconds - elapsed)) == 'interrupted' then
                return 'interrupted'
            end
            deps.jobs:update()
            if #deps.jobs:results() > 0 and deps.time() - start >= minGap then
                return 'early'
            end
        end
    end

    return t
end

return trigger
