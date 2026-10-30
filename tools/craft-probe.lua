---In-game probe for factoryctl: orders a craft and records how the CPU reports its progress.
---Question: does finalOutput().size shrink while the craft runs (a cheap progress signal),
---or do only activeItems()/pendingItems() change?
---
---  craft-probe [label] [--amount=<n>] [--every=<s>] [--cpu=<name>] [--cancel-after=<s>]
---              [--no-lists] [--out=<file>]
---    label          craftable item label (default Stick)
---    --amount       how many to order (default 100000)
---    --every        seconds between samples (default 0.5)
---    --cpu          CPU name to request the craft on (default: AE chooses)
---    --cancel-after cancel the craft through AECpu.cancel() after that many seconds
---    --no-lists     do not sum activeItems/pendingItems (they can be long on big crafts)
---    --out          write every sample as CSV (default craft-probe.csv)
---Ctrl+C stops sampling; the craft keeps running unless --cancel-after was given.
local component = require('component')
local computer = require('computer')
local shell = require('shell')

local args, opts = shell.parse(...)
local label = args[1] or 'Stick'
local amount = tonumber(opts.amount) or 100000
local every = tonumber(opts.every) or 0.5
local cancelAfter = tonumber(opts['cancel-after'])
local withLists = not opts['no-lists']
local outPath = type(opts.out) == 'string' and opts.out or 'craft-probe.csv'

local kIdleSamplesToStop = 6 -- samples without our craft on a CPU, after it was seen, to stop
local kNeverSeenSecondsToStop = 30 -- job computed but no CPU shows the craft: give up

local function network()
    for _, kind in ipairs({ 'me_interface', 'me_controller' }) do
        if component.isAvailable(kind) then
            return component[kind], kind
        end
    end
    error('no me_interface or me_controller connected')
end

local function sumSizes(list)
    local n, total = 0, 0
    for _, stack in ipairs(list or {}) do
        n = n + 1
        total = total + (stack.size or 0)
    end
    return n, total
end

---The busy CPU whose final output carries our label, or nil.
local function findCpu(me)
    for _, meta in ipairs(me.getCpus()) do
        local ok, final = pcall(meta.cpu.finalOutput)
        if ok and final and final.label == label then
            return meta, final
        end
    end
    return nil
end

local function jobState(job)
    local function flag(method)
        local ok, v = pcall(job[method])
        if not ok then
            return '?'
        end
        return v and '1' or '0'
    end
    return ('computing=%s failed=%s canceled=%s done=%s'):format(
        flag('isComputing'), flag('hasFailed'), flag('isCanceled'), flag('isDone'))
end

local me, kind = network()
print(('network: %s %s'):format(kind, me.address:sub(1, 8)))

local craftables = me.getCraftables({ label = label })
if #craftables ~= 1 then
    print(('%d craftables with label %q; need exactly one'):format(#craftables, label))
    for i, c in ipairs(craftables) do
        local s = c.getItemStack()
        print(('  %d: %s:%s'):format(i, s.name, tostring(s.damage)))
    end
    return
end
local stack = craftables[1].getItemStack()
print(('ordering %d x %s (%s:%s)'):format(amount, label, stack.name, tostring(stack.damage)))

-- Open the log before ordering: a failure after request() leaves the craft running unwatched.
local out = assert(io.open(outPath, 'w'))
local job = craftables[1].request(amount, nil, opts.cpu)
out:write('t,job,cpu,busy,final_size,active_n,active_sum,pending_n,pending_sum\n')

local start = computer.uptime()
local last, finalSizes, idle, canceled = nil, {}, 0, false

local function sample()
    local t = computer.uptime() - start
    local state = jobState(job)
    local meta, final = findCpu(me)
    local row = { cpu = '-', busy = '-', final = '-', an = '-', as = '-', pn = '-', ps = '-' }
    if meta then
        row.cpu, row.busy = meta.name, tostring(meta.busy)
        row.final = tostring(final.size)
        finalSizes[#finalSizes + 1] = final.size
        if withLists then
            row.an, row.as = sumSizes(meta.cpu.activeItems())
            row.pn, row.ps = sumSizes(meta.cpu.pendingItems())
        end
        idle = 0
    else
        idle = idle + 1
    end
    out:write(('%.2f,%s,%s,%s,%s,%s,%s,%s,%s\n'):format(t, state, row.cpu, row.busy, row.final,
        row.an, row.as, row.pn, row.ps))

    local line = ('%s cpu=%s final=%s active=%s/%s pending=%s/%s'):format(state, row.cpu,
        row.final, row.an, row.as, row.pn, row.ps)
    if line ~= last then
        print(('[%7.2f] %s'):format(t, line))
        last = line
    end

    if cancelAfter and not canceled and meta and t >= cancelAfter then
        local ok, res = pcall(meta.cpu.cancel)
        print(('[%7.2f] cancel() on %s -> %s %s'):format(t, meta.name, tostring(ok), tostring(res)))
        canceled = true
    end
    if not state:find('computing=0') then
        return false
    end
    if #finalSizes > 0 then
        return idle >= kIdleSamplesToStop
    end
    return t >= kNeverSeenSecondsToStop
end

local ok, err = pcall(function()
    while not sample() do
        os.sleep(every)
    end
end)
out:close()
if not ok and not tostring(err):find('interrupted') then
    print('error: ' .. tostring(err))
end

-- Summary: is final size a usable progress signal?
local distinct, shrinking, prev = 0, true, nil
for _, s in ipairs(finalSizes) do
    if s ~= prev then
        distinct = distinct + 1
        if prev and s > prev then
            shrinking = false
        end
        prev = s
    end
end
print(('samples on CPU: %d, distinct final sizes: %d, first %s, last %s, never grew: %s')
    :format(#finalSizes, distinct, tostring(finalSizes[1]), tostring(finalSizes[#finalSizes]),
        tostring(shrinking)))
print('all samples: ' .. outPath)
