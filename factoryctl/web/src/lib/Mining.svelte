<script lang="ts">
  import {
    deleteMiningTarget,
    fetchMining,
    fetchMiningOres,
    forgetMiningSetup,
    postMiningTarget,
    putMiningTarget,
    type Mining,
    type MiningModuleStatus,
    type MiningOreInfo,
    type MiningPlanEntry,
    type MiningTarget,
  } from './api'
  import { amount, compact, parseAmount, plainLabel, shownLabel } from './format'
  import Icon from './Icon.svelte'
  import { searchGoods } from './icons.svelte'
  import { every, kRefreshMs } from './refresh'

  const kDrones = ['LV', 'MV', 'HV', 'EV', 'IV', 'LuV', 'ZPM', 'UV', 'UHV', 'UEV', 'UIV', 'UMV', 'UXV', 'MAX']
  const kPlasmas = ['', 'Helium', 'Bismuth', 'Radon', 'Technetium', 'Plutonium 241']
  const kOffline = 60_000 // ms without a poll: the agent or its computer is away

  let mining = $state<Mining | null>(null)
  let error = $state('')

  async function load() {
    try {
      mining = await fetchMining()
      error = ''
    } catch (e) {
      error = String(e)
    }
  }
  load()
  $effect(() => every(kRefreshMs, load))

  async function act(fn: () => Promise<void>) {
    try {
      await fn()
      await load()
    } catch (e) {
      error = e instanceof Error ? e.message : String(e)
    }
  }

  // --- a target: any goods of the game (a dust below the scan threshold is not in the stock),
  // made of module outputs: the catalog suggests what processing turns into it, the rest is picked
  // by hand (chemistry is not followed).
  type Product = { name: string; damage: number; variant: string; label: string }
  type Form = {
    product: Product
    goodsId: number | null // null: a new target
    amount: number | null
    threshold: string
    priority: string
    enabled: boolean
    ores: Record<string, boolean> // "name:damage" -> taken
  }
  let form = $state<Form | null>(null)
  let search = $state('')
  let oreSearch = $state('')
  let ores = $state<MiningOreInfo[] | null>(null)

  const found = $derived(searchGoods(search, 12))
  const oreKey = (o: { name: string; damage: number }) => `${o.name}:${o.damage}`
  const byKey = $derived(new Map((ores ?? []).map((o) => [oreKey(o), o])))

  async function loadOres() {
    if (ores) return
    try {
      ores = await fetchMiningOres()
    } catch (e) {
      error = String(e)
    }
  }

  function open(product: Product, current?: MiningTarget) {
    form = {
      product,
      goodsId: current?.goodsId ?? null,
      amount: current?.amount ?? null,
      threshold: current ? String(current.threshold) : '',
      priority: String(current?.priority ?? 1),
      enabled: current?.enabled ?? true,
      ores: Object.fromEntries((current?.ores ?? []).map((o) => [oreKey(o), true])),
    }
    search = ''
    oreSearch = ''
    loadOres()
  }

  // The outputs that are the product or that processing turns into it.
  const suggested = $derived(
    form && ores && form.product.variant === ''
      ? ores.filter(
          (o) =>
            (o.name === form!.product.name && o.damage === form!.product.damage) ||
            o.products.some((p) => p.name === form!.product.name && p.damage === form!.product.damage),
        )
      : [],
  )
  const oreFound = $derived.by(() => {
    const words = oreSearch.toLowerCase().split(/\s+/).filter((w) => w !== '')
    if (!ores || words.length === 0) return []
    return ores.filter((o) => words.every((w) => o.label.toLowerCase().includes(w))).slice(0, 12)
  })
  // Taken ones stay listed whatever the search says.
  const listed = $derived.by(() => {
    if (!form) return []
    const out = new Map<string, MiningOreInfo>()
    for (const o of suggested) out.set(oreKey(o), o)
    for (const [k, on] of Object.entries(form.ores)) {
      const o = byKey.get(k)
      if (on && o) out.set(k, o)
    }
    for (const o of oreFound) out.set(oreKey(o), o)
    return [...out.values()]
  })

  const thresholdValue = $derived(form ? parseAmount(form.threshold) : null)
  const priorityValue = $derived(form ? Number(form.priority) : NaN)
  const taken = $derived(
    form ? Object.entries(form.ores).filter(([, on]) => on).map(([k]) => k) : [],
  )
  const formValid = $derived(
    thresholdValue !== null && thresholdValue > 0 && priorityValue > 0 && taken.length > 0,
  )

  function save(event: SubmitEvent) {
    event.preventDefault()
    const f = form
    if (!f || !formValid) return
    const chosen = taken.map((k) => {
      const at = k.lastIndexOf(':')
      return { name: k.slice(0, at), damage: Number(k.slice(at + 1)) }
    })
    const t = { threshold: thresholdValue!, priority: priorityValue, enabled: f.enabled, ores: chosen }
    act(async () => {
      if (f.goodsId === null) {
        await postMiningTarget({ product: f.product, ...t })
      } else {
        await putMiningTarget(f.goodsId, t)
      }
      form = null
    })
  }

  const fill = (t: MiningTarget) => Math.min(t.amount / t.threshold, 1)

  // --- setups --------------------------------------------------------------------------------
  const online = (seenAt: string) => Date.now() - new Date(seenAt).getTime() < kOffline
  const idOf = (m: MiningModuleStatus) => `${m.x},${m.y},${m.z}`
  const planOf = (plan: MiningPlanEntry[], m: MiningModuleStatus) =>
    plan.find((p) => p.module === idOf(m))
  const settingText = (p?: MiningPlanEntry) =>
    p?.setting
      ? `${kDrones[p.setting.droneTier] ?? p.setting.droneTier} drone, distance ${p.setting.distance}, ` +
        `${kPlasmas[p.setting.plasmaTier] ?? p.setting.plasmaTier} plasma, ${p.setting.parallel}×`
      : 'idle'
  const ratesText = (p?: MiningPlanEntry) =>
    (p?.rates ?? []).map((r) => `${plainLabel(r.label)} ${compact(r.rate)}/s`).join(', ')
  const poolText = (pool?: { tier: number; count: number }[]) =>
    (pool ?? [])
      .filter((p) => p.count > 0)
      .map((p) => `${kDrones[p.tier] ?? p.tier} ×${p.count}`)
      .join(', ') || 'empty'
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
  {#if mining}
    <p class="muted">
      Plasma: {mining.plasma ? mining.plasma.label : 'none in stock: the modules stay idle'} · an ore stops being
      mined at {compact(mining.bufferLimit)} in the buffer before processing
    </p>

    <h2>Targets</h2>
    <table>
      <tbody>
        {#each mining.targets as t (t.goodsId)}
          <tr class:off={!t.enabled}>
            <td class="icon"><Icon goods={t} /></td>
            <td>
              {shownLabel(t)}
              {#if !t.enabled}<span class="tag">paused</span>{/if}
              <div class="muted small">from {t.ores.map((o) => plainLabel(o.label)).join(', ') || 'no ore'}</div>
            </td>
            <td class="num">{amount(t.amount)} <span class="muted">/ {compact(t.threshold)}, {Math.round(fill(t) * 100)}%</span></td>
            <td class="num muted">priority {t.priority}</td>
            <td class="actions">
              <button class="small" onclick={() => open(t, t)}>Edit</button>
              <button class="small" onclick={() => act(() => putMiningTarget(t.goodsId, { threshold: t.threshold, priority: t.priority, enabled: !t.enabled, ores: t.ores }))}>
                {t.enabled ? 'Pause' : 'Resume'}
              </button>
              <button class="link" title="Remove the target" onclick={() => act(() => deleteMiningTarget(t.goodsId))}>✕</button>
            </td>
          </tr>
        {:else}
          <tr><td class="muted" colspan="5">No target yet: what dust (or fluid) should mining keep stocked?</td></tr>
        {/each}
      </tbody>
    </table>

    {#if form}
      <form class="panel" onsubmit={save}>
        <div class="title">
          <Icon goods={form.product} /> {shownLabel(form.product)}
          {#if form.amount !== null}<span class="muted">· now {amount(form.amount)}</span>{/if}
        </div>
        <label>Keep <input bind:value={form.threshold} placeholder="e.g. 1M" size="10" /></label>
        <label>Priority <input bind:value={form.priority} size="4" /></label>
        <label><input type="checkbox" bind:checked={form.enabled} /> enabled</label>
        <div class="ores">
          <div class="muted small">
            Made of — what the modules mine; the storage bus keeps these, the rest is voided.
            {#if ores && suggested.length === 0}
              The catalog knows no ore that processing turns into it (chemistry is not followed): pick them.
            {/if}
          </div>
          {#if !ores}<span class="muted">loading the ores…</span>{/if}
          {#each listed as o (oreKey(o))}
            <label class="ore">
              <input type="checkbox" bind:checked={form.ores[oreKey(o)]} />
              {plainLabel(o.label)}
              <span class="muted small">
                {suggested.includes(o) ? (o.name === form.product.name && o.damage === form.product.damage ? 'itself' : 'processing makes it') : ''}
                · {o.asteroids.join(', ')}
              </span>
            </label>
          {/each}
          <input placeholder="find an ore" bind:value={oreSearch} />
        </div>
        <div class="buttons">
          <button type="submit" disabled={!formValid}>Save</button>
          <button type="button" onclick={() => (form = null)}>Cancel</button>
        </div>
      </form>
    {:else}
      <div class="add">
        <input placeholder="Add a target: any item or fluid" bind:value={search} />
        {#each found as g (`${g.name}:${g.damage}:${g.variant}`)}
          <button class="found" onclick={() => open(g)}><Icon goods={g} /> {plainLabel(g.label)}</button>
        {/each}
      </div>
    {/if}

    <h2>Modules</h2>
    {#each mining.setups as s (s.name)}
      <article class:offline={!online(s.seenAt)}>
        <header>
          <strong>{s.name}</strong>
          <span class="muted">· {online(s.seenAt) ? 'polled' : 'OFFLINE since'} {new Date(s.seenAt).toLocaleTimeString()}
            · drones in the pool: {poolText(s.status.pool)}</span>
          <span class="spacer"></span>
          <button class="link" title="Forget this setup" onclick={() => {
            if (confirm(`Forget ${s.name}? A running agent comes back with its next poll.`)) act(() => forgetMiningSetup(s.name))
          }}>✕</button>
        </header>
        <table>
          <tbody>
            {#each s.status.modules ?? [] as m (idOf(m))}
              {@const p = planOf(s.plan, m)}
              <tr>
                <td class="label">T{m.tier} at {idOf(m)}</td>
                <td>
                  <span class="tag" class:warn={m.state === 'failed' || m.state === 'held'}>{m.state}</span>
                  {#if m.drone !== undefined}<span class="muted small">{kDrones[m.drone]} drone in</span>{/if}
                  {#if m.error}<div class="error small">{m.error}</div>{/if}
                </td>
                <td>{settingText(p)}<div class="muted small">{ratesText(p)}</div></td>
              </tr>
            {:else}
              <tr><td class="muted">No module found by the agent.</td></tr>
            {/each}
          </tbody>
        </table>
      </article>
    {:else}
      <p class="muted">No mining agent yet: one shows here after its first poll (factoryctl/mining).</p>
    {/each}
  {/if}
</section>

<style>
  h2 {
    font-size: 1.05rem;
    margin: 1rem 0 0.4rem;
  }
  table {
    width: 100%;
    border-collapse: collapse;
  }
  td {
    padding: 0.25rem 0.5rem;
    border-bottom: 1px solid var(--line);
    vertical-align: top;
  }
  td.icon {
    width: 32px;
  }
  tr.off td {
    opacity: 0.55;
  }
  .num {
    text-align: right;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .label,
  .actions {
    white-space: nowrap;
  }
  .panel {
    display: grid;
    gap: 0.4rem;
    padding: 0.6rem;
    margin: 0.5rem 0;
    border: 1px solid var(--line);
    border-radius: 6px;
    background: var(--panel);
  }
  .title {
    font-weight: 600;
    display: flex;
    gap: 0.4rem;
    align-items: center;
  }
  .ores {
    display: grid;
    gap: 0.2rem;
  }
  .ore {
    display: flex;
    gap: 0.4rem;
    align-items: baseline;
  }
  .buttons,
  .add {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    margin-top: 0.4rem;
  }
  .found {
    display: flex;
    gap: 0.3rem;
    align-items: center;
  }
  article {
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 0.5rem 0.8rem;
    margin-bottom: 0.8rem;
  }
  article.offline {
    opacity: 0.6;
  }
  header {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    align-items: baseline;
  }
  .spacer {
    flex: 1;
  }
  .tag {
    font-size: 0.75rem;
    padding: 0 0.3rem;
    border: 1px solid var(--line);
    border-radius: 3px;
    color: var(--muted);
  }
  .tag.warn {
    border-color: var(--warn);
    color: var(--warn);
  }
  .small {
    font-size: 0.8rem;
  }
  button.small {
    font-size: 0.8rem;
  }
  button.link {
    border: none;
    background: none;
    cursor: pointer;
    color: var(--muted);
  }
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
</style>
