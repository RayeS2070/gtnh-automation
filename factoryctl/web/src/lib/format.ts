// Minecraft formatting codes: § and one character (colour, bold, reset...).
export function plainLabel(label: string): string {
  return label.replace(/§./g, '')
}

export function isDrop(item: { variant: string }): boolean {
  return item.variant !== ''
}

const kGroups = new Intl.NumberFormat('en-US')

export function amount(n: number): string {
  return kGroups.format(n).replace(/,/g, ' ') // narrow no-break space between groups
}

export function unit(item: { variant: string }): string {
  return isDrop(item) ? ' mB' : ''
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
