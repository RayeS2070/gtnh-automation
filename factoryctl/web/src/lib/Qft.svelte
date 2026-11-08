<script lang="ts">
  import {
    fetchQft,
    forgetQft,
    putQftSettings,
    type QftCatalogRecipe,
    type QftMode,
    type QftOutputSettings,
    type QftSetup,
    type QftTrigger,
  } from './api'
  import { amount, compact, parseAmount, plainLabel } from './format'
  import { every, kRefreshMs } from './refresh'

  let setups = $state<QftSetup[]>([])
  let error = $state('')
  // The settings being edited, per setup: recipe id -> circuit -> fields as typed. Reloads keep it.
  let editing = $state<
    Record<
      string,
      {
        mode: QftMode
        enabled: Record<string, boolean>
        trigger: Record<string, QftTrigger>
        fields: Record<string, Record<number, Record<string, string>>>
      }
    >
  >({})

  async function load() {
    try {
      setups = await fetchQft()
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

  const kOffline = 60_000 // ms without a poll: the agent or its computer is away
  const online = (s: QftSetup) => Date.now() - new Date(s.seenAt).getTime() < kOffline

  const recipeSettings = (s: QftSetup, recipe: string) => s.settings?.recipes.find((r) => r.id === recipe)
  const enabled = (s: QftSetup, recipe: string) => recipeSettings(s, recipe)?.enabled !== false
  const triggerOf = (s: QftSetup, recipe: string): QftTrigger => recipeSettings(s, recipe)?.trigger ?? 'deficit'
  const outputSettings = (s: QftSetup, recipe: string, circuit: number): QftOutputSettings | undefined =>
    s.settings?.recipes.find((r) => r.id === recipe)?.outputs.find((o) => o.circuit === circuit)
  const recipeState = (s: QftSetup, recipe: string) => s.status.recipes.find((r) => r.id === recipe)
  const outputState = (s: QftSetup, recipe: string, circuit: number) =>
    recipeState(s, recipe)?.outputs.find((o) => o.circuit === circuit)

  // The fields a trigger edits: deficit keeps stock (target, priority), surplus weighs outputs.
  const fieldsOf = (t: QftTrigger) => (t === 'surplus' ? ['weight'] : ['target', 'priority'])
  // The trigger a row shows: the one being edited, else the saved one.
  const shownTrigger = (s: QftSetup, r: QftCatalogRecipe) => editing[s.name]?.trigger[r.id] ?? triggerOf(s, r.id)

  function startEdit(s: QftSetup) {
    const fields: Record<string, Record<number, Record<string, string>>> = {}
    const on: Record<string, boolean> = {}
    const trigger: Record<string, QftTrigger> = {}
    for (const r of s.catalog) {
      on[r.id] = enabled(s, r.id)
      trigger[r.id] = triggerOf(s, r.id)
      fields[r.id] = {}
      for (const o of r.outputs) {
        const cur = outputSettings(s, r.id, o.circuit)
        fields[r.id][o.circuit] = {}
        // Every field: the trigger may change while editing.
        for (const f of ['target', 'priority', 'weight']) {
          const v = cur?.[f as keyof QftOutputSettings]
          fields[r.id][o.circuit][f] = v === undefined ? '' : String(v)
        }
      }
    }
    editing[s.name] = { mode: s.settings?.mode ?? 'auto', enabled: on, trigger, fields }
  }

  // A typed value: empty is none (an output without a target is not tracked); k/M/G/T/P as elsewhere.
  function value(text: string): number | undefined | null {
    const t = text.trim()
    if (t === '') return undefined
    const n = parseAmount(t)
    if (n !== null) return n
    const f = Number(t)
    return Number.isFinite(f) && f >= 0 ? f : null
  }

  function save(s: QftSetup) {
    const e = editing[s.name]
    const recipes = s.catalog.map((r) => ({
      id: r.id,
      enabled: e.enabled[r.id],
      trigger: e.trigger[r.id],
      outputs: r.outputs.map((o) => {
        const out: QftOutputSettings = { circuit: o.circuit }
        for (const f of fieldsOf(e.trigger[r.id])) {
          const v = value(e.fields[r.id][o.circuit][f])
          if (v === null) throw new Error(`${r.name}, ${o.label}: ${f} is not a number`)
          if (v !== undefined) (out as Record<string, number>)[f] = v
        }
        return out
      }),
    }))
    act(async () => {
      await putQftSettings(s.name, { mode: e.mode, recipes })
      delete editing[s.name]
    })
  }

  // Switches a recipe on or off at once: the rest of the settings as they are.
  function toggle(s: QftSetup, id: string) {
    const cur = s.settings
    if (!cur) return
    const recipes = s.catalog.map((r) => {
      const have = cur.recipes.find((x) => x.id === r.id)
      return {
        ...have,
        id: r.id,
        enabled: r.id === id ? !enabled(s, r.id) : enabled(s, r.id),
        outputs: have?.outputs ?? [],
      }
    })
    act(() => putQftSettings(s.name, { mode: cur.mode, recipes }))
  }

  function phaseText(s: QftSetup) {
    const st = s.status.state
    if (!st?.phase) return 'no report yet'
    if (!st.recipe) return st.phase
    let text = `${st.phase}: ${s.catalog.find((r) => r.id === st.recipe)?.name ?? st.recipe}`
    if (st.fermium) text += ' + Fermium'
    if (st.neptunium) text += ` + Neptunium on output ${st.neptunium}`
    return text
  }

  const percent = (a: number, t: number) => `${Math.round((1000 * a) / t) / 10}%`

  // Recipes show their outputs only when unfolded: folded by default, the unfolded ones kept in
  // this browser. Editing unfolds them all: the fields are on the output rows.
  const kOpenKey = 'factoryctl.qft.open'
  function loadOpen(): string[] {
    try {
      const v = JSON.parse(localStorage.getItem(kOpenKey) ?? '[]')
      return Array.isArray(v) ? v.filter((x) => typeof x === 'string') : []
    } catch {
      return []
    }
  }
  let open = $state<string[]>(loadOpen())
  function setOpen(keys: string[]) {
    open = keys
    try {
      localStorage.setItem(kOpenKey, JSON.stringify(keys))
    } catch {
      // No storage (private window and the like): it lasts until the page is reloaded.
    }
  }
  const foldKey = (s: QftSetup, id: string) => `${s.name}/${id}`
  const isOpen = (s: QftSetup, id: string) => editing[s.name] !== undefined || open.includes(foldKey(s, id))
  const fold = (s: QftSetup, id: string) =>
    setOpen(open.includes(foldKey(s, id)) ? open.filter((k) => k !== foldKey(s, id)) : [...open, foldKey(s, id)])
  const anyOpen = (s: QftSetup) => s.catalog.some((r) => open.includes(foldKey(s, r.id)))
  function foldAll(s: QftSetup) {
    const mine = new Set(s.catalog.map((r) => foldKey(s, r.id)))
    const rest = open.filter((k) => !mine.has(k))
    setOpen(anyOpen(s) ? rest : [...rest, ...mine])
  }
  // Tracked outputs of a deficit recipe below their target.
  const belowCount = (s: QftSetup, id: string) =>
    triggerOf(s, id) === 'surplus'
      ? 0
      : (recipeState(s, id)?.outputs ?? []).filter((o) => o.target != null && o.amount < o.target).length
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
  {#if setups.length === 0 && !error}
    <p class="muted">No QFT yet: a QFT agent with `server` in its config/components.lua shows here after its first poll.</p>
  {/if}
  {#each setups as s (s.name)}
    <article class:offline={!online(s)}>
      <header>
        <h2>{s.name}</h2>
        {#if s.coords}<span class="muted">at {s.coords.x}, {s.coords.y}, {s.coords.z}</span>{/if}
        <span class="muted">· {online(s) ? 'polled' : 'OFFLINE since'} {new Date(s.seenAt).toLocaleTimeString()}</span>
        <span class="spacer"></span>
        <button
          class="link"
          title="Forget this setup and its settings"
          onclick={() => {
            if (confirm(`Forget ${s.name}? Its settings go too; a running agent comes back as a new setup.`))
              act(() => forgetQft(s.name))
          }}>✕</button>
      </header>
      <p class="state">
        <strong>{phaseText(s)}</strong>
        {#if s.status.state?.cycleSeconds}<span class="muted">· cycle {s.status.state.cycleSeconds} s</span>{/if}
        {#if s.settings}<span class="muted">· mode {s.settings.mode}, settings v{s.settings.version}</span>{/if}
        {#if s.status.plasma}
          <span class="muted">· plasma: Neptunium {s.status.plasma.neptunium != null ? compact(s.status.plasma.neptunium) : '?'},
            Fermium {s.status.plasma.fermium != null ? compact(s.status.plasma.fermium) : '?'} L</span>
        {/if}
      </p>

      {#if editing[s.name]}
        <form class="inline" onsubmit={(e) => { e.preventDefault(); try { save(s) } catch (err) { error = String(err) } }}>
          <label>
            Mode
            <select bind:value={editing[s.name].mode}>
              <option value="auto">auto: products first, then raw materials</option>
              <option value="deficit">deficit: only keep products stocked</option>
              <option value="surplus">surplus: only process raw materials</option>
            </select>
          </label>
          <button type="submit">Save</button>
          <button type="button" onclick={() => delete editing[s.name]}>Cancel</button>
          <span class="muted">empty target: the output is not tracked</span>
        </form>
      {:else if s.settings}
        <button onclick={() => startEdit(s)}>Edit settings</button>
        <button onclick={() => foldAll(s)}>{anyOpen(s) ? 'Fold all' : 'Unfold all'}</button>
      {:else}
        <button onclick={() => foldAll(s)}>{anyOpen(s) ? 'Fold all' : 'Unfold all'}</button>
      {/if}

      <table>
        <tbody>
          {#each s.catalog as r (r.id)}
            {@const st = recipeState(s, r.id)}
            <tr class="recipe" class:current={s.status.state?.recipe === r.id} class:off={!enabled(s, r.id)}>
              <th colspan="4">
                {#if editing[s.name]}
                  <input type="checkbox" bind:checked={editing[s.name].enabled[r.id]} title="the recipe may run" />
                {:else if s.settings}
                  <button class="small" title="Switch the recipe off or on: off, it never runs; running, it finishes its cycle and yields"
                    onclick={() => toggle(s, r.id)}>{enabled(s, r.id) ? 'Off' : 'On'}</button>
                {/if}
                {#if editing[s.name]}
                  {r.name}
                {:else}
                  {@const below = belowCount(s, r.id)}
                  <button class="fold" aria-expanded={isOpen(s, r.id)} onclick={() => fold(s, r.id)}>
                    <span class="arrow">{isOpen(s, r.id) ? '▾' : '▸'}</span>
                    {r.name}
                    {#if !isOpen(s, r.id)}<span class="muted small">{r.outputs.length} outputs</span>{/if}
                    {#if below > 0}<span class="tag warn" title="Tracked outputs below their target">{below} below</span>{/if}
                  </button>
                {/if}
                {#if !enabled(s, r.id)}<span class="tag">off</span>{/if}
                {#if editing[s.name]}
                  <select class="small" bind:value={editing[s.name].trigger[r.id]}
                    title="deficit: keeps products stocked; surplus: processes raw materials while they are in stock">
                    <option value="deficit">deficit</option>
                    <option value="surplus">surplus</option>
                  </select>
                {:else}
                  <span class="tag">{triggerOf(s, r.id)}</span>
                {/if}
                <span class="muted small">tier {r.tier}, {r.catalysts} catalysts</span>
                {#if st?.disabled}<span class="tag">no catalysts</span>
                {:else if st?.low}<span class="tag warn">wants to run</span>{/if}
                {#if st?.cycles != null}<span class="muted small">· inputs for {st.cycles.toFixed(1)} cycles</span>{/if}
              </th>
            </tr>
            {#if isOpen(s, r.id)}
            {#each r.outputs as o (o.circuit)}
              {@const cur = outputSettings(s, r.id, o.circuit)}
              {@const os = outputState(s, r.id, o.circuit)}
              <tr>
                <td class="label"><span class="muted">{o.circuit}.</span> {plainLabel(o.label)}</td>
                <td class="num">
                  {#if os}
                    <span title={amount(os.amount)}>{compact(os.amount)}</span>{o.kind === 'fluid' ? ' L' : ''}
                    <span class="muted">/ {compact(os.target)}, {percent(os.amount, os.target)}</span>
                  {/if}
                </td>
                {#if editing[s.name]}
                  <td colspan="2">
                    {#each fieldsOf(shownTrigger(s, r)) as f (f)}
                      <label class="field">{f} <input bind:value={editing[s.name].fields[r.id][o.circuit][f]} size="8" /></label>
                    {/each}
                  </td>
                {:else}
                  <td class="num muted">
                    {#if triggerOf(s, r.id) === 'surplus'}
                      {cur?.weight != null ? `weight ${cur.weight}` : 'not wanted'}
                    {:else}
                      {cur?.target != null ? `keep ${compact(cur.target)}` : 'not tracked'}
                    {/if}
                  </td>
                  <td class="num muted">{triggerOf(s, r.id) !== 'surplus' && cur?.priority != null ? `priority ${cur.priority}` : ''}</td>
                {/if}
              </tr>
            {/each}
            {/if}
          {/each}
        </tbody>
      </table>
    </article>
  {/each}
</section>

<style>
  article {
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 0.6rem 0.8rem;
    margin-bottom: 1rem;
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
  h2 {
    margin: 0;
    font-size: 1.05rem;
  }
  .spacer {
    flex: 1;
  }
  .state {
    margin: 0.4rem 0;
  }
  table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 0.5rem;
  }
  td,
  th {
    padding: 0.25rem 0.5rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
  }
  tr.recipe th {
    padding-top: 0.7rem;
    font-weight: 600;
  }
  tr.off th {
    opacity: 0.55;
  }
  tr.current th {
    color: var(--accent, inherit);
  }
  .num {
    text-align: right;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .label {
    white-space: nowrap;
  }
  .field {
    margin-right: 0.6rem;
    white-space: nowrap;
  }
  form.inline {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem 0.8rem;
    align-items: center;
  }
  button.fold {
    display: inline-flex;
    align-items: baseline;
    gap: 0.4rem;
    border: none;
    background: none;
    padding: 0;
    font: inherit;
    color: inherit;
    cursor: pointer;
  }
  .arrow {
    display: inline-block;
    width: 0.8rem;
    color: var(--muted);
  }
  .tag {
    font-size: 0.7rem;
    padding: 0 0.3rem;
    border: 1px solid var(--line);
    border-radius: 3px;
    color: var(--muted);
    font-weight: normal;
  }
  .tag.warn {
    border-color: var(--warn);
    color: var(--warn);
  }
  .small {
    font-size: 0.8rem;
    font-weight: normal;
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
