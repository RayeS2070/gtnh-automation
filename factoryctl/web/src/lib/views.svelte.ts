import type { EventLevel, StockQuery } from './api'

// The tabs' filters live here, not in the tabs: switching tabs unmounts a tab, and what was
// typed would go with it. A reload of the page starts them over.
export const stockView = $state<StockQuery>({ text: '', kind: 'all', sort: 'label', dir: 'asc' })

export const logsView = $state<{ level: EventLevel; source: string; text: string }>({
  level: 'info',
  source: '',
  text: '',
})
