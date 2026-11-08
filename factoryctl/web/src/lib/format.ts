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

// GregTech counts fluids in litres (1 L = 1 mB): so does the page.
export function unit(item: { variant: string }): string {
  return isDrop(item) ? ' L' : ''
}

// What a CPU has left to deliver. Below 0 while it finishes a batch bigger than asked (fluid
// crafts in 2.9): the surplus.
export function left(remaining: number, item: { variant: string } | null): string {
  const u = item ? unit(item) : ''
  return remaining < 0 ? `finishing, ${amount(-remaining)}${u} over` : `${amount(remaining)}${u} left`
}

// SI prefixes, as the AE2 terminal shows them; parseAmount takes the same.
const kPrefixes = ['', 'k', 'M', 'G', 'T', 'P', 'E']

// Short, for lists: three significant digits and an SI prefix (950, 1.59M, 5.5G, 227T); the
// exact number goes in a title or a form. Trailing zeros are dropped: a target of 2M stays 2M.
export function compact(n: number): string {
  if (!Number.isFinite(n)) return String(n)
  const sign = n < 0 ? '-' : ''
  let a = Math.abs(n)
  if (a < 1000) return sign + String(Number.isInteger(a) ? a : Number(a.toPrecision(3)))
  let p = 0
  while (a >= 1000 && p < kPrefixes.length - 1) {
    a /= 1000
    p++
  }
  let r = Number(a.toPrecision(3))
  if (r >= 1000 && p < kPrefixes.length - 1) {
    r = Number((r / 1000).toPrecision(3)) // 999.9k rounds to 1M, not 1000k
    p++
  }
  return sign + String(r) + kPrefixes[p]
}

// The power of ten of each prefix parseAmount takes, any case.
const kSuffixes: Record<string, number> = { '': 0, k: 3, m: 6, g: 9, t: 12, p: 15 }

// A count typed by a person: digits with optional spaces or _ between groups, an optional
// fraction and an SI prefix (10k, 1.5M, 2T). null when it is not a whole number >= 0 or too big.
// Exact: the fraction is shifted as digits, so 1.1T is 1100000000000, not a float near it.
export function parseAmount(text: string): number | null {
  const m = text.trim().replace(/[\s_\u00a0\u202f]/g, '').match(/^(\d+)(?:\.(\d+))?([kmgtp]?)$/i)
  if (!m) return null
  const shift = kSuffixes[m[3].toLowerCase()]
  const fraction = m[2] ?? ''
  if (/[^0]/.test(fraction.slice(shift))) return null // a part of one: 1.5 or 1.0001k
  const n = Number(m[1] + fraction.slice(0, shift).padEnd(shift, '0'))
  return Number.isSafeInteger(n) ? n : null
}
