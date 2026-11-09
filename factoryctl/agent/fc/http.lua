---POST to the server through the Internet Card. Errors come back as nil and a reason: OpenOS's
---internet.request throws on a failed read.
local internet = require('internet')

local http = {}

local kHeaders = { ['Content-Type'] = 'application/json' }

---Reads the card's request handle itself: OpenOS's iterator waits inside with no way out. The
---card connects and reads on its own threads; read() gives '' until data is there, nil at the end
---(OpenComputers InternetCard.HTTPRequest), close() cancels them. read() is an indirect call: it
---takes a tick and yields meanwhile, so the loop needs no sleep, and without one (os.sleep is
---event.pull) no signal is taken from the queue: they wait for the caller's own pull.
local function postBefore(url, body, timeout)
    local component = require('component')
    local computer = require('computer')
    local deadline = computer.uptime() + timeout
    local handle, reason = component.internet.request(url, body, kHeaders, 'POST')
    if not handle then
        return nil, tostring(reason)
    end
    local parts = {}
    local ok, err = pcall(function()
        while true do
            -- n is optional (InternetCard.HTTPRequest.read, optInteger); the gtnh stub says not.
            ---@diagnostic disable-next-line: missing-parameter
            local data, why = handle.read()
            if data == nil then
                if why then
                    error(why, 0)
                end
                return
            elseif #data > 0 then
                parts[#parts + 1] = data
            elseif computer.uptime() >= deadline then
                error(('no answer in %.1f s'):format(timeout), 0)
            end
        end
    end)
    pcall(handle.close)
    if not ok then
        return nil, tostring(err)
    end
    return table.concat(parts)
end

---@param url string
---@param body string # JSON
---@param timeout number? # s to give up after; nil: as long as it takes
---@return string? text
---@return string? err
function http.post(url, body, timeout)
    if timeout then
        return postBefore(url, body, timeout)
    end
    local ok, res = pcall(function()
        local handle = internet.request(url, body, kHeaders, 'POST')
        local parts = {}
        for chunk in handle do
            parts[#parts + 1] = chunk
        end
        return table.concat(parts)
    end)
    if not ok then
        return nil, tostring(res)
    end
    return res
end

return http
