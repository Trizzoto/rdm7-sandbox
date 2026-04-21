import { defineConfig } from 'vite';
import { resolve } from 'path';

/**
 * Dev (npm run dev)  → serves index.html + src/sandbox.ts with HMR.
 * Build (npm run build) → produces dist/sandbox.js for npm publish.
 * Type declarations (.d.ts) are emitted separately by `tsc` in the
 * "build" script chain — see package.json.
 *
 * The WASM artifact is built via scripts/build.ps1 and lives in
 * public/ so Vite serves it untouched at /rdm7-sandbox.wasm.
 */
export default defineConfig({
  publicDir: 'public',

  build: {
    lib: {
      entry: resolve(__dirname, 'src/sandbox.ts'),
      name: 'RDM7Sandbox',
      fileName: 'sandbox',
      formats: ['es'],
    },
    rollupOptions: {
      external: [],
    },
    target: 'es2020',
    sourcemap: true,
    emptyOutDir: true,
  },

  server: {
    port: 5173,
    fs: { allow: ['..'] },
  },
});
