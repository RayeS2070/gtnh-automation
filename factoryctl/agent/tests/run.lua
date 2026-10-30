---Local tests of the agent with fake components built from real dumps. Not installed in game.
---  cd factoryctl/agent && lua tests/run.lua
package.path = './?.lua;' .. package.path

local failures, total = 0, 0
local function check(cond, what)
    total = total + 1
    if not cond then
        failures = failures + 1
        print('FAIL ' .. what)
    end
end

local function eq(got, want, what)
    check(got == want, ('%s: got %s, want %s'):format(what, tostring(got), tostring(want)))
end

local suites = {}
local function suite(name, fn)
    suites[#suites + 1] = { name = name, fn = fn }
end

-- json ---------------------------------------------------------------------------------------
suite('json', function()
    local json = require('fc.json')
    eq(json.encode('plain'), '"plain"', 'string')
    eq(json.encode('say "hi"\\'), '"say \\"hi\\"\\\\"', 'quotes and backslash')
    eq(json.encode('a\nb\tc\1'), '"a\\nb\\tc\\u0001"', 'control characters')
    eq(json.encode('Тест §a'), '"Тест §a"', 'UTF-8 stays')
    eq(json.encode(42), '42', 'integer')
    eq(json.encode(4503599627370495.0), '4503599627370495', 'integral float as integer')
    eq(json.encode(-7), '-7', 'negative')
    eq(json.encode(true), 'true', 'true')
    eq(json.encode(false), 'false', 'false')
    eq(json.encode({}), '[]', 'empty table is an empty array')
    eq(json.encode({ 1, 'a', true }), '[1,"a",true]', 'array')
    eq(json.encode({ name = 'CPU_001' }), '{"name":"CPU_001"}', 'object')
    eq(json.encode({ b = 1, a = 2 }), '{"a":2,"b":1}', 'keys sorted: stable output')
    eq(json.encode({ cpus = {}, results = { { order = 1, status = 'done' } } }),
        '{"cpus":[],"results":[{"order":1,"status":"done"}]}', 'nested')
    check(not pcall(json.encode, 0.5), 'a fraction is refused: sizes are integers')
    check(not pcall(json.encode, function() end), 'a function is refused')
end)

-- scan chunks --------------------------------------------------------------------------------
suite('chunks', function()
    local scan = require('fc.scan')
    local params = { maxId = 11500, chunk = 1000, slow = { from = 250, to = 320, chunk = 5 } }
    local list = {}
    for first, last in scan.chunks(params) do
        list[#list + 1] = { first, last }
    end
    eq(#list, 28, 'chunk count, as the exporter scheme')
    eq(list[1][1] .. '..' .. list[1][2], '0..249', 'a big chunk stops short of the slow range')
    eq(list[2][1] .. '..' .. list[2][2], '250..254', 'slow chunks')
    eq(list[16][1] .. '..' .. list[16][2], '320..320', 'the slow range ends at its last id')
    eq(list[17][1] .. '..' .. list[17][2], '321..1320', 'big chunks again')
    eq(list[28][1] .. '..' .. list[28][2], '11321..11500', 'the last chunk ends at maxId')
    local covered, prev = 0, -1
    for _, c in ipairs(list) do
        check(c[1] == prev + 1, 'no gap or overlap at ' .. c[1])
        covered, prev = covered + c[2] - c[1] + 1, c[2]
    end
    eq(covered, 11501, 'every id 0..maxId once')
end)

-- fake me_interface from the dumps ----------------------------------------------------------
local fake = {}

---getItemsInNetworkById over the network sample: a stack belongs to the chunk it was dumped
---in (`_chunk`, the first id of that call). Records the calls.
function fake.network(extra)
    local stacks = dofile('tests/fixtures/me_sample.lua')
    for _, s in ipairs(extra or {}) do
        stacks[#stacks + 1] = s
    end
    local me = { calls = {} }
    function me.getItemsInNetworkById(ids)
        me.calls[#me.calls + 1] = ids[1] .. '..' .. ids[#ids]
        local out = {}
        for _, s in ipairs(stacks) do
            if s._chunk >= ids[1] and s._chunk <= ids[#ids] then
                out[#out + 1] = s
            end
        end
        return out
    end
    return me
end

---getCpus over the CPU dump (32 CPUs, CPU_022 with the busy=false race).
function fake.cpus(rows)
    rows = rows or dofile('tests/fixtures/cpus.lua')
    local me = { canceled = {} }
    function me.getCpus()
        local out = {}
        for _, r in ipairs(rows) do
            out[#out + 1] = {
                name = r.name, busy = r.busy, storage = 1, coprocessors = 0,
                cpu = {
                    finalOutput = function()
                        if r.fails then error('cpu gone') end
                        return r.finalOutput
                    end,
                    cancel = function()
                        me.canceled[#me.canceled + 1] = r.name
                        r.finalOutput, r.busy = nil, false
                        return true
                    end,
                },
            }
        end
        return out
    end
    return me
end

-- me: stacks and CPUs in the protocol's shape --------------------------------------------------
suite('me', function()
    local me = require('fc.me')
    local drop = { name = 'ae2fc:fluid_drop', damage = 0, label = 'drop of Water', size = 4503599627370495.0,
        isCraftable = false, hasTag = true, tag = 'binary\0nbt', inputs = {}, outputs = {},
        fluidDrop = { name = 'water', label = 'Water', amount = 1 } }
    local item = me.item(drop)
    eq(item.fluid, 'water', 'a drop carries its fluid')
    eq(item.size, 4503599627370495.0, 'size kept')
    check(item.tag == nil and item.inputs == nil and item.isCraftable == nil, 'only protocol fields')
    local stick = me.item({ name = 'minecraft:stick', damage = 0, label = 'Stick', size = 3 })
    check(stick.fluid == nil, 'an item has no fluid')

    check(me.keep({ size = 500, isCraftable = false }, 500), 'at the threshold: kept')
    check(not me.keep({ size = 499, isCraftable = false }, 500), 'small, not craftable: left out')
    check(me.keep({ size = 0, isCraftable = true }, 500), 'craftable with nothing stored: kept')

    local net = fake.network()
    local items, err = me.chunk(net, 4321, 5320, 500)
    check(items ~= nil, 'chunk read: ' .. tostring(err))
    eq(net.calls[1], '4321..5320', 'one call over the whole chunk')
    local water, small
    for _, i in ipairs(items) do
        if i.fluid == 'water' then water = i end
        if i.size < 500 and i.fluid == nil then small = i end
    end
    check(water ~= nil, 'drop of Water is in its chunk')
    for _, s in ipairs(dofile('tests/fixtures/me_sample.lua')) do
        if s._chunk == 4321 and s.size < 500 and not s.isCraftable then
            for _, i in ipairs(items) do
                check(i.label ~= s.label or i.size >= 500 or i.name ~= s.name, 'small non-craftable left out')
            end
        end
    end
    local broken = { getItemsInNetworkById = function() error('chunk unloaded') end }
    local none, why = me.chunk(broken, 0, 9, 500)
    check(none == nil and tostring(why):find('chunk unloaded') ~= nil, 'a failed call is an error, no throw')

    local cpus = me.cpus(fake.cpus())
    eq(#cpus, 32, 'every CPU')
    local by = {}
    for _, c in ipairs(cpus) do by[c.name] = c end
    eq(by.CPU_022.busy, false, 'busy as getCpus says')
    eq(by.CPU_022.output and by.CPU_022.output.fluid, 'xenoxene', 'the output decides, busy or not')
    eq(by.CPU_031.output and by.CPU_031.output.name, 'dreamcraft:item.TCetiESeaweedExtract', 'item output')
    check(by.CPU_024.output == nil, 'a free CPU has no output')
    local flaky = me.cpus(fake.cpus({ { name = 'CPU_X', busy = true, fails = true } }))
    eq(#flaky, 1, 'a CPU whose finalOutput fails is still reported')
    check(flaky[1].output == nil, '... without an output')
end)

-- jobs: orders, AE crafting jobs, results ---------------------------------------------------------
---A crafting job whose state the test sets.
function fake.job(state)
    state = state or {}
    return {
        state = state,
        isComputing = function() return state.computing == true end,
        hasFailed = function() return state.failed == true end,
        isCanceled = function() return state.canceled == true end,
        isDone = function() return state.done == true end,
    }
end

---getCraftables: `byName[name]` is the list of craftables for a filter on that name; each
---craftable's request() returns `job` (or throws when `job` is a string).
function fake.crafting(byName)
    local me = { requests = {} }
    function me.getCraftables(filter)
        me.lastFilter = filter
        local list = {}
        for _, c in ipairs(byName[filter.name] or {}) do
            list[#list + 1] = {
                request = function(amount)
                    me.requests[#me.requests + 1] = amount
                    if type(c.job) == 'string' then error(c.job) end
                    return c.job
                end,
            }
        end
        return list
    end
    return me
end

local function results(j)
    local parts = {}
    for _, r in ipairs(j:results()) do
        parts[#parts + 1] = r.order .. ':' .. r.status .. (r.message and r.message ~= '' and ('(' .. r.message .. ')') or '')
    end
    return table.concat(parts, ' ')
end

suite('jobs', function()
    local jobs = require('fc.jobs')
    local stickFilter = { name = 'minecraft:stick', damage = 0 }

    -- accepted right away, then done
    local job = fake.job()
    local ae = fake.crafting({ ['minecraft:stick'] = { { job = job } } })
    local j = jobs.new()
    j:start(ae, { id = 18, amount = 1000, filter = stickFilter })
    eq(ae.lastFilter, stickFilter, 'the filter goes to getCraftables as is')
    eq(ae.requests[1], 1000, 'requested the amount')
    eq(results(j), '18:accepted', 'accepted')
    j:confirm()
    eq(results(j), '', 'confirmed results are gone')
    j:update()
    eq(results(j), '', 'no change, nothing to say')
    job.state.done = true
    j:update()
    eq(results(j), '18:done', 'done')
    j:confirm()
    job.state.canceled = true
    j:update()
    eq(results(j), '', 'a finished job is forgotten')

    -- results wait for a successful poll
    j = jobs.new()
    j:start(fake.crafting({ ['minecraft:stick'] = { { job = fake.job({ computing = true }) } } }),
        { id = 1, amount = 1, filter = stickFilter })
    eq(results(j), '1:computing', 'computing')
    j:update()
    eq(results(j), '1:computing', 'unconfirmed results stay, not doubled')

    -- computing, then accepted; hasFailed afterwards is ignored
    j = jobs.new()
    job = fake.job({ computing = true })
    j:start(fake.crafting({ ['minecraft:stick'] = { { job = job } } }), { id = 2, amount = 1, filter = stickFilter })
    j:confirm()
    job.state.computing = false
    j:update()
    eq(results(j), '2:accepted', 'computed: accepted')
    j:confirm()
    job.state.failed = true
    j:update()
    eq(results(j), '', 'hasFailed after acceptance is not trusted (it lies mid-craft in game)')
    job.state.canceled = true
    j:update()
    eq(results(j), '2:canceled', 'canceled wins over failed (in game failed follows cancel)')

    -- AE could not plan it
    j = jobs.new()
    j:start(fake.crafting({ ['minecraft:stick'] = { { job = fake.job({ failed = true }) } } }),
        { id = 3, amount = 1, filter = stickFilter })
    eq(results(j), '3:failed(AE could not plan the craft)', 'failed before acceptance')

    -- nothing or too much matches the filter
    j = jobs.new()
    j:start(fake.crafting({}), { id = 4, amount = 1, filter = stickFilter })
    j:start(fake.crafting({ ['minecraft:stick'] = { { job = fake.job() }, { job = fake.job() } } }),
        { id = 5, amount = 1, filter = stickFilter })
    eq(results(j), '4:failed(not craftable) 5:failed(ambiguous: 2 craftables)', 'filter matches')
    j:start(fake.crafting({ ['minecraft:stick'] = { { job = 'no CPU' } } }), { id = 6, amount = 1, filter = stickFilter })
    check(results(j):find('6:failed%(request: .*no CPU%)') ~= nil, 'request() throwing: failed, not a crash')

    -- a task for an order already held is not run twice
    j = jobs.new()
    ae = fake.crafting({ ['minecraft:stick'] = { { job = fake.job() } } })
    j:start(ae, { id = 7, amount = 1, filter = stickFilter })
    j:start(ae, { id = 7, amount = 1, filter = stickFilter })
    eq(#ae.requests, 1, 'at most once')
end)

suite('cancel', function()
    local jobs = require('fc.jobs')
    local xeno = { name = 'ae2fc:fluid_drop', label = 'drop of Xenoxene' }

    local cpus = fake.cpus()
    local j = jobs.new()
    j:cancel(cpus, { order = 9, cpu = 'CPU_022', filter = xeno })
    eq(cpus.canceled[1], 'CPU_022', 'canceled the CPU crafting our drop')
    eq(results(j), '9:canceled', 'reported at once')

    cpus = fake.cpus()
    j = jobs.new()
    j:cancel(cpus, { order = 9, cpu = 'CPU_031', filter = xeno })
    eq(#cpus.canceled, 0, 'the CPU crafts something else now: left alone')
    eq(results(j), '', 'nothing to report: the server sees the CPU')
    j:cancel(cpus, { order = 9, cpu = 'CPU_404', filter = xeno })
    eq(#cpus.canceled, 0, 'no such CPU: nothing done')

    cpus = fake.cpus()
    j = jobs.new()
    j:cancel(cpus, { order = 10, cpu = 'CPU_031',
        filter = { name = 'dreamcraft:item.TCetiESeaweedExtract', damage = 0 } })
    eq(cpus.canceled[1], 'CPU_031', 'an item matches by name and damage')
end)

-- agent: one poll, its tasks and the scan -----------------------------------------------------
---A server that answers from a script: replies[i] for the i-th request (a string, or false for
---a failed request). Records path and body.
function fake.server(replies)
    local srv = { requests = {} }
    function srv.post(path, body)
        srv.requests[#srv.requests + 1] = { path = path, body = body }
        local r = replies[#srv.requests]
        if r == false then return nil, 'connection refused' end
        return r or '{ok=true}'
    end
    return srv
end

local function decode(text) return load('return ' .. text, '=reply', 't', {})() end

suite('agent', function()
    local agent = require('fc.agent')
    local scanTask = '{id=42,threshold=500,maxId=11500,chunk=1000,slow={from=250,to=320,chunk=5}}'

    -- a poll that starts a scan: the whole network goes in batches, the last one closes it
    local net = fake.network()
    local me = fake.cpus()
    me.getItemsInNetworkById = net.getItemsInNetworkById
    local srv = fake.server({ '{poll=10,orders={},cancel={},scan=' .. scanTask .. '}' })
    local a = agent.new({ me = me, post = srv.post, decode = decode, yield = function() end })
    local reply = a:poll()
    eq(reply and reply.poll, 10, 'the poll interval comes from the server')
    eq(srv.requests[1].path, '/agent/v1/poll', 'poll first')
    check(srv.requests[1].body:find('"name":"CPU_022"', 1, true) ~= nil, 'CPUs in the report')
    check(srv.requests[1].body:find('"fluid":"xenoxene"', 1, true) ~= nil, 'CPU outputs keyed like the scan')
    local scans, last = 0, nil
    for i = 2, #srv.requests do
        eq(srv.requests[i].path, '/agent/v1/scan', 'then scan batches')
        check(srv.requests[i].body:find('"scan":42', 1, true) ~= nil, 'batch of scan 42')
        scans, last = scans + 1, srv.requests[i].body
    end
    check(scans >= 2, 'several batches')
    check(last:find('"last":true', 1, true) ~= nil, 'the last batch closes the scan')
    check(srv.requests[2].body:find('"last":false', 1, true) ~= nil, 'earlier ones do not')
    eq(#net.calls, 28, 'every chunk asked once')

    -- an order: requested, its result goes with the next poll
    local job = fake.job()
    local ae = fake.crafting({ ['minecraft:stick'] = { { job = job } } })
    ae.getCpus = fake.cpus({}).getCpus
    srv = fake.server({
        '{poll=10,orders={{id=18,amount=1000,filter={name="minecraft:stick",damage=0}}},cancel={}}',
        false, -- the next poll fails: the result must survive it
        '{poll=10,orders={},cancel={}}',
        '{poll=10,orders={},cancel={}}',
    })
    a = agent.new({ me = ae, post = srv.post, decode = decode, yield = function() end })
    a:poll()
    eq(ae.requests[1], 1000, 'the order is requested')
    local _, err = a:poll()
    check(err ~= nil, 'a failed poll is an error, not a throw')
    a:poll()
    check(srv.requests[3].body:find('{"order":18,"status":"accepted"}', 1, true) ~= nil,
        'the result goes with the next poll that gets through')
    a:poll()
    check(srv.requests[4].body:find('"results":[]', 1, true) ~= nil, 'and only once')

    -- a refused report keeps the results; a reply that is not Lua is an error
    srv = fake.server({ '{ok=false,error="results[0].status: unknown"}', 'garbage {' })
    a = agent.new({ me = fake.cpus({}), post = srv.post, decode = decode, yield = function() end })
    a.jobs:report(1, 'done')
    local r1, e1 = a:poll()
    check(r1 == nil and tostring(e1):find('unknown') ~= nil, 'the server refused: its error')
    eq(#a.jobs:results(), 1, 'results kept')
    local r2, e2 = a:poll()
    check(r2 == nil and e2 ~= nil, 'a reply that does not parse is an error')

    -- a failed chunk stops the scan; the server gives up on it later
    local broken = fake.cpus({})
    broken.getItemsInNetworkById = function() error('chunk unloaded') end
    srv = fake.server({ '{poll=10,orders={},cancel={},scan=' .. scanTask .. '}' })
    a = agent.new({ me = broken, post = srv.post, decode = decode, yield = function() end })
    a:poll()
    eq(#srv.requests, 1, 'no batch sent after a failed chunk')
end)

for _, s in ipairs(suites) do
    local ok, err = pcall(s.fn)
    if not ok then
        failures = failures + 1
        print(('FAIL suite %s: %s'):format(s.name, err))
    end
end
print(('%d checks, %d failed'):format(total, failures))
os.exit(failures == 0 and 0 or 1)
