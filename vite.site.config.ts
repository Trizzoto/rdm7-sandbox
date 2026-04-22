import { defineConfig } from 'vite';

// Separate config for the Vercel demo site build.
// The main vite.config.ts builds the npm library (dist/sandbox.js).
// This config builds index.html as a plain SPA, copies public/ (WASM),
// and outputs to dist-site/ for Vercel.
export default defineConfig({
  publicDir: 'public',
  build: {
    outDir: 'dist-site',
    emptyOutDir: true,
    target: 'es2020',
    sourcemap: false,
  },
});
