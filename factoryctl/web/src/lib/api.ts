// The factoryctl server's JSON API. Types by hand, matching server/src/api/*.

export type StockItem = {
  goodsId: number
  name: string // mod:internal, e.g. gregtech:gt.metaitem.01
  damage: number
  variant: string // a fluid drop's fluid, e.g. molten.platinum; '' for other items
  label: string
  amount: number // pieces; mB for a fluid drop
}

async function get<T>(path: string): Promise<T> {
  const res = await fetch(path)
  if (!res.ok) {
    throw new Error(`${path}: HTTP ${res.status}`)
  }
  return (await res.json()) as T
}

export async function fetchStock(query: string, limit = 200): Promise<StockItem[]> {
  const params = new URLSearchParams({ q: query, limit: String(limit) })
  const body = await get<{ items: StockItem[] }>(`/api/v1/stock?${params}`)
  return body.items
}
