---POST to the server through the Internet Card. Errors come back as nil and a reason: OpenOS's
---internet.request throws on a failed read.
local internet = require('internet')

local http = {}

---@param url string
---@param body string # JSON
---@return string? text
---@return string? err
function http.post(url, body)
    local ok, res = pcall(function()
        local handle = internet.request(url, body, { ['Content-Type'] = 'application/json' }, 'POST')
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
