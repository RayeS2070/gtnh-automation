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
  group: number | null // a TargetGroup's id; groups are for people, they change no order
}

export type Target = StockItem & TargetSettings

export type TargetGroup = {
  id: number
  name: string
  targets: number // how many are in it
}

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

// Everything that matches: the whole network is some 5000 rows.
export async function fetchStock(query: StockQuery): Promise<StockItem[]> {
  const params = new URLSearchParams({
    q: query.text,
    kind: query.kind,
    sort: query.sort,
    dir: query.dir,
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

export async function fetchGroups(): Promise<TargetGroup[]> {
  return (await call<{ items: TargetGroup[] }>('GET', '/api/v1/target-groups')).items
}

export async function addGroup(name: string): Promise<number> {
  return (await call<{ id: number }>('POST', '/api/v1/target-groups', { name })).id
}

export async function renameGroup(id: number, name: string): Promise<void> {
  await call('PUT', `/api/v1/target-groups/${id}`, { name })
}

// Its targets stay, ungrouped.
export async function deleteGroup(id: number): Promise<void> {
  await call('DELETE', `/api/v1/target-groups/${id}`)
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

// A crafting CPU as the agent saw it in its last poll.
export type Cpu = {
  name: string
  busy: boolean
  goods: StockItem | null // what it crafts; null when idle, or for goods no scan has seen yet
  remaining: number // left to deliver
  seenAt: string // UTC, ISO 8601: the poll that reported it
  order: number | null // our open order of those goods; null for a craft put on in game
}

export const isIdle = (c: Cpu) => !c.busy && c.goods === null

// The person the proxy in front signed in (nginx auth_basic); null without it (an SSH tunnel).
export async function fetchMe(): Promise<string | null> {
  return (await call<{ user: string | null }>('GET', '/api/v1/me')).user
}

export async function fetchCpus(): Promise<{ items: Cpu[]; lowFreeCpus: number }> {
  return await call('GET', '/api/v1/cpus')
}

export type EventLevel = 'debug' | 'info' | 'warning' | 'error'

// Something that happened, for people: the agent's lines, order history, alarms, scans, targets.
export type FactoryEvent = {
  id: number
  at: string // UTC, ISO 8601
  level: EventLevel
  source: string // agent | server | order | scan | target
  text: string
  order: number | null
  goods: { goodsId: number; name: string; damage: number; variant: string; label: string } | null
  agentUptime: number | null // the agent's computer.uptime(), its own lines
}

export type EventQuery = {
  level?: EventLevel // this level and above
  source?: string
  text?: string
  order?: number
  before?: number // ids below it: older events
  limit?: number
}

// Newest first.
export async function fetchEvents(q: EventQuery = {}): Promise<FactoryEvent[]> {
  const params = new URLSearchParams()
  if (q.level) params.set('level', q.level)
  if (q.source) params.set('source', q.source)
  if (q.text) params.set('q', q.text)
  if (q.order !== undefined) params.set('order', String(q.order))
  if (q.before !== undefined) params.set('before', String(q.before))
  if (q.limit !== undefined) params.set('limit', String(q.limit))
  return (await call<{ items: FactoryEvent[] }>('GET', `/api/v1/events?${params}`)).items
}

// The pumping agent (factoryctl/pumps): it decides; the server keeps its settings and reports.
export type PumpSettings = { version: number; batch: number; threshold: number }

export type PumpModule = {
  address: string
  tier: number
  state: string // idle | pulse | starting | running | backoff
  progress: number | null // ticks of the cycle, running only
  maxProgress: number | null
  seenAt: string
  slots: { fluid: string; parallel: number }[]
}

export type PumpTarget = { target: number; priority: number; enabled: boolean }

export type PumpFluid = {
  fluid: string // internal name, as a drop's variant
  label: string
  planet: number
  gas: number
  rate: number // mB per second of a cycle, one parallel
  goodsId: number | null
  amount: number | null // the pumping network, last report
  inFlight: number // what running cycles bring
  produced: number // done cycles, last 24 h
  target: PumpTarget | null
}

export type Pumps = {
  settings: PumpSettings | null
  failed: number // cycles, last 24 h
  modules: PumpModule[]
  fluids: PumpFluid[]
}

export async function fetchPumps(): Promise<Pumps> {
  return call<Pumps>('GET', '/api/v1/pumps')
}

export async function putPumpSettings(batch: number, threshold: number): Promise<void> {
  await call('PUT', '/api/v1/pumps/settings', { batch, threshold })
}

export async function putPumpTarget(fluid: string, target: PumpTarget): Promise<void> {
  await call('PUT', `/api/v1/pumps/targets/${encodeURIComponent(fluid)}`, target)
}

export async function deletePumpTarget(fluid: string): Promise<void> {
  await call('DELETE', `/api/v1/pumps/targets/${encodeURIComponent(fluid)}`)
}

// QFT setups (factoryctl/qft): one Quantum Force Transformer and its agent each.
export type QftMode = 'auto' | 'deficit' | 'surplus'

export type QftOutputSettings = { circuit: number; target?: number; priority?: number; weight?: number }
export type QftSettings = {
  version: number
  mode: QftMode
  recipes: { id: string; enabled?: boolean; outputs: QftOutputSettings[] }[] // enabled: absent = true
}

export type QftCatalogRecipe = {
  id: string
  name: string
  trigger: 'deficit' | 'surplus'
  tier: number
  catalysts: number
  inputs: { kind: string; label: string; amount?: number }[]
  outputs: { circuit: number; kind: 'item' | 'fluid'; label: string }[]
}

export type QftRecipeState = {
  id: string
  trigger: string
  low: boolean
  done: boolean
  disabled: boolean
  urgency: number
  cycles?: number
  outputs: { circuit: number; amount: number; target: number }[]
}

export type QftSetup = {
  name: string
  coords: { x: number; y: number; z: number } | null
  seenAt: string
  catalog: QftCatalogRecipe[]
  status: {
    state: {
      phase?: string
      mode?: QftMode
      want?: string
      recipe?: string
      fermium?: boolean
      neptunium?: number
      cycleSeconds?: number
    } | null
    recipes: QftRecipeState[]
    plasma: { neptunium?: number; fermium?: number } | null
  }
  settings: QftSettings | null
}

export async function fetchQft(): Promise<QftSetup[]> {
  return (await call<{ items: QftSetup[] }>('GET', '/api/v1/qft')).items
}

export async function putQftSettings(name: string, settings: Omit<QftSettings, 'version'>): Promise<void> {
  await call('PUT', `/api/v1/qft/${encodeURIComponent(name)}/settings`, settings)
}

export async function forgetQft(name: string): Promise<void> {
  await call('DELETE', `/api/v1/qft/${encodeURIComponent(name)}`)
}


// --- Mining (Space Elevator mining modules: the server plans, the agents carry it out) ---------

export type MiningOre = { name: string; damage: number; label: string }

export type MiningTarget = StockItem & {
  threshold: number
  priority: number // > 0: urgency = 1 - fill^priority
  enabled: boolean
  ores: MiningOre[] // the module outputs it is made of
}

export type MiningSetting = { droneTier: number; distance: number; plasmaTier: number; parallel: number }

export type MiningModuleStatus = {
  x: number
  y: number
  z: number
  tier: number
  state: string // idle | stopping | loading | starting | running | held | unloading | failed
  active: boolean
  drone?: number // the drone tier in its bus
  assignment?: string // what it is set up for: the id of a setting
  error?: string
}

export type MiningPlanEntry = {
  module: string // "x,y,z"
  tier: number
  setting?: MiningSetting // none: idle
  value?: number
  rates: (MiningOre & { rate: number })[] // wanted outputs, per second
}

export type MiningSetup = {
  name: string
  run: string
  seenAt: string
  status: {
    modules?: MiningModuleStatus[]
    pool?: { tier: number; count: number }[]
    buffer?: { name: string; damage: number; size: number }[]
    slots?: number
  }
  plan: MiningPlanEntry[]
}

export type Mining = {
  setups: MiningSetup[]
  targets: MiningTarget[]
  plasma: { tier: number; label: string } | null
  bufferLimit: number
}

// A module output: the asteroids that drop it, what processing makes of it (not chemistry).
export type MiningOreInfo = MiningOre & { asteroids: string[]; products: { name: string; damage: number }[] }

export async function fetchMining(): Promise<Mining> {
  return await call('GET', '/api/v1/mining')
}

export async function putMiningTarget(
  goodsId: number,
  t: { threshold: number; priority: number; enabled: boolean; ores: { name: string; damage: number }[] },
): Promise<void> {
  await call('PUT', `/api/v1/mining/targets/${goodsId}`, t)
}

export async function deleteMiningTarget(goodsId: number): Promise<void> {
  await call('DELETE', `/api/v1/mining/targets/${goodsId}`)
}

export async function fetchMiningOres(): Promise<MiningOreInfo[]> {
  return await call('GET', '/api/v1/mining/ores')
}

// A target on any goods, in the stock or not.
export async function postMiningTarget(t: {
  product: { name: string; damage: number; variant: string; label: string }
  threshold: number
  priority: number
  enabled: boolean
  ores: { name: string; damage: number }[]
}): Promise<number> {
  return (await call<{ goodsId: number }>('POST', '/api/v1/mining/targets', t)).goodsId
}

export async function forgetMiningSetup(name: string): Promise<void> {
  await call('DELETE', `/api/v1/mining/setups/${encodeURIComponent(name)}`)
}
