<script lang="ts">
  import {
    fetchCpus,
    fetchStock,
    fetchTargets,
    type Cpu,
    type StockItem,
    type StockQuery,
    type Target,
  } from './api'
  import { amount, compact, isDrop, shownLabel, unit } from './format'
  import Icon from './Icon.svelte'
  import OrderForm from './OrderForm.svelte'
  import { every, kRefreshMs } from './refresh'
  import TargetForm from './TargetForm.svelte'
  import { stockView as view } from './views.svelte'

  const kinds = { all: 'All', items: 'Items', fluids: 'Fluids' } as const

  const query = (): StockQuery => ({ ...view })

  let items = $state<StockItem[]>([])
  let targets = $state(new Map<number, Target>())
  // The goods whose forms are open above the grid. A search that leaves them out keeps them;
  // a list that has them refreshes their amount.
  let selected = $state<StockItem | null>(null)
  // Bumped to start the forms over: after a save or an order.
  let formsVersion = $state(0)
  let placed = $state('')
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

  // What the CPUs craft, by goods: ordering what is on a CPU already is seldom meant.
  let crafting = $state(new Map<number, Cpu[]>())
  async function loadCpus() {
    try {
      const map = new Map<number, Cpu[]>()
      for (const c of (await fetchCpus()).items) {
        if (c.goods) map.set(c.goods.goodsId, [...(map.get(c.goods.goodsId) ?? []), c])
      }
      crafting = map
    } catch (e) {
      error = String(e)
    }
  }
  loadCpus()

  // Answers to older queries are dropped: typing fast must not show a stale list.
  let asked = 0
  async function loadStock(q: StockQuery) {
    const n = ++asked
    loading = true
    try {
      const found = await fetchStock(q)
      if (n === asked) {
        items = found
        error = ''
        const id = selected?.goodsId
        selected = found.find((i) => i.goodsId === id) ?? selected
      }
    } catch (e) {
      if (n === asked) error = String(e)
    } finally {
      if (n === asked) loading = false
    }
  }

  // A moment after typing or switching stops.
  $effect(() => {
    const q = query()
    const timer = setTimeout(() => loadStock(q), 250)
    return () => clearTimeout(timer)
  })

  // And again on its own: stock and targets change while the page is open. Open forms keep what
  // is typed in them: they read their values once.
  $effect(() =>
    every(kRefreshMs, () => {
      loadStock(query())
      loadTargets()
      loadCpus()
    }),
  )

  function select(item: StockItem) {
    selected = selected?.goodsId === item.goodsId ? null : item
    placed = ''
  }

  const keyOf = (item: StockItem) => `${item.name}:${item.damage}${isDrop(item) ? ` · ${item.variant}` : ''}`
</script>

