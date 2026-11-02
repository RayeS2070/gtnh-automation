// Item icons from the GTNH Calculator's export (the assets submodule, served at /data/).
// atlas.webp is a grid of 32 px icons, 256 to a row; data.bin numbers the icon of each item
// and fluid. data.bin is read the way the calculator reads it (assets/src/repository.ts),
// keeping only the icon numbers.

import { plainLabel } from './format'

export const kIconSize = 32
export const kIconsPerRow = 256
export const kAtlasUrl = '/data/atlas.webp'

const kDataVersion = 7

type Goods = { name: string; damage: number; variant: string; label: string }

type Index = {
  items: Map<string, number> // mod:internal:damage
  fluids: Map<string, number> // the fluid's internal name, as a drop's variant
  labels: Map<string, number> // a label only one item has; -1 for a shared one
  all: Goods[] // every item (but the NBT variants) and fluid of the export, for searching
}

let index = $state<Index | null>(null)

// The icon number; undefined while the data loads, null when the export has no such goods.
// The export is of another game version: some goods are missing or renamed.
export function iconOf(goods: Goods): number | null | undefined {
  if (!index) return undefined
  if (goods.variant !== '') return index.fluids.get(goods.variant) ?? null
  const label = index.labels.get(plainLabel(goods.label))
  return (
    index.items.get(`${goods.name}:${goods.damage}`) ??
    // dreamcraft:item.QuantumCrystal in the game is dreamcraft:QuantumCrystal in the export.
    index.items.get(`${goods.name.replace(/:(item|tile)\./, ':')}:${goods.damage}`) ??
    (label !== undefined && label >= 0 ? label : null)
  )
}

function parse(data: ArrayBuffer): Index {
  const elements = new Int32Array(data)
  const bytes = new Uint8Array(data)
  const text = new TextDecoder()
  if (elements[0] !== kDataVersion) throw new Error(`data.bin version ${elements[0]}, not ${kDataVersion}`)

  // An object field holds a pointer; a string is its length in bytes, then UTF-8.
  const string = (object: number, field: number): string | null => {
    const at = elements[object + field]
    if (at === -1) return null
    return text.decode(bytes.subarray(at * 4 + 4, at * 4 + 4 + elements[at]))
  }
  // A slice: its length, then the elements.
  const slice = (at: number) => elements.subarray(at + 1, at + 1 + elements[at])
  const kMod = 6, kInternal = 7, kIcon = 9, kNbt = 12, kDamage = 16, kName = 5

  const items = new Map<string, number>()
  const labels = new Map<string, number>()
  const all: Goods[] = []
  const withNbt: number[] = []
  for (const item of slice(elements[1])) {
    const name = plainLabel(string(item, kName) ?? '')
    labels.set(name, labels.has(name) ? -1 : elements[item + kIcon])
    if (string(item, kNbt) !== null) {
      withNbt.push(item)
      continue
    }
    const mod = `${string(item, kMod)}:${string(item, kInternal)}`
    items.set(`${mod}:${elements[item + kDamage]}`, elements[item + kIcon])
    all.push({ name: mod, damage: elements[item + kDamage], variant: '', label: name })
  }
  // OC does not give NBT: an item that has variants only by NBT gets the first one's icon.
  for (const item of withNbt) {
    const key = `${string(item, kMod)}:${string(item, kInternal)}:${elements[item + kDamage]}`
    if (!items.has(key)) items.set(key, elements[item + kIcon])
  }

  const fluids = new Map<string, number>()
  for (const fluid of slice(elements[2])) {
    const name = string(fluid, kInternal) ?? ''
    if (!fluids.has(name)) {
      fluids.set(name, elements[fluid + kIcon])
      // As the stock keys a fluid (a drop's key, domain::keyOf on the server).
      all.push({ name: 'ae2fc:fluid_drop', damage: 0, variant: name, label: string(fluid, kName) ?? name })
    }
  }
  return { items, fluids, labels, all }
}

async function load() {
  try {
    const res = await fetch('/data/data.bin')
    if (!res.ok || !res.body) throw new Error(`HTTP ${res.status}`)
    index = parse(await new Response(res.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer())
  } catch (e) {
    console.warn('no item icons:', e)
    index = { items: new Map(), fluids: new Map(), labels: new Map(), all: [] }
  }
}
load()

// Items and fluids of the export whose label has every word, shortest labels first: any goods of
// the game, in the stock or not. Empty while the data loads.
export function searchGoods(text: string, limit: number): Goods[] {
  const words = text.toLowerCase().split(/\s+/).filter((w) => w !== '')
  if (!index || words.length === 0) return []
  const found: Goods[] = []
  for (const g of index.all) {
    const label = g.label.toLowerCase()
    if (words.every((w) => label.includes(w))) found.push(g)
  }
  found.sort((a, b) => a.label.length - b.label.length || a.label.localeCompare(b.label))
  return found.slice(0, limit)
}
