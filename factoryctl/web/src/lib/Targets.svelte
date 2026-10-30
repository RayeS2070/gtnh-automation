<script lang="ts">
  import { deleteTarget, fetchTargets, putTarget, type Target } from './api'
  import { amount, plainLabel, unit } from './format'
  import Icon from './Icon.svelte'
  import { every, kRefreshMs } from './refresh'
  import TargetForm from './TargetForm.svelte'

  let targets = $state<Target[]>([])
  let editing = $state<number | null>(null)
  let error = $state('')

  async function load() {
    try {
      targets = await fetchTargets()
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
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
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
    <tbody>
      {#each targets as t (t.goodsId)}
        <tr class:off={!t.enabled}>
          <td>
            <div class="goods">
              <Icon goods={t} />
              <div>
                <div>{plainLabel(t.label)} {#if t.priority === 'low'}<span class="tag">low</span>{/if}
                  {#if t.fill}<span class="tag">fill</span>{/if}</div>
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
                if (confirm(`Delete the target for ${plainLabel(t.label)}?`)) act(() => deleteTarget(t.goodsId))
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
  </table>
</section>

<style>
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
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
</style>