<div class="controls">
  <input class="search" type="search" placeholder="Search: words in any order, e.g. al cab" bind:value={view.text} />
  <div class="switch">
    {#each Object.entries(kinds) as [key, title] (key)}
      <button class:on={view.kind === key} onclick={() => (view.kind = key as StockQuery['kind'])}>{title}</button>
    {/each}
  </div>
  <select bind:value={view.sort}>
    <option value="label">by name</option>
    <option value="amount">by amount</option>
  </select>
  <button onclick={() => (view.dir = view.dir === 'asc' ? 'desc' : 'asc')}>
    {view.dir === 'asc' ? '↑ ascending' : '↓ descending'}
  </button>
</div>

{#if selected}
  {@const target = targets.get(selected.goodsId)}
  <section class="selected">
    <div class="head">
      <Icon goods={selected} />
      <div class="name">
        <div class="label">{shownLabel(selected)}</div>
        <div class="key">{keyOf(selected)}</div>
      </div>
      <div class="num">{amount(selected.amount)}{unit(selected)}</div>
      <button class="close" title="Close" onclick={() => (selected = null)}>✕</button>
    </div>
    {#each crafting.get(selected.goodsId) ?? [] as c (c.name)}
      <p class="oncpu">
        Already crafting on {c.name}: {amount(c.remaining)}{unit(selected)} left,
        {#if c.order !== null}<a href="#/orders">order #{c.order}</a>{:else}put on in game{/if}.
      </p>
    {/each}
    {#if placed}<p class="ok">{placed}</p>{/if}
    {#key `${selected.goodsId}:${formsVersion}`}
      <div class="forms">
        <TargetForm
          goods={selected}
          current={target}
          onsaved={async () => {
            await loadTargets()
            formsVersion++
          }}
          oncancel={() => (selected = null)}
        />
        {#if selected.craftable}
          <OrderForm
            goods={selected}
            onplaced={(id) => {
              placed = `Order #${id} queued: see Orders.`
              formsVersion++
            }}
            oncancel={() => (selected = null)}
          />
        {:else}
          <p class="nopattern-note">
            AE has no pattern for it, so it cannot be ordered. A target waits until AE can craft it
            again; the next scan sees a new pattern (a target's, the next poll).
          </p>
        {/if}
      </div>
    {/key}
  </section>
{/if}

{#if error}
  <p class="error">{error}</p>
{:else if !loading && items.length === 0}
  <p class="muted">Nothing found{view.text || view.kind !== 'all' ? '' : ': no finished scan yet?'}</p>
{/if}

<div class="grid">
  {#each items as item (item.goodsId)}
    {@const target = targets.get(item.goodsId)}
    <button
      class="tile"
      class:current={selected?.goodsId === item.goodsId}
      title={`${shownLabel(item)}\n${amount(item.amount)}${unit(item)}\n${keyOf(item)}`}
      onclick={() => select(item)}
    >
      <Icon goods={item} />
      <span class="text">
        <span class="label">{shownLabel(item)}</span>
        <span class="amount" class:zero={item.amount === 0}>
          {compact(item.amount)}{unit(item)}
          {#if target}
            <span class="target" class:off={!target.enabled}>≥ {compact(target.threshold)}</span>
          {/if}
          {#if !item.craftable}<span class="nopattern">no pattern</span>{/if}
          {#if crafting.has(item.goodsId)}
            {@const on = crafting.get(item.goodsId)!}
            <span class="oncpu" title={on.map((c) => c.name).join(', ')}>
              ⚙ {on.length === 1 ? on[0].name : `${on.length} CPUs`}
            </span>
          {/if}
        </span>
      </span>
    </button>
  {/each}
</div>

<style>
  .controls {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    margin-bottom: 0.75rem;
  }
  .search {
    flex: 1 1 16rem;
    padding: 0.5rem 0.75rem;
    font-size: 1rem;
  }
  .switch {
    display: flex;
  }
  .switch button {
    border-radius: 0;
  }
  .switch button:first-child {
    border-radius: 4px 0 0 4px;
  }
  .switch button:last-child {
    border-radius: 0 4px 4px 0;
  }
  .switch button.on {
    background: var(--accent);
    color: var(--bg);
  }

  .selected {
    position: sticky;
    top: 0;
    z-index: 1;
    margin-bottom: 0.75rem;
    padding: 0.6rem;
    border: 1px solid var(--accent);
    border-radius: 6px;
    background: var(--bg);
  }
  .head {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }
  .name {
    flex: 1;
    min-width: 0;
  }
  .head .label {
    font-weight: 600;
  }
  .forms {
    display: flex;
    flex-wrap: wrap;
    gap: 0 1rem;
  }
  .forms > :global(form) {
    flex: 1 1 26rem;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(12rem, 1fr));
    gap: 0.4rem;
  }
  .tile {
    /* The whole network is some 5000 tiles: the browser skips laying out the ones off screen. */
    content-visibility: auto;
    contain-intrinsic-size: auto 3.2rem;
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.35rem 0.5rem;
    border: 1px solid var(--line);
    border-radius: 6px;
    background: var(--panel);
    color: inherit;
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .tile:hover {
    border-color: var(--muted);
  }
  .tile.current {
    border-color: var(--accent);
    box-shadow: 0 0 0 1px var(--accent);
  }
  .text {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }
  .tile .label {
    display: -webkit-box;
    -webkit-box-orient: vertical;
    -webkit-line-clamp: 2;
    line-clamp: 2;
    overflow: hidden;
    line-height: 1.25;
  }
  .amount {
    font-size: 0.85rem;
    font-variant-numeric: tabular-nums;
  }
  .target {
    margin-left: 0.3rem;
    color: var(--accent);
  }
  .target.off {
    color: var(--muted);
    text-decoration: line-through;
  }

  .nopattern {
    margin-left: 0.3rem;
    color: var(--warn);
  }
  .oncpu {
    margin-left: 0.3rem;
    color: var(--accent);
  }
  p.oncpu {
    margin: 0.3rem 0;
  }
  .nopattern-note {
    flex: 1 1 26rem;
    margin: 0.3rem 0;
    color: var(--warn);
  }

  .key {
    font-size: 0.75rem;
    color: var(--muted);
  }
  .num {
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .close {
    padding: 0.2rem 0.5rem;
  }
  .zero,
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
  .ok {
    margin: 0.3rem 0;
    color: var(--accent);
  }
</style>
