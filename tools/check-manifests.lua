---Local check, not for the game: every <project>/manifest.lua lists existing files and covers
---every .lua file of the project except tests/. Run from the repository root:
---  lua tools/check-manifests.lua
local function lines(cmd)
    local out = {}
    local p = assert(io.popen(cmd))
    for line in p:lines() do
        out[#out + 1] = line
    end
    p:close()
    return out
end

local function exists(path)
    local f = io.open(path, 'r')
    if f then
        f:close()
    end
    return f ~= nil
end

local failed = 0
local function fail(fmt, ...)
    print(fmt:format(...))
    failed = failed + 1
end

for _, path in ipairs(lines("git ls-files -co --exclude-standard '*/manifest.lua'")) do
    local project = path:match('^([^/]+)/manifest%.lua$')
    if project then
        local manifest = assert(loadfile(path, 't', {}))()
        local listed = {}
        for _, field in ipairs({ 'code', 'config' }) do
            for _, file in ipairs(manifest[field] or {}) do
                listed[file] = true
                if not exists(project .. '/' .. file) then
                    fail('%s: %s lists a missing file %s', project, field, file)
                end
            end
        end
        local cmd = ("git ls-files -co --exclude-standard -- '%s/*.lua'"):format(project)
        for _, file in ipairs(lines(cmd)) do
            local rel = file:sub(#project + 2)
            -- ls-files still lists tracked files deleted from disk
            if exists(file) and rel ~= 'manifest.lua' and not rel:match('^tests/')
                and not rel:match('/tests/')
                and not listed[rel] then
                fail('%s: %s is not in manifest.lua', project, rel)
            end
        end
    end
end

if failed > 0 then
    os.exit(1)
end
print('manifests ok')
