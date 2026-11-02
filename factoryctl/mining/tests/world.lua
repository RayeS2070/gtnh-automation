---The fake world of the mining agent's tests: a mining module, its stocking bus and hatch, the
---drone bus and transposer, the pool interface, the storage buses, the buffer, the server and the
---clock. Not installed in game.
local kDrone = 'gtnhintergalactic:item.MiningDrone'
local kOre = { name = 'gregtech:gt.blockores2', damage = 2982 }

local function decode(text) return load('return ' .. text, '=data', 't', {})() end

-- What the server says: a literal of plain data, as the server's toLua makes it.
local function lua(v)
    if type(v) == 'table' then
        local parts = {}
        local n = #v
        for i = 1, n do parts[#parts + 1] = lua(v[i]) end
        local keys = {}
        for k in pairs(v) do
            if not (math.type(k) == 'integer' and k >= 1 and k <= n) then keys[#keys + 1] = k end
        end
        table.sort(keys)
        for _, k in ipairs(keys) do parts[#parts + 1] = k .. '=' .. lua(v[k]) end
        return '{' .. table.concat(parts, ',') .. '}'
    elseif type(v) == 'string' then
        return ('%q'):format(v)
    end
    return tostring(v)
end

local function assignment(tier, distance)
    return { x = 169, y = 33, z = -36, id = ('%d/%d/3/4'):format(tier, distance), droneTier = tier,
        drone = { name = kDrone, damage = tier },
        tip = { name = 'gregtech:gt.metaitem.02', damage = 8000 + tier },
        rod = { name = 'gregtech:gt.metaitem.01', damage = 23000 + tier },
        plasma = 'plasma.radon', distance = distance, parallel = 4 }
end

---The world. The module works while work is allowed and it has a drone in its bus, a tip and rod
---in its stocking bus and a plasma in its hatch; an operation lasts `opTime` s and, disallowed,
---ends at its end.
local function fakeWorld()
    local w = { now = 0, opTime = 30, calls = {}, posts = {}, pool = { [5] = 2, [9] = 1 },
        lendDelay = 1, tips = true, reply = nil, buffer = {} }
    local function log(what) w.calls[#w.calls + 1] = what end

    w.controller = { address = 'ctrl-0001', allowed = false, opEnd = nil, params = {} }
    local c = w.controller
    function c.getName() return 'projectmoduleminert2' end
    function c.getCoordinates() return 169, 33, -36 end
    function c.setWorkAllowed(v) c.allowed = v; log('work ' .. tostring(v)) end
    function c.setParameter(k, v) c.params[k] = v; log(('param %s=%s'):format(k, tostring(v))) end
    function c.isMachineActive()
        if c.opEnd and w.now >= c.opEnd then c.opEnd = nil end
        local ready = w.droneBus[1] and w.bus[1] and w.bus[2] and w.hatch[1] and w.tips
        if not c.opEnd and c.allowed and ready then c.opEnd = w.now + w.opTime end
        return c.opEnd ~= nil
    end

    local function stocking(address, x, y, z, slots)
        local p = { address = address }
        function p.getCoordinates() return x, y, z end
        function p.setConfiguration(slot, detail) slots[slot] = detail; log(address .. ' ' .. slot) end
        return p
    end
    w.bus, w.hatch = {}, {}
    w.inputbus = stocking('bus-0001', 169, 34, -35, w.bus)
    w.inputhatch = stocking('hatch-0001', 169, 33, -35, w.hatch)
    w.farbus = stocking('bus-0002', 400, 34, -35, {}) -- another module's, far away

    w.droneBus, w.buffer9 = { { name = kDrone, damage = 5, size = 1 } }, {}
    w.lent = nil -- { tier, at }: the interface brings a drone into its slot 1
    local function poolSlot()
        if w.lent and w.now >= w.lent.at and (w.pool[w.lent.tier] or 0) > 0 then
            w.pool[w.lent.tier] = w.pool[w.lent.tier] - 1
            w.buffer9[1] = { name = kDrone, damage = w.lent.tier, size = 1 }
            w.lent.at = math.huge -- one only
        end
        return w.buffer9
    end
    w.transposer = { address = 'tp-0001' }
    local t = w.transposer
    function t.getStackInSlot(side, slot)
        if side == 4 then return w.droneBus[slot] end
        if side == 0 then return poolSlot()[slot] end
        return nil
    end
    function t.getInventoryName(side)
        if side == 4 then return 'gt.blockmachines' end
        if side == 0 then return 'tile.fluid_interface' end
        if side == 1 then return 'tile.oc.adapter' end
        return nil, 'no inventory'
    end
    function t.transferItem(from, to, count, slot, toSlot)
        local src = from == 4 and w.droneBus or poolSlot()
        local stack = src[slot or 1]
        if not stack then return 0 end
        src[slot or 1] = nil
        if to == 0 then
            w.pool[stack.damage] = (w.pool[stack.damage] or 0) + 1 -- the interface takes it in
        else
            w.droneBus[toSlot or 1] = stack
        end
        log(('transfer %d->%d %d'):format(from, to, stack.damage))
        return 1
    end
    w.interface = { address = 'if-0001' }
    function w.interface.setInterfaceConfiguration(slot, detail)
        if detail then
            w.lent = { tier = detail.damage, at = w.now + w.lendDelay }
        else
            w.lent = nil
            if w.buffer9[1] then -- what it holds unconfigured goes back into the network
                w.pool[w.buffer9[1].damage] = (w.pool[w.buffer9[1].damage] or 0) + 1
                w.buffer9[1] = nil
            end
        end
        return true
    end
    function w.interface.getItemsInNetwork(filter)
        local out = {}
        for tier, n in pairs(w.pool) do
            if n > 0 then out[#out + 1] = { name = kDrone, damage = tier, size = n } end
        end
        return out
    end
    w.storage = { address = 'sb-0001', slots = {} }
    function w.storage.getStorageSlotSize(side) assert(side == 4) return 63 end
    function w.storage.setStorageConfiguration(side, slot, detail) w.storage.slots[slot] = detail end
    w.void = { address = 'sv-0001', slots = {} }
    function w.void.getStorageSlotSize() return 18 end
    function w.void.setStorageConfiguration(side, slot, detail) w.void.slots[slot] = detail end
    w.bufferNet = { address = 'bf-0001' }
    function w.bufferNet.getItemInNetwork(name, damage)
        local n = w.buffer[name .. ':' .. damage]
        return n and { name = name, damage = damage, size = n } or nil
    end

    w.components = {
        gt_machine = { w.controller }, me_inputbus = { w.farbus, w.inputbus },
        me_inputhatch = { w.inputhatch }, transposer = { w.transposer },
        fluid_interface = { w.interface }, me_storagebus = { w.storage, w.void },
        me_controller = { w.bufferNet },
    }
    local byAddress = {}
    for _, list in pairs(w.components) do
        for _, p in ipairs(list) do byAddress[p.address] = p end
    end
    w.deps = {
        list = function(kind)
            local list, i = w.components[kind] or {}, 0
            return function() i = i + 1; return list[i] and list[i].address end
        end,
        proxy = function(a) return byAddress[a] end,
        post = function(path, body)
            w.posts[#w.posts + 1] = body
            w.lastBody = body
            if not w.reply then return nil, 'connection refused' end
            return lua(w.reply())
        end,
        decode = decode,
        config = {
            name = 'mining-1',
            drones = { ['169,33,-36'] = { transposer = 'tp-', bus = 4, pool = 0, interface = 'if-' } },
            routes = { keep = { bus = 'sb-', side = 4 }, void = { bus = 'sv-', side = 4 } },
            buffer = 'bf-',
        },
        run = 'test',
        warn = function(fmt, ...) w.warnings = (w.warnings or '') .. fmt:format(...) .. '\n' end,
    }
    return w
end

---Runs the agent until `untilT`, as main.lua does (step, then wait what it says).
local function run(w, m, untilT)
    while w.now < untilT do
        local wait = m:step(w.now)
        w.now = math.min(w.now + wait, untilT)
    end
end

return { fakeWorld = fakeWorld, run = run, assignment = assignment, lua = lua, decode = decode,
    kOre = kOre, kDrone = kDrone }
