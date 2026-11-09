---The black hole agent and the factoryctl server. The agent decides alone, second by second; the
---server keeps its settings and shows what it reports. It talks to the server only while the hole
---is closed (idle): with an open hole a wait for the network would miss pulses.
---
---Like the QFT agent: settings carry a version; newer ones from the server are taken and saved, so
---a restart without the server keeps them; until the server has any, the agent offers its own
---(config.lua). Pure: HTTP and the file come in as deps.
local json = require('fc.json')

local link = {}
link.__index = link

link.kMaxHolds = 600 -- 600 holds: 2^20 L a second at the end, ~2.1e7 L in all
link.kMaxSessions = 20 -- summaries kept while the server does not answer

---@class bhc.LinkSettings
---@field version integer # The server's; 0: never had any from it.
---@field holdAt integer
---@field floor integer
---@field maxHolds integer
---@field minSpacetime integer

local function int(v)
    return type(v) == 'number' and math.tointeger(v) or nil
end

---Checks settings from the server or the saved file; the server checks the same. Errors name the
---field.
---@param raw any
---@return bhc.LinkSettings? settings
---@return string? err
function link.check(raw)
    if type(raw) ~= 'table' then
        return nil, 'not a table'
    end
    local s = { version = int(raw.version) or 0, holdAt = int(raw.holdAt), floor = int(raw.floor),
        maxHolds = int(raw.maxHolds), minSpacetime = int(raw.minSpacetime) }
    if not s.floor or s.floor < 1 or s.floor > 98 then
        return nil, 'floor: an integer from 1 to 98'
    end
    if not s.holdAt or s.holdAt <= s.floor or s.holdAt > 99 then
        return nil, 'holdAt: an integer above floor, up to 99'
    end
    if not s.maxHolds or s.maxHolds < 0 or s.maxHolds > link.kMaxHolds then
        return nil, ('maxHolds: an integer from 0 to %d'):format(link.kMaxHolds)
    end
    if not s.minSpacetime or s.minSpacetime < 0 then
        return nil, 'minSpacetime: an integer, 0 or more'
    end
    return s
end

---Where the server's settings are kept for restarts.
---@class bhc.SettingsStore
---@field read fun(): string?
---@field write fun(text: string): boolean?, string?
---@field encode fun(v: table): string
---@field decode fun(text: string): any

---@class bhc.LinkDeps
---@field post fun(path: string, body: string): string?, string?
---@field decode fun(text: string): any
---@field store bhc.SettingsStore
---@field offer bhc.LinkSettings # config.lua's: what a server without settings gets
---@field name string # The setup: the server's key.
---@field coords { x: number, y: number, z: number }? # Of the controller: the server checks them.
---@field run string # This run of the agent.
---@field clock fun(): number # computer.uptime: how long ago a session ended
---@field logs table? # fc.logbuf
---@field warn fun(fmt: string, ...)?
---@field info fun(fmt: string, ...)?

---@param deps bhc.LinkDeps
function link.new(deps)
    local l = setmetatable({ deps = deps, warn = deps.warn or function() end,
        info = deps.info or function() end, offerWanted = false, synced = false,
        sessions = {} }, link)
    l.settings = deps.offer
    local text = deps.store.read()
    if text then
        local ok, raw = pcall(deps.store.decode, text)
        local s, err = link.check(ok and raw or nil)
        if s then
            l.settings = s
            l.info('settings v%d (saved): %s', s.version, link.describe(s))
        else
            l.warn('saved settings refused (%s): config.lua until the server sends them',
                tostring(err))
        end
    end
    return l
end

---@param s bhc.LinkSettings
function link.describe(s)
    return ('hold at %d, floor %d, %d holds, %d L to open'):format(s.holdAt, s.floor, s.maxHolds,
        s.minSpacetime)
end

---Keeps a session summary for the next poll; the oldest go when the server is away long.
---@param summary bhc.Summary
function link:ended(summary)
    self.sessions[#self.sessions + 1] = { summary = summary, at = self.deps.clock() }
    if #self.sessions > link.kMaxSessions then
        table.remove(self.sessions, 1)
    end
end

---@class bhc.Report
---@field state table # phase, session, stability, holds, spent, blocked, low, busy, work
---@field supplies { seeds: integer, collapsers: integer, spacetime: integer }?

---One poll: reports, takes newer settings. Returns the reply and whether the settings changed, or
---nil and why.
---@param report bhc.Report
---@return table? reply
---@return string|boolean|nil errOrChanged
function link:poll(report)
    local d = self.deps
    local sent = #self.sessions
    local sessions = {}
    local now = d.clock()
    for i = 1, sent do
        local out = {}
        for k, v in pairs(self.sessions[i].summary) do out[k] = v end
        out.ago = math.max(0, now - self.sessions[i].at)
        sessions[i] = out
    end
    local body = {
        setup = { name = d.name, coords = d.coords },
        run = d.run,
        -- -1 until a poll of this run got through: the server sends its settings anyway.
        settings = self.synced and self.settings.version or -1,
        state = report.state,
        supplies = report.supplies,
        sessions = sessions,
        log = d.logs and d.logs:entries() or {},
    }
    if self.settings.version == 0 or self.offerWanted then
        local o = self.deps.offer
        body.offer = { holdAt = o.holdAt, floor = o.floor, maxHolds = o.maxHolds,
            minSpacetime = o.minSpacetime }
    end
    local text, err = d.post('/agent/v1/bhc/poll', json.encode(body))
    local ok, reply = false, nil
    if text then
        ok, reply = pcall(d.decode, text)
    end
    if not text or not ok or type(reply) ~= 'table' or reply.ok == false then
        return nil, err or (ok and type(reply) == 'table' and reply.error) or text
    end
    if d.logs then
        d.logs:confirm()
    end
    for _ = 1, sent do table.remove(self.sessions, 1) end
    local fresh = not self.synced
    self.synced = true
    self.offerWanted = reply.offer == true
    local changed = false
    if reply.settings then
        local s, serr = link.check(reply.settings)
        if not s then
            self.warn('settings from the server refused: %s', tostring(serr))
        elseif s.version ~= self.settings.version or fresh then
            changed = s.version ~= self.settings.version
            self.settings = s
            self.info('settings v%d (server): %s', s.version, link.describe(s))
            local saved, werr = d.store.write(d.store.encode(s))
            if not saved then
                self.warn('settings not saved: %s', tostring(werr))
            end
        end
    end
    return reply, changed
end

return link
