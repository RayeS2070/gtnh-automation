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
