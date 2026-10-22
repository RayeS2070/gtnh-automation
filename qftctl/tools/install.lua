---Installs or updates qftctl from GitHub with wget. Needs an Internet Card.
---  install [dir] [--config] [--branch=<name>]
---Code is always replaced; config/ files are only downloaded when missing, unless --config.
local component = require('component')
local filesystem = require('filesystem')
local shell = require('shell')

local kRepo = 'https://raw.githubusercontent.com/RayeS2070/gtnh-automation/'
local kProjectDir = 'qftctl/'
local kDefaultBranch = 'devel'
local kDefaultDir = '/home/qftctl'

-- Every file the program needs. A new module must be added here, or it will not be installed.
local kCode = {
    'main.lua',
    'qft/bind.lua',
    'qft/config.lua',
    'qft/log.lua',
    'qft/plan.lua',
    'qft/resource.lua',
    'qft/runner.lua',
    'qft/setup.lua',
    'qft/store.lua',
}
-- Edited by the player in game, so not overwritten by default.
local kConfig = {
    'config/ids.lua',
    'config/components.lua',
    'config/recipes.lua',
}

---@param url string
---@param target string
---@return boolean
local function download(url, target)
    local dir = filesystem.path(target)
    if not filesystem.exists(dir) then
        filesystem.makeDirectory(dir)
    end
    local ok, err = shell.execute(('wget -fq "%s" "%s"'):format(url, target))
    -- wget removes the file when the download fails, so its presence is the real result.
    if not ok or not filesystem.exists(target) then
        print(('  FAILED %s%s'):format(url, err and (': ' .. tostring(err)) or ''))
        return false
    end
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

local function main(...)
    local args, opts = shell.parse(...)
    if not component.isAvailable('internet') then
        print('An Internet Card is required.')
        return 1
    end

    local dir = shell.resolve(args[1] or kDefaultDir)
    local base = kRepo .. (opts.branch or kDefaultBranch) .. '/' .. kProjectDir
    print(('Installing qftctl from %s into %s'):format(base, dir))

    local failed = 0
    local function fetch(file, overwrite)
        local target = filesystem.concat(dir, file)
        if not overwrite and filesystem.exists(target) then
            -- A silently kept config hides upstream changes; say when it differs.
            local fresh = '/tmp/qftctl-install.lua'
            if download(base .. file, fresh) and readAll(fresh) ~= readAll(target) then
                print('  kept   ' .. file .. '  (DIFFERS from the repository, --config replaces it)')
            else
                print('  kept   ' .. file)
            end
            filesystem.remove(fresh)
            return
        end
        print('  get    ' .. file)
        if not download(base .. file, target) then
            failed = failed + 1
        end
    end

    for _, file in ipairs(kCode) do
        fetch(file, true)
    end
    for _, file in ipairs(kConfig) do
        fetch(file, opts.config)
    end

    if failed > 0 then
        print(('%d file(s) failed. Check the Internet Card and that the server allows HTTP.'):format(failed))
        return 1
    end
    print(('Done. Run: cd %s && main setup'):format(dir))
    return 0
end

return main(...)
