// The factoryctl server's JSON API. Types by hand, matching server/src/api/*.

export type StockItem = {
  goodsId: number
  name: string // mod:internal, e.g. gregtech:gt.metaitem.01
  damage: number
  variant: string // a fluid drop's fluid, e.g. molten.platinum; '' for other items
  label: string
  amount: number // pieces; mB for a fluid drop
  craftable: boolean // AE has a pattern for it, when last seen (scan, probe or order)
}

export type Priority = 'high' | 'low'

export type TargetSettings = {
  threshold: number // 0: order whenever nothing is on order
  batch: number
  priority: Priority
  enabled: boolean
  note: string
  fill: boolean // order what is missing, in whole batches, instead of one batch
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

export type StockQuery = {
  text: string // words, all found in the label in any order
  kind: 'all' | 'items' | 'fluids'
  sort: 'label' | 'amount'
  dir: 'asc' | 'desc'
}

export async function fetchStock(query: StockQuery, limit = 200): Promise<StockItem[]> {
  const params = new URLSearchParams({
    q: query.text,
    kind: query.kind,
    sort: query.sort,
    dir: query.dir,
    limit: String(limit),
  })
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

export type OrderStatus =
  | 'queued'
  | 'sent'
  | 'computing'
  | 'accepted'
  | 'crafting'
  | 'done'
  | 'failed'
  | 'canceled'
  | 'lost'

export const openStatuses: OrderStatus[] = ['queued', 'sent', 'computing', 'accepted', 'crafting']

export type Order = {
  id: number
  goods: StockItem
  amount: number
  status: OrderStatus
  source: 'auto' | 'manual'
  requestedBy: string
  createdAt: string // UTC, ISO 8601
  changedAt: string
  message: string // why it failed
  cpu: string // the CPU that crafts it, once seen
  remaining: number // what the CPU has left to deliver
  cancelRequested: boolean
}

export async function fetchOrders(limit = 100): Promise<Order[]> {
  return (await call<{ items: Order[] }>('GET', `/api/v1/orders?limit=${limit}`)).items
}

// A one-off order, queued for the agent's next poll.
export async function placeOrder(goodsId: number, amount: number, requestedBy: string): Promise<number> {
  return (await call<{ id: number }>('POST', '/api/v1/orders', { goodsId, amount, requestedBy })).id
}

// 'canceled': it was queued; 'asked': the agent cancels the craft; 'nothing': closed already.
export async function cancelOrder(id: number): Promise<'canceled' | 'asked' | 'nothing'> {
  return (await call<{ result: 'canceled' | 'asked' | 'nothing' }>('POST', `/api/v1/orders/${id}/cancel`)).result
}
