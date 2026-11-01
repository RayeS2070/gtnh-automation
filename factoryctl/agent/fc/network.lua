---Which ME component the agents talk to. On GTNH 2.9 AE keeps fluids as fluids, and only a fluid
---interface or the ME controller answers for them (in game, 2026-11-01): those come first. A plain
---ME interface is the last resort (enough for items, and on 2.8.4).
local network = {}

network.kKinds = { 'fluid_interface', 'me_controller', 'me_interface' }

---The address to use: of the first kind that has a match (several of one kind need a prefix).
---@param list fun(kind: string): table<string, any> # component.list(kind, true) in game
---@param prefix string? # the start of an address, from config.lua
---@return string? address
---@return string kind_or_error
function network.find(list, prefix)
    for _, kind in ipairs(network.kKinds) do
        local found = {}
        for address in pairs(list(kind)) do
            if not prefix or address:sub(1, #prefix) == prefix then
                found[#found + 1] = address
            end
        end
        if #found > 1 then
            table.sort(found)
            return nil, ('%d components %s: give one by its address in config.lua'):format(#found,
                kind)
        end
        if #found == 1 then
            return found[1], kind
        end
    end
    return nil, 'no ' .. table.concat(network.kKinds, ', ')
        .. (prefix and (' at ' .. prefix) or '')
end

return network
