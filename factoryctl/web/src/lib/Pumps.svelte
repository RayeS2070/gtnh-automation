<script lang="ts">
  import {
    deletePumpTarget,
    fetchPumps,
    putPumpSettings,
    putPumpTarget,
    type PumpFluid,
    type Pumps,
  } from './api'
  import { amount, compact, parseAmount } from './format'
  import Icon from './Icon.svelte'
  import { every, kRefreshMs } from './refresh'

  let pumps = $state<Pumps | null>(null)
  let error = $state('')
  let showAll = $state(false)
  // What is being edited: a fluid's target, or the general settings. Reloads keep what is typed.
  let editing = $state<{ fluid: string; target: string; priority: string; enabled: boolean } | null>(
    null,
  )
  let general = $state<{ batch: string; threshold: string } | null>(null)

  async function load() {
    try {
      pumps = await fetchPumps()
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

  // As the stock keys a fluid: its icon and label come from the same data.
  const goodsOf = (fluid: string, label: string) => ({
    name: 'ae2fc:fluid_drop',
    damage: 0,
    variant: fluid,
    label,
  })
  const byFluid = $derived(new Map((pumps?.fluids ?? []).map((f) => [f.fluid, f])))
  const labelOf = (fluid: string) => byFluid.get(fluid)?.label ?? fluid

  const fill = (f: PumpFluid) =>
    f.target && f.target.target > 0 && f.amount !== null ? f.amount / f.target.target : null
  const low = (f: PumpFluid) => {
    const x = fill(f)
    return !!(f.target?.enabled && x !== null && pumps?.settings && x < pumps.settings.threshold)
  }
  const shown = $derived((pumps?.fluids ?? []).filter((f) => showAll || f.target))
  const running = $derived((pumps?.modules ?? []).filter((m) => m.state === 'running').length)

  function edit(f: PumpFluid) {
    editing = {
      fluid: f.fluid,
      target: f.target ? String(f.target.target) : '',
      priority: String(f.target?.priority ?? 1),
      enabled: f.target?.enabled ?? true,
    }
  }

  const targetValue = $derived(editing ? parseAmount(editing.target) : null)
  const priorityValue = $derived(editing ? Number(editing.priority) : NaN)
  const editValid = $derived(targetValue !== null && priorityValue > 0)

  function saveTarget(event: SubmitEvent) {
    event.preventDefault()
    const e = editing
    if (!e || !editValid) return
    act(async () => {
      await putPumpTarget(e.fluid, { target: targetValue!, priority: priorityValue, enabled: e.enabled })
      editing = null
    })
  }

  const batchValue = $derived(general ? Number(general.batch) : NaN)
  const thresholdValue = $derived(general ? Number(general.threshold) / 100 : NaN)
  const generalValid = $derived(
    Number.isInteger(batchValue) &&
      batchValue >= 1 &&
      batchValue <= 128 &&
      thresholdValue > 0 &&
      thresholdValue <= 1,
  )

  function saveGeneral(event: SubmitEvent) {
    event.preventDefault()
    if (!generalValid) return
    act(async () => {
      await putPumpSettings(batchValue, thresholdValue)
      general = null
    })
  }

  const percent = (x: number) => `${Math.round(x * 1000) / 10}%`
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
  {#if pumps}
    <p class="summary">
      {#if pumps.settings}
        <span>
          settings v{pumps.settings.version}: cycles of {pumps.settings.batch} s, pumped below
          {percent(pumps.settings.threshold)} of the target
        </span>
        {#if !general}
          <button
            class="link"
            onclick={() =>
              (general = {
                batch: String(pumps!.settings!.batch),
                threshold: String(Math.round(pumps!.settings!.threshold * 1000) / 10),
              })}>✎</button>
        {/if}
      {:else}
        <span class="muted">no settings yet: the agent gives its own with its first poll</span>
      {/if}
      {#if pumps.failed > 0}<span class="warn">· {pumps.failed} failed cycles in 24 h (see Logs)</span>{/if}
    </p>
    {#if general}
      <form class="inline" onsubmit={saveGeneral}>
        <label>Batch, s <input bind:value={general.batch} inputmode="numeric" size="4" /></label>
        <label>Threshold, % <input bind:value={general.threshold} inputmode="decimal" size="4" /></label>
        <button type="submit" disabled={!generalValid}>Save</button>
        <button type="button" onclick={() => (general = null)}>Cancel</button>
      </form>
    {/if}

    <h2>Modules <span class="muted">{running} of {pumps.modules.length} running</span></h2>
    {#if pumps.modules.length === 0}
      <p class="muted">No modules yet: the pumping agent reports them with every poll.</p>
    {/if}
    <table>
      <tbody>
        {#each pumps.modules as m (m.address)}
          <tr class:idle={m.state !== 'running'}>
            <td class="name"><span class="tag">T{m.tier}</span> {m.address.slice(0, 8)}</td>
            <td>{m.state}</td>
            <td>
              {#each m.slots as s, i (i)}
                <span class="slot" title={`${labelOf(s.fluid)}, ${s.parallel} parallels`}>
                  <Icon goods={goodsOf(s.fluid, labelOf(s.fluid))} />
                  {labelOf(s.fluid)} <span class="muted">×{s.parallel}</span>
                </span>
              {/each}
            </td>
            <td class="progress">
              {#if m.progress !== null && m.maxProgress}
                <progress value={m.progress} max={m.maxProgress}></progress>
              {/if}
            </td>
          </tr>
        {/each}
      </tbody>
    </table>

    <h2>
      Fluids
      <label class="small"><input type="checkbox" bind:checked={showAll} /> every fluid a module can pump</label>
    </h2>
    <table>
      <thead>
        <tr>
          <th>Fluid</th>
          <th class="num">Stock</th>
          <th class="num">Target</th>
          <th class="num">Fill</th>
          <th class="num">In flight</th>
          <th class="num">24 h</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        {#each shown as f (f.fluid)}
          <tr class:off={!f.target || !f.target.enabled}>
            <td>
              <div class="goods">
                <Icon goods={goodsOf(f.fluid, f.label)} />
                <div>
                  <div>
                    {f.label}
                    {#if f.target && f.target.priority !== 1}<span class="tag">priority {f.target.priority}</span>{/if}
                  </div>
                  <div class="note">planet {f.planet}, gas {f.gas} · {compact(f.rate)} L/s a parallel</div>
                </div>
              </div>
            </td>
            <td class="num" class:low={low(f)} title={f.amount === null ? '' : amount(f.amount)}>{f.amount === null ? '—' : compact(f.amount)}</td>
            <td class="num">{f.target ? compact(f.target.target) : ''}</td>
            <td class="num" class:low={low(f)}>{fill(f) === null ? '' : percent(fill(f)!)}</td>
            <td class="num">{f.inFlight ? compact(f.inFlight) : ''}</td>
            <td class="num">{f.produced ? compact(f.produced) : ''}</td>
            <td class="actions">
              <button onclick={() => (editing?.fluid === f.fluid ? (editing = null) : edit(f))}>
                {f.target ? 'Edit' : 'Pump…'}
              </button>
              {#if f.target}
                <button onclick={() => act(() => putPumpTarget(f.fluid, { ...f.target!, enabled: !f.target!.enabled }))}>
                  {f.target.enabled ? 'Pause' : 'Resume'}
                </button>
                <button
                  onclick={() => {
                    if (confirm(`Stop pumping ${f.label}?`)) act(() => deletePumpTarget(f.fluid))
                  }}>Remove</button>
              {/if}
            </td>
          </tr>
          {#if editing?.fluid === f.fluid}
            <tr>
              <td colspan="7">
                <form class="inline" onsubmit={saveTarget}>
                  <label>
                    Keep
                    <input bind:value={editing.target} placeholder="e.g. 10G, 1.5T" inputmode="numeric" size="10" />
                    {#if targetValue !== null}<span class="muted">{amount(targetValue)} L</span>{/if}
                  </label>
                  <label>Priority <input bind:value={editing.priority} inputmode="decimal" size="3" /></label>
                  <label><input type="checkbox" bind:checked={editing.enabled} /> enabled</label>
                  <button type="submit" disabled={!editValid}>Save</button>
                  <button type="button" onclick={() => (editing = null)}>Cancel</button>
                </form>
              </td>
            </tr>
          {/if}
        {/each}
      </tbody>
    </table>
    {#if shown.length === 0}
      <p class="muted">No fluid is pumped: tick "every fluid" and pick one.</p>
    {/if}
  {/if}
</section>

<style>
  h2 {
    font-size: 1rem;
    margin: 1.2rem 0 0.4rem;
    display: flex;
    gap: 1rem;
    align-items: baseline;
  }
  h2 .muted,
  .small {
    font-size: 0.85rem;
    font-weight: normal;
  }
  table {
    width: 100%;
    border-collapse: collapse;
  }
  th {
    text-align: left;
    font-weight: 600;
    color: var(--muted);
    font-size: 0.85rem;
  }
  td,
  th {
    padding: 0.35rem 0.5rem;
    border-bottom: 1px solid var(--line);
    vertical-align: middle;
  }
  .num {
    text-align: right;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .name {
    white-space: nowrap;
    font-family: monospace;
  }
  .goods,
  .slot {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
  }
  .slot {
    margin-right: 0.8rem;
  }
  .progress progress {
    width: 8rem;
  }
  .idle,
  .off {
    opacity: 0.6;
  }
  .low {
    color: var(--warn);
    font-weight: 600;
  }
  .warn {
    color: var(--warn);
  }
  .actions {
    width: 1%;
    white-space: nowrap;
  }
  .note {
    font-size: 0.8rem;
    color: var(--muted);
  }
  .tag {
    font-size: 0.7rem;
    padding: 0 0.3rem;
    border: 1px solid var(--line);
    border-radius: 3px;
    color: var(--muted);
  }
  form.inline {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem 0.8rem;
    align-items: center;
    padding: 0.4rem 0;
  }
  button.link {
    border: none;
    background: none;
    padding: 0 0.15rem;
    cursor: pointer;
    color: var(--muted);
  }
  .summary {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    align-items: baseline;
  }
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
</style>
