<script lang="ts">
  import { cancelOrder, fetchEvents, fetchOrders, openStatuses, type FactoryEvent, type Order } from './api'
  import EventList from './EventList.svelte'
  import { amount, left, shownLabel, unit } from './format'
  import Icon from './Icon.svelte'
  import { every, kRefreshMs } from './refresh'

  let orders = $state<Order[]>([])
  let error = $state('')

  // The order whose history is open under its row.
  let history = $state<{ order: number; events: FactoryEvent[] } | null>(null)

  async function loadHistory(order: number) {
    const events = await fetchEvents({ order, limit: 100 })
    if (history === null || history.order === order) history = { order, events }
  }

  async function load() {
    try {
      orders = await fetchOrders()
      if (history) await loadHistory(history.order)
      error = ''
    } catch (e) {
      error = String(e)
    }
  }

  async function toggleHistory(order: number) {
    if (history?.order === order) {
      history = null
      return
    }
    history = { order, events: [] }
    try {
      await loadHistory(order)
    } catch (e) {
      error = String(e)
    }
  }

  // The agent polls every 10 s; refreshing a little more often shows its steps.
  load()
  $effect(() => every(kRefreshMs, load))

  async function cancel(o: Order) {
    if (!confirm(`Cancel the order of ${amount(o.amount)} ${shownLabel(o.goods)}?`)) return
    try {
      await cancelOrder(o.id)
      await load()
    } catch (e) {
      error = e instanceof Error ? e.message : String(e)
    }
  }

  const isOpen = (o: Order) => openStatuses.includes(o.status)
  const time = (iso: string) => new Date(iso).toLocaleString()
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
  {#if orders.length === 0 && !error}
    <p class="muted">No orders yet: order something from the stock list.</p>
  {/if}
  <table>
    <thead>
      <tr>
        <th>Goods</th>
        <th class="num">Amount</th>
        <th>Status</th>
        <th>Ordered</th>
        <th></th>
      </tr>
    </thead>
    <tbody>
      {#each orders as o (o.id)}
        <tr class:closed={!isOpen(o)}>
          <td>
            <div class="goods">
              <Icon goods={o.goods} />
              <div>
                <div>{shownLabel(o.goods)}</div>
                <div class="small">#{o.id} · {o.source === 'auto' ? 'target' : o.requestedBy || 'one-off'}</div>
              </div>
            </div>
          </td>
          <td class="num">{amount(o.amount)}{unit(o.goods)}</td>
          <td>
            <span class="status {o.status}">{o.status}</span>
            {#if o.status === 'crafting'}
              <div class="small">{o.cpu}: {left(o.remaining, o.goods)}</div>
            {/if}
            {#if o.cancelRequested && isOpen(o)}<div class="small">cancel asked</div>{/if}
            {#if o.message}<div class="small error">{o.message}</div>{/if}
            {#if !o.goods.craftable && (o.status === 'accepted' || o.status === 'crafting')}
              <div class="small warn">AE can no longer craft it: the craft may hang. Cancel it if it stops.</div>
            {/if}
          </td>
          <td class="small">{time(o.createdAt)}</td>
          <td class="actions">
            <button class:on={history?.order === o.id} onclick={() => toggleHistory(o.id)}>History</button>
            {#if isOpen(o) && !o.cancelRequested}
              <button onclick={() => cancel(o)}>Cancel</button>
            {/if}
          </td>
        </tr>
        {#if history?.order === o.id}
          <tr class="history">
            <td colspan="5">
              {#if history.events.length}
                <EventList events={history.events} showOrder={false} />
              {:else}
                <span class="muted">No history: the order is older than the events kept.</span>
              {/if}
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
  .small {
    font-size: 0.8rem;
    color: var(--muted);
  }
  .closed {
    opacity: 0.6;
  }
  .status {
    font-size: 0.8rem;
    padding: 0.05rem 0.4rem;
    border-radius: 3px;
    border: 1px solid var(--line);
  }
  .status.crafting,
  .status.accepted,
  .status.computing,
  .status.sent {
    border-color: var(--accent);
    color: var(--accent);
  }
  .status.failed,
  .status.lost {
    border-color: var(--error);
    color: var(--error);
  }
  .actions {
    width: 1%;
    white-space: nowrap;
  }
  .muted {
    color: var(--muted);
  }
  .history > td {
    padding: 0 0 0.5rem 2rem;
    background: var(--panel);
  }
  button.on {
    background: var(--accent);
    color: var(--bg);
  }
  .warn {
    color: var(--warn);
  }
  .error {
    color: var(--error);
  }
</style>
