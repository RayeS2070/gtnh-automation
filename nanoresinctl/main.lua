local colors = require('colors')
local component = require('component')
local event = require('event')
local sides = require('sides')
local term = require('term')
local unicode = require('unicode')

local const = {
    kDebug = false,
    kDuration = 600,
    kThreshold = 0.8,
    Event = {
        kInterrupted = 'interrupted'
    },
    kBundledOutSide = sides.down,
    kBundledOff = 0,
    kBundledOn = 255,
    Colors = {
        kNone = '\27[0m',
        kGreen = '\27[0;32m',
        kYellow = '\27[1;33m',
        kCyan = '\27[0;36m',
    }
}

local function logDebug(...)
    if const.kDebug then
        print(...)
    end
end

---@class nanoresin.Fluid
---@field label string # Exactly as getFluidsInNetwork reports it.
---@field target number # mB to keep in stock.
---@field priority number # Urgency exponent: higher = more urgent at the same fill level.
---@field color integer # Bundled redstone color that turns its production on.
---@field amount number? # mB in the network, set by the program on every check.

-- The order of this list is the order of the dashboard rows
---@type nanoresin.Fluid[]
local fluids = {
    { label = 'Molten Samarium', target = 1e10, priority = 1, color = colors.white },
    { label = 'Molten Lanthanum', target = 1e10, priority = 2, color = colors.orange },
    { label = 'Molten Cerium', target = 1e10, priority = 1, color = colors.magenta },
    { label = 'Molten Praseodymium', target = 1e10, priority = 2, color = colors.lightblue },
    { label = 'Molten Lutetium', target = 1e10, priority = 1, color = colors.yellow },
    { label = 'Molten Holmium', target = 1e10, priority = 1, color = colors.lime },
    { label = 'Molten Dysprosium', target = 1e10, priority = 1, color = colors.pink },
    { label = 'Molten Gadolinium', target = 1e10, priority = 1, color = colors.gray },
    { label = 'Molten Europium', target = 1e10, priority = 2, color = colors.silver },
}

---@type table<string, nanoresin.Fluid>
local fluidsByLabel = {}
for _, fluid in ipairs(fluids) do
    fluid.amount = 0
    fluidsByLabel[fluid.label] = fluid
end

local me = component.me_controller
local rs = component.redstone

---@param fluid nanoresin.Fluid
---@return number
local function fillRatio(fluid)
    return fluid.amount / fluid.target
end

---@param fluid nanoresin.Fluid
---@return boolean
local function isLow(fluid)
    return fillRatio(fluid) < const.kThreshold
end

---@param fluid nanoresin.Fluid
---@return number
local function urgency(fluid)
    return 1 - fillRatio(fluid) ^ fluid.priority
end

---@param fluid nanoresin.Fluid
---@return number
local function emptiness(fluid)
    return 1 - fillRatio(fluid)
end

---Returns the fluid with the highest rate among those passing the filter (all fluids if there is no filter).
---@param rate fun(fluid: nanoresin.Fluid): number
---@param filter (fun(fluid: nanoresin.Fluid): boolean)?
---@return nanoresin.Fluid?
local function selectBest(rate, filter)
    local best, bestRate
    for _, fluid in ipairs(fluids) do
        if filter == nil or filter(fluid) then
            local fluidRate = rate(fluid)
            logDebug(fluid.label, fluid.amount, fluidRate)
            if best == nil or fluidRate > bestRate then
                best, bestRate = fluid, fluidRate
            end
        end
    end
    return best
end

local function reloadFluids()
    logDebug('reloadFluids')
    for _, fluid in ipairs(fluids) do
        fluid.amount = 0
    end

    for _, stack in ipairs(me.getFluidsInNetwork()) do
        local fluid = fluidsByLabel[stack.label]
        if fluid then
            fluid.amount = stack.amount
        end
    end
end

---@param color integer
local function setRedstone(color)
    -- with the side only it returns all colors of the side, the stubs miss this overload
    local outputs = rs.getBundledOutput(const.kBundledOutSide) --[[@as table<integer, integer>]]

    -- turn the others off first, so two fluids are never enabled at once
    for clr, value in pairs(outputs) do
        if clr ~= color and value ~= const.kBundledOff then
            rs.setBundledOutput(const.kBundledOutSide, clr, const.kBundledOff)
        end
    end
    if outputs[color] ~= const.kBundledOn then
        rs.setBundledOutput(const.kBundledOutSide, color, const.kBundledOn)
    end

    logDebug('Selected color: ', colors[color])
