<script lang="ts">
  import Bhc from './lib/Bhc.svelte'
  import Cpus from './lib/Cpus.svelte'
  import Logs from './lib/Logs.svelte'
  import Mining from './lib/Mining.svelte'
  import Orders from './lib/Orders.svelte'
  import Pumps from './lib/Pumps.svelte'
  import Qft from './lib/Qft.svelte'
  import Stock from './lib/Stock.svelte'
  import Targets from './lib/Targets.svelte'

  // Pages by the URL hash (#/stock, #/targets, ...): no router needed for a few tabs.
  const pages = {
    stock: 'Stock',
    targets: 'Targets',
    orders: 'Orders',
    cpus: 'CPUs',
    pumps: 'Pumping',
    qft: 'QFT',
    mining: 'Mining',
    bhc: 'BHC',
    logs: 'Logs',
  } as const
  type Page = keyof typeof pages

  const fromHash = (): Page => {
    const name = location.hash.replace(/^#\/?/, '')
    return name in pages ? (name as Page) : 'stock'
  }
  let page = $state<Page>(fromHash())
  $effect(() => {
    const update = () => (page = fromHash())
    addEventListener('hashchange', update)
    return () => removeEventListener('hashchange', update)
  })
</script>

<header>
  <h1>factoryctl</h1>
  <nav>
    {#each Object.entries(pages) as [key, title] (key)}
      <a href={`#/${key}`} class:current={page === key}>{title}</a>
    {/each}
  </nav>
</header>
<main class:wide={page === 'stock' || page === 'logs'}>
  {#if page === 'targets'}
    <Targets />
  {:else if page === 'orders'}
    <Orders />
  {:else if page === 'cpus'}
    <Cpus />
  {:else if page === 'pumps'}
    <Pumps />
  {:else if page === 'qft'}
    <Qft />
  {:else if page === 'mining'}
    <Mining />
  {:else if page === 'bhc'}
    <Bhc />
  {:else if page === 'logs'}
    <Logs />
  {:else}
    <Stock />
  {/if}
</main>

<style>
  header {
    display: flex;
    align-items: baseline;
    gap: 1.5rem;
    padding: 0.75rem 1rem;
    border-bottom: 1px solid var(--line);
  }
  h1 {
    margin: 0;
    font-size: 1.1rem;
  }
  nav {
    display: flex;
    gap: 1rem;
  }
  nav a {
    color: var(--muted);
    text-decoration: none;
  }
  nav a.current {
    color: var(--fg);
    font-weight: 600;
  }
  main {
    max-width: 60rem;
    margin: 0 auto;
    padding: 1rem;
  }
  /* The stock grid uses what the screen has. */
  main.wide {
    max-width: 110rem;
  }
</style>
