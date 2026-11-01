<script lang="ts">
  import {
    addGroup,
    deleteGroup,
    deleteTarget,
    fetchGroups,
    fetchTargets,
    putTarget,
    renameGroup,
    type Target,
    type TargetGroup,
  } from './api'
  import { amount, shownLabel, unit } from './format'
  import Icon from './Icon.svelte'
  import { every, kRefreshMs } from './refresh'
  import TargetForm from './TargetForm.svelte'

  let targets = $state<Target[]>([])
  let groups = $state<TargetGroup[]>([])
  let editing = $state<number | null>(null)
  let error = $state('')
  let newGroup = $state('')
  let renaming = $state<{ id: number; name: string } | null>(null)

  async function load() {
    try {
      ;[targets, groups] = await Promise.all([fetchTargets(), fetchGroups()])
      error = ''
    } catch (e) {
      error = String(e)
    }
  }
  load()
  // Stock is measured every poll: keep the numbers current. An open form keeps what is typed.
  $effect(() => every(kRefreshMs, load))

  async function act(fn: () => Promise<void>) {
    try {
      await fn()
      await load()
    } catch (e) {
      error = e instanceof Error ? e.message : String(e)
    }
  }

  const below = (t: Target) => t.enabled && t.amount < t.threshold

  // Targets by group, groups by name (as the server sends them), the ungrouped last. A group
  // without targets has no section: it is in the list of groups above.
  const sections = $derived.by(() => {
    const out = groups
      .map((g) => ({ key: g.id, name: g.name, items: targets.filter((t) => t.group === g.id) }))
      .filter((s) => s.items.length > 0)
    const known = new Set(groups.map((g) => g.id))
    const rest = targets.filter((t) => t.group === null || !known.has(t.group))
    if (rest.length > 0) out.push({ key: 0, name: out.length > 0 ? 'No group' : '', items: rest })
    return out
  })

  function add(event: SubmitEvent) {
    event.preventDefault()
    const name = newGroup.trim()
    if (!name) return
    act(async () => {
      await addGroup(name)
      newGroup = ''
    })
  }

  function rename(event: SubmitEvent) {
    event.preventDefault()
    const r = renaming
    if (!r || !r.name.trim()) return
    act(async () => {
      await renameGroup(r.id, r.name.trim())
      renaming = null
    })
  }
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
  <div class="groups">
    <span class="muted">Groups:</span>
    {#each groups as g (g.id)}
      {#if renaming?.id === g.id}
        <form class="inline" onsubmit={rename}>
          <input bind:value={renaming.name} />
          <button type="submit">Save</button>
          <button type="button" onclick={() => (renaming = null)}>Cancel</button>
        </form>
      {:else}
        <span class="group">
          {g.name} <span class="muted">{g.targets}</span>
          <button class="link" title="Rename" onclick={() => (renaming = { id: g.id, name: g.name })}
            >✎</button>
          <button
            class="link"
            title="Delete the group; its targets stay, ungrouped"
            onclick={() => {
              if (confirm(`Delete the group ${g.name}? Its targets stay, ungrouped.`))
                act(() => deleteGroup(g.id))
            }}>✕</button>
        </span>
      {/if}
    {/each}
    <form class="inline" onsubmit={add}>
      <input bind:value={newGroup} placeholder="new group" />
      <button type="submit" disabled={!newGroup.trim()}>Add</button>
    </form>
  </div>
  {#if targets.length === 0 && !error}
    <p class="muted">No targets yet: set one from the stock list.</p>
  {/if}
  <table>
    <thead>
      <tr>
        <th>Goods</th>
        <th class="num">Stock</th>
        <th class="num">Keep</th>
        <th class="num">Order</th>
        <th></th>
      </tr>
    </thead>
    {#each sections as section (section.key)}
    <tbody>
      {#if section.name}
        <tr class="section"><th colspan="5">{section.name}</th></tr>
      {/if}
      {#each section.items as t (t.goodsId)}
        <tr class:off={!t.enabled}>
          <td>
            <div class="goods">
              <Icon goods={t} />
              <div>
                <div>{shownLabel(t)} {#if t.priority === 'low'}<span class="tag">low</span>{/if}
                  {#if t.fill}<span class="tag">fill</span>{/if}
                  {#if !t.craftable}<span class="tag warn" title="AE has no pattern for it: the target waits until it has">no pattern</span>{/if}</div>
                {#if t.note}<div class="note">{t.note}</div>{/if}
              </div>
            </div>
          </td>
          <td class="num" class:below={below(t)}>{amount(t.amount)}{unit(t)}</td>
          <td class="num">{t.threshold === 0 ? 'always' : amount(t.threshold)}</td>
          <td class="num">{amount(t.batch)}</td>
          <td class="actions">
            <button onclick={() => (editing = editing === t.goodsId ? null : t.goodsId)}>Edit</button>
            <button onclick={() => act(() => putTarget(t.goodsId, { ...t, enabled: !t.enabled }))}>
              {t.enabled ? 'Pause' : 'Resume'}
            </button>
            <button
              onclick={() => {
                if (confirm(`Delete the target for ${shownLabel(t)}?`)) act(() => deleteTarget(t.goodsId))
              }}>Delete</button>
          </td>
        </tr>
        {#if editing === t.goodsId}
          <tr>
            <td colspan="5">
              <TargetForm
                goods={t}
                current={t}
                onsaved={() => {
                  editing = null
                  load()
                }}
                oncancel={() => (editing = null)}
              />
            </td>
          </tr>
        {/if}
      {/each}
    </tbody>
    {/each}
  </table>
</section>

<style>
  .groups {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.4rem 0.8rem;
    margin-bottom: 0.6rem;
  }
  .group {
    padding: 0.1rem 0.4rem;
    border: 1px solid var(--line);
    border-radius: 4px;
  }
  form.inline {
    display: inline-flex;
    gap: 0.3rem;
  }
  button.link {
    border: none;
    background: none;
    padding: 0 0.15rem;
    cursor: pointer;
    color: var(--muted);
  }
  tr.section th {
    padding-top: 0.8rem;
    color: inherit;
    font-size: 0.95rem;
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
    vertical-align: top;
  }
  .num {
    text-align: right;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .below {
    color: var(--warn);
    font-weight: 600;
  }
  .off {
    opacity: 0.55;
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
  .tag.warn {
    border-color: var(--warn);
    color: var(--warn);
  }
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
</style>
