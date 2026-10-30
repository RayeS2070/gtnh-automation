---The agent's scan of the ME network: getItemsInNetworkById over id chunks, as the server's
---scan task says. Ids slow.from..slow.to hold items with a great many NBT variants (EECs):
---they go in small chunks, or the game server runs out of memory.
local scan = {}

---@class fc.ScanParams
---@field id integer
---@field threshold integer
---@field maxId integer
---@field chunk integer
---@field slow { from: integer, to: integer, chunk: integer }

---Iterates (first, last) id chunks over 0..maxId: big chunks stop short of the slow range,
---inside it chunks are small.
---@param p fc.ScanParams
---@return fun(): integer?, integer?
function scan.chunks(p)
    local first = 0
    return function()
        if first > p.maxId then
            return nil
        end
        local last
        if first >= p.slow.from and first <= p.slow.to then
            last = math.min(first + p.slow.chunk - 1, p.slow.to, p.maxId)
        else
            last = math.min(first + p.chunk - 1, p.maxId)
            if first < p.slow.from and last >= p.slow.from then
                last = p.slow.from - 1
            end
        end
        local from = first
        first = last + 1
        return from, last
    end
end

return scan
