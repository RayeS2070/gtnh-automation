<script lang="ts">
  import { placeOrder } from './api'
  import { amount as format, parseAmount, shownLabel, unit } from './format'
  import { loadName, saveName, signedIn } from './who'

  type Props = {
    goods: { goodsId: number; label: string; variant: string }
    onplaced: (id: number) => void
    oncancel: () => void
  }
  let { goods, onplaced, oncancel }: Props = $props()

  let count = $state('')
  let who = $state(loadName())
  let user = $state<string | null>(null)
  signedIn().then((u) => (user = u))
  let error = $state('')
  let saving = $state(false)

  const value = $derived(parseAmount(count))
  const valid = $derived(value !== null && value > 0)

  async function submit(event: SubmitEvent) {
    event.preventDefault()
    if (!valid) return
    saving = true
    try {
      if (!user) saveName(who)
      onplaced(await placeOrder(goods.goodsId, value!, user ?? who))
    } catch (e) {
      error = e instanceof Error ? e.message : String(e)
    } finally {
      saving = false
    }
  }
</script>

<form onsubmit={submit}>
  <div class="title">Order {shownLabel(goods)} once</div>
  <label>
    Amount
    <input bind:value={count} placeholder="e.g. 128, 10k" inputmode="numeric" />
    {#if value !== null}<span class="hint">{format(value)}{unit(goods)}</span>{/if}
  </label>
  {#if user}
    <div class="row"><span>Who</span><span>{user}</span></div>
  {:else}
    <label>Who <input bind:value={who} placeholder="your name" /></label>
  {/if}
  {#if error}<div class="error">{error}</div>{/if}
  <div class="buttons">
    <button type="submit" disabled={!valid || saving}>Order</button>
    <button type="button" onclick={oncancel}>Cancel</button>
  </div>
</form>

<style>
  form {
    display: grid;
    gap: 0.4rem;
    padding: 0.6rem;
    margin: 0.3rem 0;
    border: 1px solid var(--line);
    border-radius: 6px;
    background: var(--panel);
  }
  .title {
    font-weight: 600;
  }
  label,
  .row {
    display: grid;
    grid-template-columns: 8rem 12rem auto;
    align-items: center;
    gap: 0.5rem;
  }
  .hint {
    color: var(--muted);
    font-size: 0.85rem;
  }
  .buttons {
    display: flex;
    gap: 0.5rem;
  }
  .error {
    color: var(--error);
  }
</style>
