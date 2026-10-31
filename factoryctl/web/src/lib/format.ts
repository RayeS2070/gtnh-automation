// Minecraft formatting codes: § and one character (colour, bold, reset...).
export function plainLabel(label: string): string {
  return label.replace(/§./g, '')
}

export function isDrop(item: { variant: string }): boolean {
  return item.variant !== ''
}

// What to show for goods: the label without formatting codes; a drop without "drop of ".
export function shownLabel(goods: { label: string; variant: string }): string {
  const label = plainLabel(goods.label)
  return isDrop(goods) ? label.replace(/^drop of /, '') : label
}

const kGroups = new Intl.NumberFormat('en-US')

export function amount(n: number): string {
  return kGroups.format(n).replace(/,/g, ' ') // narrow no-break space between groups
}

export function unit(item: { variant: string }): string {
  return isDrop(item) ? ' mB' : ''
}

// What a CPU has left to deliver. Below 0 while it finishes a batch bigger than asked (fluid
// crafts in 2.9): the surplus.
export function left(remaining: number, item: { variant: string } | null): string {
  const u = item ? unit(item) : ''
  return remaining < 0 ? `finishing, ${amount(-remaining)}${u} over` : `${amount(remaining)}${u} left`
}

const kCompact = new Intl.NumberFormat('en-US', { notation: 'compact', maximumFractionDigits: 1 })

// Short, for tight places: 1.5K, 23M.
export function compact(n: number): string {
  return kCompact.format(n)
}

const kSuffixes: Record<string, number> = { k: 1e3, m: 1e6, g: 1e9 }

// A count typed by a person: digits with optional spaces or _ between groups, an optional
// fraction and k/M/G suffix (10k, 1.5M). null when it is not a whole number >= 0.
export function parseAmount(text: string): number | null {
  const m = text.trim().replace(/[\s_ ]/g, '').match(/^(\d+(?:\.\d+)?)([kmg]?)$/i)
  if (!m) return null
  const n = Number(m[1]) * (m[2] ? kSuffixes[m[2].toLowerCase()] : 1)
  return Number.isSafeInteger(n) ? n : null
}
