import { svelte } from '@sveltejs/vite-plugin-svelte'
import { defineConfig } from 'vite'

// The built files are served by the factoryctl server (handler-static, web-dir). While
// developing, `npm run dev` serves them and passes the API and the icon data through to the server.
export default defineConfig({
  plugins: [svelte()],
  build: {
    // Write over dist/ instead of deleting it: the server watches dist/ with inotify, and a
    // deleted directory ends the watch, so later builds went unseen until a restart. Old bundles
    // pile up in dist/assets (tens of KB each); `npm run clean` and a restart clear them.
    emptyOutDir: false,
  },
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
      '/data': 'http://localhost:8080',
    },
  },
})
