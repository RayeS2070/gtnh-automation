<script lang="ts">
  import type { FactoryEvent } from './api'
  import { shownLabel } from './format'
  import Icon from './Icon.svelte'

  let { events, showOrder = true }: { events: FactoryEvent[]; showOrder?: boolean } = $props()

  // Today's events by time only.
  const when = (iso: string) => {
    const d = new Date(iso)
    return d.toDateString() === new Date().toDateString()
      ? d.toLocaleTimeString()
      : d.toLocaleString()
  }
</script>

<table>
  <tbody>
    {#each events as e (e.id)}
      <tr class={e.level}>
        <td class="when" title={e.at}>{when(e.at)}</td>
        <td class="level">{e.level}</td>
        <td class="source" title={e.agentUptime !== null ? `agent uptime ${e.agentUptime.toFixed(1)} s` : ''}>
          {e.source}
        </td>
        <td>
          <div class="what">
            {#if e.goods}
              <Icon goods={e.goods} />
              <span class="goods">{shownLabel(e.goods)}</span>
            {/if}
            {#if showOrder && e.order !== null}<a href="#/orders" class="order">#{e.order}</a>{/if}
            <span class="text">{e.text}</span>
          </div>
        </td>
      </tr>
    {/each}
  </tbody>
</table>

<style>
  table {
    width: 100%;
    border-collapse: collapse;
  }
  td {
    padding: 0.25rem 0.5rem;
    border-bottom: 1px solid var(--line);
    vertical-align: middle;
    font-size: 0.9rem;
  }
  .when,
  .level,
  .source {
    width: 1%;
    white-space: nowrap;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
  }
  .what {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.4rem;
  }
  .goods {
    font-weight: 600;
  }
  .order {
    font-variant-numeric: tabular-nums;
  }
  .debug .text {
    color: var(--muted);
  }
  .warning .level,
  .warning .text {
    color: var(--warn);
  }
  .error .level,
  .error .text {
    color: var(--error);
  }
</style>
