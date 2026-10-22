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
    kBunledOutSide = sides.down,
    kBundledOff = 0,
    kBundledOn = 255,
    Colors = {
        kNone = '\27[0m',
        kGreen = '\27[0;32m',
        kYellow = '\27[1;33m',
        kCyan = '\27[0;36m',
    }
}

local function log_debug(...)
    if const.kDebug then
        print(...)
    end
end

-- The order of this list is the order of the dashboard rows
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

local fluids_by_label = {}
for _, fluid in ipairs(fluids) do
    fluid.amount = 0
    fluids_by_label[fluid.label] = fluid
end

local me = component.me_controller
local rs = component.redstone

local function fill_ratio(fluid)
    return fluid.amount / fluid.target
end

local function is_low(fluid)
    return fill_ratio(fluid) < const.kThreshold
end

local function urgency(fluid)
    return 1 - fill_ratio(fluid) ^ fluid.priority
end

local function emptiness(fluid)
    return 1 - fill_ratio(fluid)
end

-- Returns the fluid with the highest rate among those passing the filter (all fluids if there is no filter)
local function select_best(rate, filter)
    local best, best_rate
    for _, fluid in ipairs(fluids) do
        if filter == nil or filter(fluid) then
            local fluid_rate = rate(fluid)
            log_debug(fluid.label, fluid.amount, fluid_rate)
            if best == nil or fluid_rate > best_rate then
                best, best_rate = fluid, fluid_rate
            end
        end
    end
    return best
end

local function reload_fluids()
    log_debug('reload_fluids')
    for _, fluid in ipairs(fluids) do fluid.amount = 0 end

    for _, stack in ipairs(me.getFluidsInNetwork()) do
        local fluid = fluids_by_label[stack.label]
        if fluid then
            fluid.amount = stack.amount
        end
    end
end

local function set_redstone(color)
    -- with the side only it returns all colors of the side, the stubs miss this overload
    local outputs = rs.getBundledOutput(const.kBunledOutSide) --[[@as table<integer, integer>]]

    -- turn the others off first, so two fluids are never enabled at once
    for clr, value in pairs(outputs) do
        if clr ~= color and value ~= const.kBundledOff then
            rs.setBundledOutput(const.kBunledOutSide, clr, const.kBundledOff)
        end
    end
    if outputs[color] ~= const.kBundledOn then
        rs.setBundledOutput(const.kBunledOutSide, color, const.kBundledOn)
    end

    log_debug('Selected color: ', colors[color])
end

local function format_amount(amount)
    local kSuffixes = { '', 'k', 'M', 'G', 'T', 'P', 'E' }
    local i = 1
    while amount >= 1000 and i < #kSuffixes do
        amount = amount / 1000
        i = i + 1
    end
    return string.format('%.1f%s', amount, kSuffixes[i])
end

local function pad_left(str, width)
    return string.rep(' ', width - unicode.len(str)) .. str
end

local function pad_right(str, width)
    return str .. string.rep(' ', width - unicode.len(str))
end

local function make_bar(ratio, width, color)
    local filled = math.min(width, math.ceil(width * ratio))
    return color .. string.rep('■', filled) .. string.rep('□', width - filled) .. const.Colors.kNone
end

local function print_dashboard(active_fluid)
    local _, _, screen_width = term.getGlobalArea()

    local kPercentWidth = 7
    local kAmountWidth = 7
    local label_width = unicode.len('Fluid')
    for _, fluid in ipairs(fluids) do
        label_width = math.max(label_width, unicode.len(fluid.label))
    end
    -- a row is '│' plus ' <cell> │' per column, the bar takes the rest of the screen
    local bar_width = math.max(5, screen_width - 16 - label_width - kPercentWidth - 2 * kAmountWidth)
    local widths = { label_width, bar_width, kPercentWidth, kAmountWidth, kAmountWidth }

    local function rule(left, middle, right)
        local parts = {}
        for i, width in ipairs(widths) do
            parts[i] = string.rep('─', width + 2)
        end
        return left .. table.concat(parts, middle) .. right
    end

    local function row(cells)
        return '│ ' .. table.concat(cells, ' │ ') .. ' │'
    end

    local lines = {
        rule('┌', '┬', '┐'),
        row({
            pad_right('Fluid', label_width),
            pad_right('Stock', bar_width),
            pad_left('%', kPercentWidth),
            pad_left('Amount', kAmountWidth),
            pad_left('Target', kAmountWidth),
        }),
        rule('├', '┼', '┤'),
    }

    local low_count = 0
    for _, fluid in ipairs(fluids) do
        local color = const.Colors.kGreen
        if fluid == active_fluid then
            color = const.Colors.kYellow
        elseif is_low(fluid) then
            color = const.Colors.kCyan
        end
        if is_low(fluid) then
            low_count = low_count + 1
        end

        local ratio = fill_ratio(fluid)
        lines[#lines + 1] = row({
            pad_right(fluid.label, label_width),
            make_bar(ratio, bar_width, color),
            pad_left(string.format('%.1f%%', ratio * 100), kPercentWidth),
            pad_left(format_amount(fluid.amount), kAmountWidth),
            pad_left(format_amount(fluid.target), kAmountWidth),
        })
    end

    lines[#lines + 1] = rule('└', '┴', '┘')
    lines[#lines + 1] = string.format(' Producing: %s%s%s   Low: %d/%d   Amounts in mB',
        const.Colors.kYellow, active_fluid.label, const.Colors.kNone, low_count, #fluids)

    term.clear()
    for y, line in ipairs(lines) do
        term.setCursor(1, y)
        io.write(line)
    end
    term.setCursor(1, #lines + 1)
end

local function update()
    reload_fluids()

    -- produce the most urgent low fluid, or the least stocked one when nothing is low
    local fluid = select_best(urgency, is_low) or select_best(emptiness)
    set_redstone(fluid.color)

    if not const.kDebug then
        print_dashboard(fluid)
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
