<script lang="ts">
  import { fetchStock, fetchTargets, type StockItem, type Target } from './api'
  import { amount, isDrop, plainLabel, unit } from './format'
  import TargetForm from './TargetForm.svelte'

  let query = $state('')
  let items = $state<StockItem[]>([])
  let targets = $state(new Map<number, Target>())
  let editing = $state<number | null>(null)
  let error = $state('')
  let loading = $state(false)

  async function loadTargets() {
    try {
      targets = new Map((await fetchTargets()).map((t) => [t.goodsId, t]))
    } catch (e) {
      error = String(e)
    }
  }
  loadTargets()

  // Ask the server a moment after typing stops, and drop answers to older queries.
  let asked = 0
  $effect(() => {
    const q = query
    const n = ++asked
    const timer = setTimeout(async () => {
      loading = true
      try {
        const found = await fetchStock(q)
        if (n === asked) {
          items = found
          error = ''
        }
      } catch (e) {
        if (n === asked) error = String(e)
      } finally {
        if (n === asked) loading = false
      }
    }, 250)
    return () => clearTimeout(timer)
  })
</script>

<section>
  <input class="search" type="search" placeholder="Search by name" bind:value={query} />
  {#if error}
    <p class="error">{error}</p>
  {:else if !loading && items.length === 0}
    <p class="muted">Nothing found{query ? '' : ': no finished scan yet?'}</p>
  {/if}
  <table>
    <tbody>
      {#each items as item (item.goodsId)}
        {@const target = targets.get(item.goodsId)}
        <tr>
          <td>
            <div>{plainLabel(item.label)}</div>
            <div class="key">{item.name}:{item.damage}{isDrop(item) ? ` · ${item.variant}` : ''}</div>
          </td>
          <td class="num" class:zero={item.amount === 0}>{amount(item.amount)}{unit(item)}</td>
          <td class="actions">
            <button class:set={target} onclick={() => (editing = editing === item.goodsId ? null : item.goodsId)}>
              {target ? `≥ ${amount(target.threshold)}` : 'Target…'}
            </button>
          </td>
        </tr>
        {#if editing === item.goodsId}
          <tr>
            <td colspan="3">
              <TargetForm
                goods={item}
                current={target}
                onsaved={() => {
                  editing = null
                  loadTargets()
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
  .search {
    width: 100%;
    box-sizing: border-box;
    padding: 0.5rem 0.75rem;
    font-size: 1rem;
    margin-bottom: 0.75rem;
  }
  table {
    width: 100%;
    border-collapse: collapse;
  }
  td {
    padding: 0.35rem 0.5rem;
    border-bottom: 1px solid var(--line);
    vertical-align: top;
  }
  .key {
    font-size: 0.75rem;
    color: var(--muted);
  }
  .num {
    text-align: right;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .actions {
    width: 1%;
    white-space: nowrap;
  }
  button.set {
    font-variant-numeric: tabular-nums;
  }
  .zero,
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
</style>
