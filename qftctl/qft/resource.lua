---A resource is what the config names: an item or a fluid in an ME network, found by label.
---Label is the key (unique enough in practice); name/damage only refine an ambiguous label.
---Pure logic: snapshots come in as plain tables, so everything is testable locally.
local resource = {}

---@alias qft.ResourceKind 'item' | 'fluid'

---@class qft.Resource
---@field kind qft.ResourceKind
---@field label string # What the game shows; the key.
---@field name string? # Internal id, set only to resolve an ambiguous label.
---@field damage integer? # Items only: metadata, set only to resolve an ambiguous label.

---@class qft.Stack
---@field name string
---@field damage integer # 0 for fluids.
---@field amount number # Items: count (`size`); fluids: mB (`amount`).
---@field hasTag boolean

---@alias qft.Snapshot { item: table<string, qft.Stack[]>?, fluid: table<string, qft.Stack[]>? }

-- AE2FC lists stored fluids as items too ("fluid drops"); their fluid type lives in NBT, so
-- every drop looks like an NBT-only duplicate. Fluids are read with getFluidsInNetwork.
local kIgnoredItems = { ['ae2fc:fluid_drop'] = true }

---Reads a config entry: `item = <label>` or `fluid = <label>`, plus optional name/damage.
---@param spec table
---@return qft.Resource? res
---@return string? err
function resource.fromSpec(spec)
    if spec.item ~= nil and spec.fluid ~= nil then
        return nil, 'either item or fluid, not both'
    end
    local kind = (spec.item ~= nil and 'item') or (spec.fluid ~= nil and 'fluid') or nil
    if not kind then
        return nil, 'item or fluid label is required'
    end
    local label = spec[kind]
    if type(label) ~= 'string' or label == '' then
        return nil, kind .. ' must be a label'
    end
    if spec.name ~= nil and type(spec.name) ~= 'string' then
        return nil, 'name must be a string'
    end
    if spec.damage ~= nil then
        if kind ~= 'item' then
            return nil, 'damage is for items only'
        end
        if math.type(spec.damage) ~= 'integer' then
            return nil, 'damage must be an integer'
        end
    end
    return { kind = kind, label = label, name = spec.name, damage = spec.damage }
end

---@param res qft.Resource
---@return string
function resource.describe(res)
    return ('%s "%s"'):format(res.kind, res.label)
end

local function add(byLabel, label, stack)
    local list = byLabel[label]
    if not list then
        list = {}
        byLabel[label] = list
    end
    list[#list + 1] = stack
end

---Builds a snapshot from getItemsInNetwork() / getFluidsInNetwork() results.
---A kind that was not read stays nil, and asking for it is an error, not a silent 0.
---@param items table[]?
---@param fluids table[]?
---@return qft.Snapshot
function resource.snapshot(items, fluids)
    local snap = {}
    if items then
        snap.item = {}
        for _, s in ipairs(items) do
            if not kIgnoredItems[s.name] then
                add(snap.item, s.label or '?', {
                    name = s.name, damage = s.damage or 0, amount = s.size or 0,
                    hasTag = s.hasTag == true,
                })
            end
        end
    end
    if fluids then
        snap.fluid = {}
        for _, f in ipairs(fluids) do
            add(snap.fluid, f.label or '?', {
                name = f.name, damage = 0, amount = f.amount or 0, hasTag = f.hasTag == true,
            })
        end
    end
    return snap
end

local function identity(kind, stack)
    return kind == 'item' and ('%s:%d'):format(stack.name, stack.damage) or stack.name
end

---Amount of a resource in a snapshot. Absent means 0: AE does not list empty stacks.
---Ambiguous (different items under one label, or items told apart by NBT only) is an error:
---summing different things would be worse than not knowing.
---@param snap qft.Snapshot
---@param res qft.Resource
---@return number? amount # nil on error.
---@return string? err
---@return boolean? absent # true when nothing matched (amount is 0).
function resource.amount(snap, res)
    local byLabel = snap[res.kind]
    if not byLabel then
        return nil, res.kind .. 's of this network were not read'
    end
    local total, ids, seen, count = 0, {}, {}, 0
    for _, stack in ipairs(byLabel[res.label] or {}) do
        if (res.name == nil or stack.name == res.name)
                and (res.damage == nil or stack.damage == res.damage) then
            local id = identity(res.kind, stack)
            if not seen[id] then
                seen[id] = true
                ids[#ids + 1] = id
            end
            count = count + 1
            total = total + stack.amount
        end
    end
    if count == 0 then
        return 0, nil, true
    end
    if #ids > 1 then
        table.sort(ids)
        return nil, ('%s matches %s; add name/damage'):format(resource.describe(res),
            table.concat(ids, ', '))
    end
    if res.kind == 'item' and count > 1 then
        return nil, ('%s: %d stacks of %s differ only by NBT'):format(resource.describe(res),
            count, ids[1])
    end
    return total
end

return resource
