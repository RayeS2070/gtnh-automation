<script lang="ts">
  import { cancelOrder, fetchOrders, openStatuses, type Order } from './api'
  import { amount, plainLabel, unit } from './format'
  import { every, kRefreshMs } from './refresh'

  let orders = $state<Order[]>([])
  let error = $state('')

  async function load() {
    try {
      orders = await fetchOrders()
      error = ''
    } catch (e) {
      error = String(e)
    }
  }

  // The agent polls every 10 s; refreshing a little more often shows its steps.
  load()
  $effect(() => every(kRefreshMs, load))

  async function cancel(o: Order) {
    if (!confirm(`Cancel the order of ${amount(o.amount)} ${plainLabel(o.goods.label)}?`)) return
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
            <div>{plainLabel(o.goods.label)}</div>
            <div class="small">#{o.id} · {o.source === 'auto' ? 'target' : o.requestedBy || 'one-off'}</div>
          </td>
          <td class="num">{amount(o.amount)}{unit(o.goods)}</td>
          <td>
            <span class="status {o.status}">{o.status}</span>
            {#if o.status === 'crafting'}
              <div class="small">{o.cpu}: {amount(o.remaining)}{unit(o.goods)} left</div>
            {/if}
            {#if o.cancelRequested && isOpen(o)}<div class="small">cancel asked</div>{/if}
            {#if o.message}<div class="small error">{o.message}</div>{/if}
          </td>
          <td class="small">{time(o.createdAt)}</td>
          <td class="actions">
            {#if isOpen(o) && !o.cancelRequested}
              <button onclick={() => cancel(o)}>Cancel</button>
            {/if}
          </td>
        </tr>
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
  .error {
    color: var(--error);
  }
</style>
