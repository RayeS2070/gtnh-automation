// Item icons from an NEI item panel dump of the game client, built by factoryctl/tools/icons.py
// and served at /data/. icons.webp is a grid of 32 px icons, 256 to a row; index.bin numbers
// them: the n-th item is icon n, a fluid has its icon beside its label.

import { plainLabel } from './format'

export const kIconSize = 32
export const kIconsPerRow = 256
export const kAtlasUrl = '/data/icons.webp'

const kIndexVersion = 1

type Goods = { name: string; damage: number; variant: string; label: string }

type Index = {
  items: Map<string, number> // name:damage
  labels: Map<string, number> // an item's label; -1 when items of different names share it
  fluids: Map<string, number> // a fluid's label (the dump has no fluid names); the first one
  all: Goods[] // every item, for searching
}

type IndexFile = {
  version: number
  size: number
  perRow: number
  items: [string, number, string][]
  fluids: [string, number][]
}

let index = $state<Index | null>(null)

const fluidLabel = (label: string) => plainLabel(label).replace(/^drop of /, '')

// The icon number; undefined while the index loads, null when the dump has no such goods.
export function iconOf(goods: Goods): number | null | undefined {
  if (!index) return undefined
  if (goods.variant !== '') return index.fluids.get(fluidLabel(goods.label)) ?? null
  const label = index.labels.get(plainLabel(goods.label))
  // A label helps an item whose damage the panel does not show: a charged battery, say.
  return index.items.get(`${goods.name}:${goods.damage}`) ?? (label !== undefined && label >= 0 ? label : null)
}

function parse(file: IndexFile): Index {
  if (file.version !== kIndexVersion) throw new Error(`index version ${file.version}, not ${kIndexVersion}`)
  if (file.size !== kIconSize || file.perRow !== kIconsPerRow) throw new Error('index of another atlas layout')

  const items = new Map<string, number>()
  const labels = new Map<string, number>()
  const owner = new Map<string, string>() // label -> the name that has it
  const all: Goods[] = []
  file.items.forEach(([name, damage, raw], icon) => {
    const label = plainLabel(raw)
    items.set(`${name}:${damage}`, icon)
    const had = owner.get(label)
    if (had === undefined) {
      owner.set(label, name)
      labels.set(label, icon)
    } else if (had !== name) {
      labels.set(label, -1)
    }
    all.push({ name, damage, variant: '', label })
  })
  const fluids = new Map<string, number>()
  for (const [label, icon] of file.fluids) {
    const key = fluidLabel(label)
    if (!fluids.has(key)) fluids.set(key, icon)
  }
  return { items, labels, fluids, all }
}

async function load() {
  try {
    const res = await fetch('/data/index.bin')
    if (!res.ok || !res.body) throw new Error(`HTTP ${res.status}`)
    const text = await new Response(res.body.pipeThrough(new DecompressionStream('gzip'))).text()
    index = parse(JSON.parse(text) as IndexFile)
  } catch (e) {
    console.warn('no item icons:', e)
    index = { items: new Map(), labels: new Map(), fluids: new Map(), all: [] }
  }
}
load()

// Items whose label has every word, shortest labels first: any item of the game, in the stock or
// not. Fluids are not offered: the dump has no fluid names. Empty while the index loads.
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
