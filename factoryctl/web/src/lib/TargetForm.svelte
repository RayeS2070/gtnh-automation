<script lang="ts">
  import { untrack } from 'svelte'
  import { fetchGroups, putTarget, type Priority, type TargetGroup, type TargetSettings } from './api'
  import { amount, parseAmount, shownLabel, unit } from './format'

  type Props = {
    goods: { goodsId: number; label: string; variant: string }
    current?: TargetSettings
    onsaved: () => void
    oncancel: () => void
  }
  let { goods, current, onsaved, oncancel }: Props = $props()

  // Initial values from the current target, read once on purpose: the form then edits its
  // own copies, and a reload of the list must not overwrite what is being typed.
  const initial = untrack(() => current)
  let threshold = $state(initial ? String(initial.threshold) : '')
  let batch = $state(initial ? String(initial.batch) : '')
  let priority = $state<Priority>(initial?.priority ?? 'high')
  let enabled = $state(initial?.enabled ?? true)
  let note = $state(initial?.note ?? '')
  let fill = $state(initial?.fill ?? false)
  let group = $state<number | null>(initial?.group ?? null)
  let groups = $state<TargetGroup[]>([])
  // Optional: without the list the form still saves, with the group it had.
  fetchGroups().then((g) => (groups = g), () => {})
  let error = $state('')
  let saving = $state(false)

  const thresholdValue = $derived(parseAmount(threshold))
  const batchValue = $derived(parseAmount(batch))
  const valid = $derived(thresholdValue !== null && batchValue !== null && batchValue > 0)

  async function save(event: SubmitEvent) {
    event.preventDefault()
    if (!valid) return
    saving = true
    try {
      await putTarget(goods.goodsId, {
        threshold: thresholdValue!,
        batch: batchValue!,
        priority,
        enabled,
        note,
        fill,
        group,
      })
      onsaved()
    } catch (e) {
      error = e instanceof Error ? e.message : String(e)
    } finally {
      saving = false
    }
  }
</script>

<form onsubmit={save}>
  <div class="title">Target for {shownLabel(goods)}</div>
  <label>
    Keep at least
    <input bind:value={threshold} placeholder="0 = always" inputmode="numeric" />
    {#if thresholdValue !== null}<span class="hint">{amount(thresholdValue)}{unit(goods)}</span>{/if}
  </label>
  <label>
    Order
    <input bind:value={batch} placeholder="e.g. 64, 10k" inputmode="numeric" />
    {#if batchValue !== null}<span class="hint">{amount(batchValue)}{unit(goods)}</span>{/if}
  </label>
  <label>
    Priority
    <select bind:value={priority}>
      <option value="high">high</option>
      <option value="low">low: only when CPUs are free</option>
    </select>
  </label>
  <label class="inline">
    <input type="checkbox" bind:checked={fill} />
    order what is missing, in whole batches (instead of one batch)
  </label>
  <label>
    Group
    <select bind:value={group}>
      <option value={null}>none</option>
      {#each groups as g (g.id)}<option value={g.id}>{g.name}</option>{/each}
    </select>
    {#if groups.length === 0}<span class="hint">groups are made on the Targets tab</span>{/if}
  </label>
  <label class="inline"><input type="checkbox" bind:checked={enabled} /> enabled</label>
  <label>Note <input bind:value={note} /></label>
  {#if error}<div class="error">{error}</div>{/if}
  <div class="buttons">
    <button type="submit" disabled={!valid || saving}>Save</button>
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
  label.inline {
    display: block;
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
