<script lang="ts">
  import { iconOf, kAtlasUrl, kIconSize, kIconsPerRow } from './icons.svelte'

  let { goods }: { goods: { name: string; damage: number; variant: string; label: string } } = $props()

  const icon = $derived(iconOf(goods))
  const position = $derived(
    typeof icon === 'number'
      ? `-${(icon % kIconsPerRow) * kIconSize}px -${Math.floor(icon / kIconsPerRow) * kIconSize}px`
      : undefined,
  )
</script>

<span
  class="icon"
  class:none={icon === null}
  style:background-image={position && `url(${kAtlasUrl})`}
  style:background-position={position}
></span>

<style>
  .icon {
    display: inline-block;
    flex: none;
    width: 32px;
    height: 32px;
    background-repeat: no-repeat;
  }
  .none {
    box-sizing: border-box;
    border: 1px dashed var(--line);
    border-radius: 4px;
  }
</style>
