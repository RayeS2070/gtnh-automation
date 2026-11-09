<script lang="ts">
  import { fetchBhc, forgetBhc, putBhcSettings, type BhcSession, type BhcSettings, type BhcSetup } from './api'
  import { amount, compact, parseAmount } from './format'
  import { every, kRefreshMs } from './refresh'

  let setups = $state<BhcSetup[]>([])
  let error = $state('')
  // The settings being edited, per setup, as typed. Reloads keep them.
  let editing = $state<Record<string, { holdAt: string; floor: string; maxHolds: string; minSpacetime: string }>>({})

  async function load() {
    try {
      setups = await fetchBhc()
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

  // The agent reports every 30 s while the hole is closed and is silent while it is open.
  const kSilent = 90_000
  const silent = (s: BhcSetup) => Date.now() - new Date(s.seenAt).getTime() > kSilent

  // Spacetime for `holds` holds of a session: the cost doubles every 30 (bhc/hole.lua).
  function holdsCost(holds: number): number {
    const q = Math.floor(holds / 30)
    const r = holds % 30
    return 30 * (2 ** q - 1) + r * 2 ** q
  }

  function startEdit(s: BhcSetup) {
    const c = s.settings
    if (!c) return
    editing[s.name] = {
      holdAt: String(c.holdAt),
      floor: String(c.floor),
      maxHolds: String(c.maxHolds),
      minSpacetime: String(c.minSpacetime),
    }
  }

  function int(text: string, what: string): number {
    const n = Number(text.trim())
    if (!Number.isInteger(n)) throw new Error(`${what}: a whole number`)
    return n
  }

  function save(s: BhcSetup) {
    const e = editing[s.name]
    const minSpacetime = parseAmount(e.minSpacetime)
    if (minSpacetime === null) throw new Error('spacetime to open: a whole number of L (10k, 1.5M)')
    const settings: Omit<BhcSettings, 'version'> = {
      holdAt: int(e.holdAt, 'hold at'),
      floor: int(e.floor, 'floor'),
      maxHolds: int(e.maxHolds, 'holds per session'),
      minSpacetime,
    }
    act(async () => {
      await putBhcSettings(s.name, settings)
      delete editing[s.name]
    })
  }

  const editedHolds = (s: BhcSetup) => {
    const n = Number(editing[s.name]?.maxHolds)
    return Number.isInteger(n) && n >= 0 && n <= s.maxHolds ? n : null
  }

  const kEnding: Record<BhcSession['ending'], string> = {
    collapsed: 'closed by the agent',
    closed: 'closed, not by the agent',
    seed: 'seed not taken',
  }

  function minutes(seconds: number): string {
    const m = Math.floor(seconds / 60)
    return m > 0 ? `${m} min ${seconds % 60} s` : `${seconds} s`
  }

  function when(iso: string): string {
    const d = new Date(iso)
    const today = new Date().toDateString() === d.toDateString()
    return today ? d.toLocaleTimeString() : d.toLocaleString()
  }
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
  {#if setups.length === 0 && !error}
    <p class="muted">No black hole yet: a black hole agent with `server` in its config.lua shows here after its first report.</p>
  {/if}
  {#each setups as s (s.name)}
    {@const st = s.status.state ?? {}}
    {@const sup = s.status.supplies ?? {}}
    <article>
      <header>
        <h2>{s.name}</h2>
        {#if s.coords}<span class="muted">at {s.coords.x}, {s.coords.y}, {s.coords.z}</span>{/if}
        <span class="muted" title="The agent reports every 30 s while the hole is closed; while it is open, it hears nothing but its pulses">
          · reported {when(s.seenAt)}{silent(s) ? ': silent since, a hole open or the agent away' : ''}
        </span>
        <span class="spacer"></span>
        <button
          class="link"
          title="Forget this setup, its settings and sessions"
          onclick={() => {
            if (confirm(`Forget ${s.name}? Its settings and sessions go too; a running agent comes back as a new setup.`))
              act(() => forgetBhc(s.name))
          }}>✕</button>
      </header>

      <p class="state">
        <strong>{st.phase ?? '?'}</strong>
        {#if st.session != null}<span class="muted">· {st.session} sessions</span>{/if}
        {#if st.blocked}
          <span class="tag warn" title="A hold went wrong (see Logs): no new hole until the agent is restarted">blocked: restart the agent</span>
        {/if}
        {#if st.low}<span class="tag warn" title="The supplies had less spacetime than it takes to open">spacetime low</span>{/if}
        {#if st.work}<span class="tag">work waiting</span>{/if}
      </p>
      <p class="state muted">
        Supplies: {sup.seeds ?? '?'} seeds, {sup.collapsers ?? '?'} collapsers,
        <span title={sup.spacetime != null ? `${amount(sup.spacetime)} L` : ''}>{sup.spacetime != null ? compact(sup.spacetime) : '?'} L</span> spacetime
      </p>

      {#if editing[s.name]}
        {@const holds = editedHolds(s)}
        <form class="settings" onsubmit={(e) => { e.preventDefault(); try { save(s) } catch (err) { error = String(err) } }}>
          <label>Hold at <input bind:value={editing[s.name].holdAt} size="4" /> %
            <span class="muted small">above the longest recipe, in s (Eternal Singularity: 20)</span></label>
          <label>Floor <input bind:value={editing[s.name].floor} size="4" /> %
            <span class="muted small">work stops if a missed hold lets it get this low</span></label>
          <label>Holds per session <input bind:value={editing[s.name].maxHolds} size="5" />
            <span class="muted small">0 to {s.maxHolds}{holds !== null ? `: ${amount(holdsCost(holds))} L a session` : ''}; a running recipe is held to its end anyway</span></label>
          <label>Spacetime to open <input bind:value={editing[s.name].minSpacetime} size="8" /> L
            <span class="muted small">the supplies must have it to open a hole and to take new work</span></label>
          <div>
            <button type="submit">Save</button>
            <button type="button" onclick={() => delete editing[s.name]}>Cancel</button>
            <span class="muted small">the agent takes them once the hole is closed</span>
          </div>
        </form>
      {:else if s.settings}
        <p class="state">
          Hold at {s.settings.holdAt} %, floor {s.settings.floor} %, {s.settings.maxHolds} holds a session
          ({amount(holdsCost(s.settings.maxHolds))} L), open with ≥ {compact(s.settings.minSpacetime)} L
          <span class="muted">· v{s.settings.version}</span>
          <button onclick={() => startEdit(s)}>Edit</button>
        </p>
      {:else}
        <p class="muted">No settings yet: the agent offers its config.lua with its next report.</p>
      {/if}

      {#if s.sessions.length > 0}
        <table>
          <thead>
            <tr>
              <th>Closed</th>
              <th class="num">#</th>
              <th class="num">Open</th>
              <th class="num">Recipes</th>
              <th class="num">Holds</th>
              <th class="num">Spacetime</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {#each s.sessions as x (`${x.closedAt}/${x.session}`)}
              <tr class:warn={x.ending !== 'collapsed' || x.blocked || x.unknown}>
                <td>{when(x.closedAt)}</td>
                <td class="num">{x.session}</td>
                <td class="num">{minutes(x.seconds)}</td>
                <td class="num">{x.recipes}</td>
                <td class="num">{x.holds}</td>
                <td class="num">{compact(x.spent)} L</td>
                <td>
                  {#if x.ending !== 'collapsed'}<span class="tag warn">{kEnding[x.ending]}</span>{/if}
                  {#if x.blocked}<span class="tag warn" title="A hold went wrong: see Logs">hold failed</span>{/if}
                  {#if x.unknown}<span class="tag" title="Restarted without knowing the time: stability not counted">count lost</span>{/if}
                </td>
              </tr>
            {/each}
          </tbody>
        </table>
      {:else}
        <p class="muted">No sessions yet.</p>
      {/if}
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
  form.settings {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    margin: 0.5rem 0;
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
  th {
    font-weight: 600;
  }
  .num {
    text-align: right;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  tr.warn td {
    color: var(--warn);
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
