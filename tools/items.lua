---Scans the items of an ME network and reports how items are identified: how many share a
---name (told apart by damage), how many carry NBT, which labels collide.
---Safe for a big network: stacks are never collected, only counted.
---  items [filter] [--byid] [--max=<id>] [--chunk=<n>] [--slow=<from>-<to>] [--slow-chunk=<n>]
---        [--addr=<prefix>] [--out=<file>] [--top=<n>] [--labels]
---    filter   case-insensitive substring of name or label: matching stacks are listed
---    --byid   read with getItemsInNetworkById over numeric ids 0..max, chunk ids per call,
---             instead of the allItems() iterator
---    --max    highest numeric item id to ask for with --byid (default 11500)
---    --chunk  ids per call with --byid (default 1000)
---    --slow   ids asked for in small chunks (default 250-320)
---    --slow-chunk  ids per call inside --slow (default 5)
---
---The --byid chunks follow the InfluxDB exporter, which runs on the server without trouble:
---ids 250..320 hold items with a great many NBT variants (damaged/enchanted items from EECs),
---and a big chunk over them ran the server out of memory. Our old default (256 ids per call)
---covered them in one call, and a scan with it brought the server down.
---    --addr   only the interface/controller whose address starts with this
---    --out    write the whole report to a file instead of the screen
---    --top    how many entries of each top list to show (default 15)
---    --labels also count labels shared by different name:damage, and name:damage shared by
---             several stacks (= told apart by NBT only); more memory
---
---allItems() is not reliable on a busy network: on the same network it once stopped at exactly
---2000 stacks without an error and later returned 5578. --byid asks for a small range of ids
---per call, so a change in the network can only affect the ids of that call.
local component = require('component')
local computer = require('computer')
local shell = require('shell')

local args, opts = shell.parse(...)
local filter = args[1] and args[1]:lower()
local top = tonumber(opts.top) or 15
local maxId = tonumber(opts.max) or 11500
local chunk = tonumber(opts.chunk) or 1000
local slowFrom, slowTo = 250, 320 ---@type number?, number?
if type(opts.slow) == 'string' then
    slowFrom, slowTo = opts.slow:match('^(%d+)%-(%d+)$')
    slowFrom, slowTo = tonumber(slowFrom), tonumber(slowTo)
    assert(slowFrom and slowTo and slowFrom <= slowTo, '--slow must look like 250-320')
end
local slowChunk = tonumber(opts['slow-chunk']) or 5

local kYieldEvery = 200 -- stacks between yields: avoids "too long without yielding"
local kMinFree = 64 * 1024 -- stop before the computer runs out of memory (bytes)

local out = io.stdout
if type(opts.out) == 'string' then
    out = assert(io.open(opts.out, 'w'))
end
local function say(fmt, ...)
    out:write(fmt:format(...), '\n')
end

local function matches(stack)
    -- plain find: a filter like "dust (" must not be read as a pattern
    return stack.name:lower():find(filter, 1, true) ~= nil
        or (stack.label or ''):lower():find(filter, 1, true) ~= nil
end

---Feeds every stack of the network to visit() through the allItems() iterator.
---visit returns false to stop. Returns why it stopped early, or nil.
local function viaIterator(proxy, visit)
    local ok, it = pcall(proxy.allItems)
    if not ok then
        return 'allItems failed: ' .. tostring(it)
    end
    while true do
        local okNext, stack = pcall(it)
        if not okNext then
            return 'iterator call failed: ' .. tostring(stack)
        end
        if stack == nil then
            return nil
        end
        if visit(stack) == false then
            return 'stopped by the scan'
        end
    end
end

---The last id of the chunk that starts at `first`: small chunks inside the slow range, and a
---big chunk stops short of it.
local function chunkEnd(first)
    if first >= slowFrom and first <= slowTo then
        return math.min(first + slowChunk - 1, slowTo, maxId)
    end
    local last = math.min(first + chunk - 1, maxId)
    if first < slowFrom and last >= slowFrom then
        last = slowFrom - 1
    end
    return last
end

