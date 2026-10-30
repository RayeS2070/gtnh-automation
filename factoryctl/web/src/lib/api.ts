// The factoryctl server's JSON API. Types by hand, matching server/src/api/*.

export type StockItem = {
  goodsId: number
  name: string // mod:internal, e.g. gregtech:gt.metaitem.01
  damage: number
  variant: string // a fluid drop's fluid, e.g. molten.platinum; '' for other items
  label: string
  amount: number // pieces; mB for a fluid drop
}

export type Priority = 'high' | 'low'

export type TargetSettings = {
  threshold: number // 0: order whenever nothing is on order
  batch: number
  priority: Priority
  enabled: boolean
  note: string
}

export type Target = StockItem & TargetSettings

async function call<T>(method: string, path: string, body?: unknown): Promise<T> {
  const res = await fetch(path, {
    method,
    headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  })
  const text = await res.text()
  let data: unknown = undefined
  try {
    data = text ? JSON.parse(text) : undefined
  } catch {
    // not JSON: an error page of the server
  }
  if (!res.ok) {
    const message = (data as { error?: string } | undefined)?.error ?? text
    throw new Error(message || `HTTP ${res.status}`)
  }
  return data as T
}

export async function fetchStock(query: string, limit = 200): Promise<StockItem[]> {
  const params = new URLSearchParams({ q: query, limit: String(limit) })
  return (await call<{ items: StockItem[] }>('GET', `/api/v1/stock?${params}`)).items
}

export async function fetchTargets(): Promise<Target[]> {
  return (await call<{ items: Target[] }>('GET', '/api/v1/targets')).items
}

export async function putTarget(goodsId: number, settings: TargetSettings): Promise<void> {
  await call('PUT', `/api/v1/targets/${goodsId}`, settings)
}

export async function deleteTarget(goodsId: number): Promise<void> {
  await call('DELETE', `/api/v1/targets/${goodsId}`)
}
