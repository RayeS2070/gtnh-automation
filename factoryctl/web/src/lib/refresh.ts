// Calls `load` every `ms` while the page is visible, and at once when it becomes visible again.
// Returns the stop function, for $effect cleanups.
export function every(ms: number, load: () => void): () => void {
  const tick = () => {
    if (document.visibilityState === 'visible') load()
  }
  const timer = setInterval(tick, ms)
  document.addEventListener('visibilitychange', tick)
  return () => {
    clearInterval(timer)
    document.removeEventListener('visibilitychange', tick)
  }
}

// How often lists ask the server again. Targets are measured by the agent every poll (~10 s);
// other stock changes with the scans (5 min).
export const kRefreshMs = 5000
