import { fetchMe } from './api'

// Who orders. Behind the proxy the server knows the person (and takes it, whatever the page
// sends); without it the name is typed once and remembered in this browser.
const kKey = 'factoryctl.requestedBy'

let me: Promise<string | null> | undefined

// Asked once per page load.
export function signedIn(): Promise<string | null> {
  me ??= fetchMe().catch(() => null)
  return me
}

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
