# factoryctl web client

Svelte 5 + TypeScript, built with Vite into `dist/`, which the factoryctl server serves
(`handler-static`, `web-dir` in `server/configs/config_vars.yaml`).

```sh
npm install
npm run dev     # http://localhost:5173, /api goes to the development server on :8081
npm run check   # types
npm run build   # dist/ for the server
```
