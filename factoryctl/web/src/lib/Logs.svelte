<script lang="ts">
  import { fetchEvents, type EventLevel, type FactoryEvent } from './api'
  import EventList from './EventList.svelte'
  import { every, kRefreshMs } from './refresh'

  const kPage = 200
  const kMaxShown = 1000 // what the server gives at once
  const sources = ['', 'agent', 'order', 'scan', 'target', 'server']

  let level = $state<EventLevel>('info')
  let source = $state('')
  let text = $state('')
  let events = $state<FactoryEvent[]>([])
  let error = $state('')
  let more = $state(true)

  // Answers to older filters are dropped, as in the stock list.
  let asked = 0
  async function load(count: number) {
    const n = ++asked
    try {
      const found = await fetchEvents({ level, source, text, limit: count })
      if (n !== asked) return
      events = found
      more = found.length === count
      error = ''
    } catch (e) {
      if (n === asked) error = String(e)
    }
  }

  async function older() {
    const last = events.at(-1)
    if (!last) return
    try {
      const found = await fetchEvents({ level, source, text, before: last.id, limit: kPage })
      events = [...events, ...found]
      more = found.length === kPage
    } catch (e) {
      error = String(e)
    }
  }

  // A filter change starts over, a moment after typing stops.
  $effect(() => {
    void [level, source, text]
    const timer = setTimeout(() => load(kPage), 250)
    return () => clearTimeout(timer)
  })
  // New events come in at the top; pages loaded below stay.
  $effect(() => every(kRefreshMs, () => load(Math.min(Math.max(events.length, kPage), kMaxShown))))
</script>

<section>
  <div class="controls">
    <input type="search" placeholder="Search the text" bind:value={text} />
    <select bind:value={level}>
      <option value="debug">debug and above</option>
      <option value="info">info and above</option>
      <option value="warning">warnings and errors</option>
      <option value="error">errors</option>
    </select>
    <select bind:value={source}>
      {#each sources as s (s)}<option value={s}>{s || 'all sources'}</option>{/each}
    </select>
  </div>
  {#if error}<p class="error">{error}</p>{/if}
  {#if events.length === 0 && !error}<p class="muted">Nothing yet.</p>{/if}
  <EventList {events} />
  {#if more && events.length > 0}
    <button class="older" onclick={older}>Older…</button>
  {/if}
</section>

<style>
  .controls {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    margin-bottom: 0.75rem;
  }
  .controls input {
    flex: 1 1 16rem;
    padding: 0.4rem 0.6rem;
  }
  .older {
    margin-top: 0.75rem;
  }
  .muted {
    color: var(--muted);
  }
  .error {
    color: var(--error);
  }
</style>
