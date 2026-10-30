// Who orders: typed once, remembered in this browser (there are no accounts).
const kKey = 'factoryctl.requestedBy'

export function loadName(): string {
  try {
    return localStorage.getItem(kKey) ?? ''
  } catch {
    return '' // storage blocked: just ask every time
  }
}

export function saveName(name: string): void {
  try {
    localStorage.setItem(kKey, name)
  } catch {
    // storage blocked: nothing to keep
  }
}
