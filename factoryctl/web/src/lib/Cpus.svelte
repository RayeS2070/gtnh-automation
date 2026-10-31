<script lang="ts">
  import { fetchCpus, isIdle, type Cpu } from './api'
  import { amount, left, shownLabel, unit } from './format'
  import Icon from './Icon.svelte'
  import { every, kRefreshMs } from './refresh'

  let cpus = $state<Cpu[]>([])
  let lowFreeCpus = $state(0)
  let error = $state('')

  async function load() {
    try {
      const r = await fetchCpus()
      cpus = r.items
      lowFreeCpus = r.lowFreeCpus
      error = ''
    } catch (e) {
      error = String(e)
    }
  }
  load()
  $effect(() => every(kRefreshMs, load))

  const free = $derived(cpus.filter(isIdle).length)
  const seenAt = $derived(cpus.length ? new Date(cpus[0].seenAt) : null)
</script>

<section>
  {#if error}<p class="error">{error}</p>{/if}
  {#if cpus.length === 0 && !error}
    <p class="muted">No CPUs yet: the agent reports them with every poll.</p>
  {:else if cpus.length}
    <p class="summary">
      <span class:low={free < lowFreeCpus}>{free} of {cpus.length} free</span>
      <span class="muted">· low priority targets order while at least {lowFreeCpus} are free</span>
      {#if seenAt}<span class="muted">· as of {seenAt.toLocaleTimeString()}</span>{/if}
    </p>
  {/if}
  <table>
    <tbody>
      {#each cpus as c (c.name)}
        <tr class:idle={isIdle(c)}>
          <td class="name">{c.name}</td>
          <td>
            {#if c.goods}
              <div class="goods">
                <Icon goods={c.goods} />
                <div>{shownLabel(c.goods)}</div>
              </div>
            {:else if c.busy}
              <span class="muted">crafts goods no scan has seen yet</span>
            {:else}
              <span class="muted">free</span>
            {/if}
          </td>
          <td class="num">
            {#if c.goods || c.busy}{left(c.remaining, c.goods)}{/if}
          </td>
          <td class="small">
            {#if c.order !== null}<a href="#/orders">order #{c.order}</a>
            {:else if c.goods || c.busy}<span class="muted">put on in game</span>{/if}
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
  td {
    padding: 0.35rem 0.5rem;
    border-bottom: 1px solid var(--line);
    vertical-align: middle;
  }
  .name {
    width: 1%;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .num {
    text-align: right;
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }
  .small {
    font-size: 0.85rem;
    white-space: nowrap;
  }
  .idle {
    opacity: 0.55;
  }
  .summary {
    margin-top: 0;
  }
  .low {
    color: var(--warn);
  }
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
</style>
