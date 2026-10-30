---The agent's log lines waiting for the server: they go with every poll and are dropped only once
---a poll got through. A ring buffer: memory is bounded, the oldest lines go first, and one line
---says how many went.
local logbuf = {}
logbuf.__index = logbuf

---@param o { capacity: integer?, maxLength: integer? } # lines kept; characters per line
function logbuf.new(o)
    o = o or {}
    return setmetatable({
        capacity = o.capacity or 100,
        maxLength = o.maxLength or 300,
        lines = {},   -- { seq, t, level, text }, oldest first
        nextSeq = 1,
        dropped = 0,  -- lines pushed out since the last confirmed poll
        sentSeq = 0,  -- the newest line handed out by entries()
        sentDropped = 0,
    }, logbuf)
end

---Cuts by characters, not bytes: a cut inside a UTF-8 sequence (§ in labels is two bytes) would
---make the whole poll invalid JSON.
local function cut(text, max)
    local ok, at = pcall(utf8.offset, text, max + 1)
    if ok and at and at <= #text then
        return text:sub(1, at - 1) .. '…'
    end
    if not ok and #text > max then
        return text:sub(1, max) .. '…' -- not UTF-8: bytes it is
    end
    return text
end

---@param t number # computer.uptime()
---@param level string # 'dbg' | 'inf' | 'wrn' | 'err'
---@param text string
function logbuf:push(t, level, text)
    self.lines[#self.lines + 1] = { seq = self.nextSeq, t = t, level = level,
        text = cut(text, self.maxLength) }
    self.nextSeq = self.nextSeq + 1
    if #self.lines > self.capacity then
        local gone = table.remove(self.lines, 1)
        -- A line already handed to a poll is not lost: that poll may still get through.
        if gone.seq > self.sentSeq then
            self.dropped = self.dropped + 1
        end
    end
end

---The lines for a poll, oldest first, in the protocol's shape: {t, level, text}. Remembers what
---was handed out for confirm().
function logbuf:entries()
    local out = {}
    if self.dropped > 0 then
        out[1] = { t = self.lines[1] and self.lines[1].t or 0, level = 'wrn',
            text = ('agent log: dropped %d lines'):format(self.dropped) }
    end
    for _, l in ipairs(self.lines) do
        out[#out + 1] = { t = l.t, level = l.level, text = l.text }
    end
    self.sentSeq = self.nextSeq - 1
    self.sentDropped = self.dropped
    return out
end

---The poll with the last entries() got through: those lines are gone, newer ones stay.
function logbuf:confirm()
    local keep = {}
    for _, l in ipairs(self.lines) do
        if l.seq > self.sentSeq then
            keep[#keep + 1] = l
        end
    end
    self.lines = keep
    self.dropped = self.dropped - self.sentDropped
end

return logbuf
