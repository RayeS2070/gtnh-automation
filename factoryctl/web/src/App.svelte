<script lang="ts">
  import Stock from './lib/Stock.svelte'
</script>

<header>
  <h1>factoryctl</h1>
</header>
<main>
  <Stock />
</main>

<style>
  header {
    padding: 0.75rem 1rem;
    border-bottom: 1px solid var(--line);
  }
  h1 {
    margin: 0;
    font-size: 1.1rem;
  }
  main {
    max-width: 60rem;
    margin: 0 auto;
    padding: 1rem;
  }
</style>