---Same, through getItemsInNetworkById over ids 0..maxId, chunk by chunk (see chunkEnd).
---Also reports the first id of the last chunk that returned anything: if it is close to
---maxId, items with higher ids may exist and --max should be raised.
local function viaIds(proxy, visit, info)
    local first = 0
    while first <= maxId do
        local last = chunkEnd(first)
        local ids = {}
        for id = first, last do
            ids[#ids + 1] = id
        end
        local ok, stacks = pcall(proxy.getItemsInNetworkById, ids)
        if not ok then
            return ('getItemsInNetworkById(%d..%d) failed: %s'):format(first, ids[#ids], tostring(stacks))
        end
        if #stacks > 0 then
            info.lastChunk = first
        end
        for _, stack in ipairs(stacks) do
            if visit(stack) == false then
                return 'stopped by the scan'
            end
        end
        os.sleep(0) -- one call per chunk: yield between them as well
        first = last + 1
    end
    return nil
end

local function scan(address)
    local proxy = component.proxy(address)

    local total, damaged, tagged, listed = 0, 0, 0, 0
    local perName = {} -- name -> number of stacks (= variants: AE keeps one stack per item)
    local labelIds = opts.labels and {} or nil -- label -> first "name:damage", or true if shared
    local sharedLabels = 0
    local sharedLabelList = {} -- label -> ids seen under it; the list is short in practice
    local perId = opts.labels and {} or nil -- "name:damage" -> number of stacks
    local sharedIds, inSharedIds = 0, 0
    local lowMemory

    local function visit(stack)
        total = total + 1
        local damage = stack.damage or 0
        if damage ~= 0 then damaged = damaged + 1 end
        if stack.hasTag then tagged = tagged + 1 end
        perName[stack.name] = (perName[stack.name] or 0) + 1

        if labelIds then
            local label, idStr = stack.label or '?', ('%s:%d'):format(stack.name, damage)
            local seen = labelIds[label]
            if seen == nil then
                labelIds[label] = idStr
            elseif seen ~= true and seen ~= idStr then
                labelIds[label] = true
                sharedLabels = sharedLabels + 1
                sharedLabelList[label] = { seen, idStr }
            elseif seen == true then
                local ids = sharedLabelList[label]
                local known = false
                for _, x in ipairs(ids) do
                    if x == idStr then known = true break end
                end
                if not known then ids[#ids + 1] = idStr end
            end

            local n = (perId[idStr] or 0) + 1
            perId[idStr] = n
            if n == 2 then
                sharedIds = sharedIds + 1
                inSharedIds = inSharedIds + 2
            elseif n > 2 then
                inSharedIds = inSharedIds + 1
            end
        end

        if filter and matches(stack) then
            listed = listed + 1
            say('%s:%d  %s  x%d%s', stack.name, damage, stack.label or '?', stack.size or 0,
                stack.hasTag and '  [NBT]' or '')
        end

        if total % kYieldEvery == 0 then
            os.sleep(0)
            if computer.freeMemory() < kMinFree then
                lowMemory = ('low memory after %d stacks'):format(total)
                return false
            end
        end
    end

    local info = {}
    local started = computer.uptime()
    local stopped
    if opts.byid then
        stopped = viaIds(proxy, visit, info)
    else
        stopped = viaIterator(proxy, visit)
    end
    stopped = lowMemory or stopped

    local names, shared, inShared = 0, {}, 0
    for name, n in pairs(perName) do
        names = names + 1
        if n > 1 then
            shared[#shared + 1] = { name = name, n = n }
            inShared = inShared + n
        end
    end
    table.sort(shared, function(a, b) return a.n > b.n end)

    say('== %s via %s, %.0fs%s', address, opts.byid and 'getItemsInNetworkById' or 'allItems',
        computer.uptime() - started, stopped and ('  STOPPED: ' .. stopped) or '')
    if opts.byid then
        say('ids 0..%d by %d (%d..%d by %d); last chunk with items starts at %s%s', maxId, chunk,
            slowFrom, slowTo, slowChunk, tostring(info.lastChunk),
            info.lastChunk and info.lastChunk + 2 * chunk > maxId and '  (near the limit: raise --max)' or '')
    end
    say('stacks %d, names %d, damage ~= 0: %d, with NBT: %d', total, names, damaged, tagged)
    say('names shared by several items: %d (%d stacks)', #shared, inShared)
    if labelIds then
        say('labels shared by different name:damage: %d', sharedLabels)
        local labels = {}
        for label in pairs(sharedLabelList) do labels[#labels + 1] = label end
        table.sort(labels)
        for i = 1, math.min(top, #labels) do
            local ids = sharedLabelList[labels[i]]
            local shown = table.concat(ids, ', ', 1, math.min(3, #ids))
            say('  "%s": %s%s', labels[i], shown, #ids > 3 and (' (+%d more)'):format(#ids - 3) or '')
        end
        say('name:damage shared by several stacks (NBT only): %d (%d stacks)', sharedIds, inSharedIds)
    end
    if filter then
        say('listed %d stacks matching "%s"', listed, filter)
    end
    for i = 1, math.min(top, #shared) do
        say('  %5d  %s', shared[i].n, shared[i].name)
    end
end

for _, ctype in ipairs({ 'me_interface', 'me_controller' }) do
    for address in component.list(ctype, true) do
        if type(opts.addr) ~= 'string' or address:sub(1, #opts.addr) == opts.addr then
            scan(address)
        end
    end
end

if out ~= io.stdout then
    out:close()
end