end

---@param amount number
---@return string
local function formatAmount(amount)
    local kSuffixes = { '', 'k', 'M', 'G', 'T', 'P', 'E' }
    local i = 1
    while amount >= 1000 and i < #kSuffixes do
        amount = amount / 1000
        i = i + 1
    end
    return ('%.1f%s'):format(amount, kSuffixes[i])
end

---@param str string
---@param width integer
---@return string
local function padLeft(str, width)
    return (' '):rep(width - unicode.len(str)) .. str
end

---@param str string
---@param width integer
---@return string
local function padRight(str, width)
    return str .. (' '):rep(width - unicode.len(str))
end

---@param ratio number
---@param width integer
---@param color string # ANSI color sequence.
---@return string
local function makeBar(ratio, width, color)
    local filled = math.min(width, math.ceil(width * ratio))
    return color .. ('■'):rep(filled) .. ('□'):rep(width - filled) .. const.Colors.kNone
end

---@param activeFluid nanoresin.Fluid
local function printDashboard(activeFluid)
    local _, _, screenWidth = term.getGlobalArea()

    local kPercentWidth = 7
    local kAmountWidth = 7
    local labelWidth = unicode.len('Fluid')
    for _, fluid in ipairs(fluids) do
        labelWidth = math.max(labelWidth, unicode.len(fluid.label))
    end
    -- a row is '│' plus ' <cell> │' per column, the bar takes the rest of the screen
    local barWidth = math.max(5, screenWidth - 16 - labelWidth - kPercentWidth - 2 * kAmountWidth)
    local widths = { labelWidth, barWidth, kPercentWidth, kAmountWidth, kAmountWidth }

    local function rule(left, middle, right)
        local parts = {}
        for i, width in ipairs(widths) do
            parts[i] = ('─'):rep(width + 2)
        end
        return left .. table.concat(parts, middle) .. right
    end

    local function row(cells)
        return '│ ' .. table.concat(cells, ' │ ') .. ' │'
    end

    local lines = {
        rule('┌', '┬', '┐'),
        row({
            padRight('Fluid', labelWidth),
            padRight('Stock', barWidth),
            padLeft('%', kPercentWidth),
            padLeft('Amount', kAmountWidth),
            padLeft('Target', kAmountWidth),
        }),
        rule('├', '┼', '┤'),
    }

    local lowCount = 0
    for _, fluid in ipairs(fluids) do
        local color = const.Colors.kGreen
        if fluid == activeFluid then
            color = const.Colors.kYellow
        elseif isLow(fluid) then
            color = const.Colors.kCyan
        end
        if isLow(fluid) then
            lowCount = lowCount + 1
        end

        local ratio = fillRatio(fluid)
        lines[#lines + 1] = row({
            padRight(fluid.label, labelWidth),
            makeBar(ratio, barWidth, color),
            padLeft(('%.1f%%'):format(ratio * 100), kPercentWidth),
            padLeft(formatAmount(fluid.amount), kAmountWidth),
            padLeft(formatAmount(fluid.target), kAmountWidth),
        })
    end

    lines[#lines + 1] = rule('└', '┴', '┘')
    lines[#lines + 1] = (' Producing: %s%s%s   Low: %d/%d   Amounts in mB'):format(
        const.Colors.kYellow, activeFluid.label, const.Colors.kNone, lowCount, #fluids)

    term.clear()
    for y, line in ipairs(lines) do
        term.setCursor(1, y)
        io.write(line)
    end
    term.setCursor(1, #lines + 1)
end

local function update()
    reloadFluids()

    -- produce the most urgent low fluid, or the least stocked one when nothing is low
    local fluid = selectBest(urgency, isLow) or selectBest(emptiness)
    ---@cast fluid nanoresin.Fluid
    setRedstone(fluid.color)

    if not const.kDebug then
        printDashboard(fluid)
    end
end

local function main()
    assert(#fluids > 0, 'fluid list is empty')

    repeat
        local ok, err = xpcall(update, debug.traceback)
        if not ok then
            print(err)
        end
    until event.pull(const.kDuration, const.Event.kInterrupted)
end

main()
