<script lang="ts">
  import { placeOrder } from './api'
  import { amount as format, parseAmount, plainLabel, unit } from './format'
  import { loadName, saveName } from './who'

  type Props = {
    goods: { goodsId: number; label: string; variant: string }
    onplaced: (id: number) => void
    oncancel: () => void
  }
  let { goods, onplaced, oncancel }: Props = $props()

  let count = $state('')
  let who = $state(loadName())
  let error = $state('')
  let saving = $state(false)

  const value = $derived(parseAmount(count))
  const valid = $derived(value !== null && value > 0)

  async function submit(event: SubmitEvent) {
    event.preventDefault()
    if (!valid) return
    saving = true
    try {
      saveName(who)
      onplaced(await placeOrder(goods.goodsId, value!, who))
    } catch (e) {
      error = e instanceof Error ? e.message : String(e)
    } finally {
      saving = false
    }
  }
</script>

<form onsubmit={submit}>
  <div class="title">Order {plainLabel(goods.label)} once</div>
  <label>
    Amount
    <input bind:value={count} placeholder="e.g. 128, 10k" inputmode="numeric" />
    {#if value !== null}<span class="hint">{format(value)}{unit(goods)}</span>{/if}
  </label>
  <label>Who <input bind:value={who} placeholder="your name" /></label>
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
  label {
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
