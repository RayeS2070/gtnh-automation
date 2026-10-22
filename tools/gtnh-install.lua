---Installs or updates a project of this repository from GitHub. Needs an Internet Card.
---  gtnh-install <project> [dir] [--config] [--branch=<name>]
---The file list comes from <project>/manifest.lua in the repository.
---Code is always replaced; config files are only downloaded when missing, unless --config.
---If /etc/gtnh-install.token exists, its GitHub token is sent with every request (private repo).
local component = require('component')
local filesystem = require('filesystem')
local internet = require('internet')
local shell = require('shell')

local kRepo = 'https://raw.githubusercontent.com/RayeS2070/gtnh-automation/'
local kDefaultBranch = 'devel'
local kTmp = '/tmp/install.tmp'
local kTokenFile = '/etc/gtnh-install.token'

---@type table<string, string>?
local headers = nil

---@class InstallManifest
---@field code string[] replaced on every install
---@field config string[]? edited by the player in game, kept unless --config
---@field run string? command shown when the install is done, run from the install directory

---Fetches the whole body first, so a failed download never leaves a partial file.
---@param url string
---@return string? body
---@return string? error
local function fetchBody(url)
    local ok, result = pcall(function()
        local handle = internet.request(url, nil, headers)
        local chunks = {}
        for chunk in handle do
            chunks[#chunks + 1] = chunk
        end
        -- Errors usually come from the iterator; check the status too where the handle has it.
        -- The stub types the handle as a bare iterator, but OpenOS forwards fields to the card's request.
        ---@diagnostic disable-next-line: undefined-field
        local hasResponse, response = pcall(function() return handle.response end)
        if hasResponse and response then
            local code, message = response()
            if code and code ~= 200 then
                error(('HTTP %s %s'):format(tostring(code), tostring(message or '')), 0)
            end
        end
        return table.concat(chunks)
    end)
    if not ok then
        return nil, tostring(result)
    end
    return result
end

---@param url string
---@param target string
---@return boolean
local function download(url, target)
    local body, err = fetchBody(url)
    if not body then
        print(('  FAILED %s: %s'):format(url, err))
        return false
    end
    local dir = filesystem.path(target)
    if not filesystem.exists(dir) then
        filesystem.makeDirectory(dir)
    end
    local f, openErr = io.open(target, 'w')
    if not f then
        print(('  FAILED %s: %s'):format(target, tostring(openErr)))
        return false
    end
    f:write(body)
    f:close()
    return true
end

---@param path string
---@return string?
local function readAll(path)
    local f = io.open(path, 'r')
    if not f then
        return nil
    end
    local text = f:read('*a')
    f:close()
    return text
end

---A file path must stay inside the install directory.
---@param path any
---@return boolean
local function isRelativePath(path)
    return type(path) == 'string' and path ~= '' and path:sub(1, 1) ~= '/'
        and not ('/' .. path .. '/'):find('/%.%./')
end

---@param list any
---@param field string
---@return string? error
local function checkList(list, field)
    if type(list) ~= 'table' then
        return field .. ' must be a list'
    end
    for i, path in ipairs(list) do
        if not isRelativePath(path) then
            return ('%s[%d] is not a relative path: %s'):format(field, i, tostring(path))
        end
    end
    return nil
end

---Downloads and loads the manifest. It runs in an empty environment: data only, no access to OS.
---@param base string
---@return InstallManifest? manifest
---@return string? error
local function loadManifest(base)
    if not download(base .. 'manifest.lua', kTmp) then
        return nil, 'no manifest.lua (unknown project or branch, or no access to the repository?)'
    end
    local text = readAll(kTmp)
    filesystem.remove(kTmp)
    local chunk, err = load(text or '', '=manifest.lua', 't', {})
    if not chunk then
        return nil, err
    end
    local ok, manifest = pcall(chunk)
    if not ok then
        return nil, manifest
    end
    if type(manifest) ~= 'table' then
        return nil, 'manifest.lua must return a table'
    end
    manifest.config = manifest.config or {}
    local listErr = checkList(manifest.code, 'code') or checkList(manifest.config, 'config')
    if listErr then
        return nil, listErr
    end
    return manifest
end

local function main(...)
    local args, opts = shell.parse(...)
    local project = args[1]
    if not project then
        print('Usage: gtnh-install <project> [dir] [--config] [--branch=<name>]')
        return 1
    end
    if not project:match('^[%w_%-]+$') then
        print('Bad project name: ' .. project)
        return 1
    end
    if not component.isAvailable('internet') then
        print('An Internet Card is required.')
        return 1
    end

    local token = readAll(kTokenFile)
    token = token and token:match('^%s*(%S+)')
    if token then
        headers = { Authorization = 'token ' .. token }
    end

    local dir = shell.resolve(args[2] or ('/home/' .. project))
    local base = kRepo .. (opts.branch or kDefaultBranch) .. '/' .. project .. '/'
    print(('Installing %s from %s into %s%s'):format(project, base, dir,
        token and ' (with token)' or ''))

    local manifest, err = loadManifest(base)
    if not manifest then
        print('Cannot read the manifest: ' .. tostring(err))
        return 1
    end

    local failed = 0
    local function fetch(file, overwrite)
        local target = filesystem.concat(dir, file)
        if not overwrite and filesystem.exists(target) then
            -- A silently kept config hides upstream changes; say when it differs.
            if download(base .. file, kTmp) and readAll(kTmp) ~= readAll(target) then
                print('  kept   ' .. file .. '  (DIFFERS from the repository, --config replaces it)')
            else
                print('  kept   ' .. file)
            end
            filesystem.remove(kTmp)
            return
        end
        print('  get    ' .. file)
        if not download(base .. file, target) then
            failed = failed + 1
        end
    end

    for _, file in ipairs(manifest.code) do
        fetch(file, true)
    end
    for _, file in ipairs(manifest.config) do
        fetch(file, opts.config)
    end

    if failed > 0 then
        print(('%d file(s) failed. Check the Internet Card, that the server allows HTTP'
            .. ' and the token in ' .. kTokenFile .. '.'):format(failed))
        return 1
    end
    if manifest.run then
        print(('Done. Run: cd %s && %s'):format(dir, manifest.run))
    else
        print('Done. Installed into ' .. dir)
    end
    return 0
end

return main(...)
