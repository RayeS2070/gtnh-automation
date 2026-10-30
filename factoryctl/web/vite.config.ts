import { svelte } from '@sveltejs/vite-plugin-svelte'
import { defineConfig } from 'vite'

// The built files are served by the factoryctl server (handler-static, web-dir). While
// developing, `npm run dev` serves them and passes the API through to the server.
export default defineConfig({
  plugins: [svelte()],
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
    },
  },
})
