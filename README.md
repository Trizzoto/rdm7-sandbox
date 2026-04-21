# @rdm7/sandbox

> **Tutorial-grade emulator for the RDM-7 Dash firmware — runs in your browser.**

An interactive WebAssembly port of the RDM-7 Dash first-run wizard UI, paired with a **Guided Tour** runtime (AI voiceover + scripted pointer injection + highlight overlays). Replaces recorded user-guide videos for [RDM Web Studio](https://studio.realtimedatamonitoring.com.au/).

<p align="center">
  <em>Your customer never needs to own the device to see how it's set up.</em>
</p>

---

## ✨ Features

- **Pixel-matched wizard UI** — CAN bus scan (4 bitrates, animated), ECU picker (Haltech / Link / MoTeC / MaxxECU / MS3-Pro / Ford / Custom), WiFi scan + connect, Hotspot fallback explanation
- **Guided Tours** — JSON-authored walkthroughs with narration (Web Speech API or pre-baked MP3), cyan pulse highlights, and scripted clicks
- **Scene-synced Back / Next** — rewinding the narration rewinds the LVGL render, not just the caption
- **Drop-in web component** — `<dash-sandbox script="first-boot.json" />` in any Next.js / vanilla / Tauri page
- **Tiny footprint** — ~1.3 MB WASM + 370 KB JS, loads lazily on demand
- **Zero firmware coupling** — hand-rolled LVGL clone, so changes to the real firmware don't break the sandbox (and vice versa)

---

## 🎯 Why this exists

Recorded videos for onboarding are expensive to produce, instantly go stale when the UI changes, and can't show *interaction* — only playback. A customer watching a video can't hover-to-pause, scrub to the part they missed, or see what happens when they tap a button.

This sandbox fixes that. The dash *actually runs* in the browser. The tour *actually drives* the wizard via scripted pointer events the firmware processes through its normal LVGL input pipeline. When you advance a step, the user sees the real wizard respond.

---

## 🚀 Try it

Requires Node 18+, Git, and (for rebuilding the WASM) [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).

```bash
git clone https://github.com/Trizzoto/rdm7-sandbox.git
cd rdm7-sandbox

./scripts/setup-submodules.ps1   # LVGL + lv_drivers (one-time)
./scripts/build.ps1              # Compile WASM (requires emsdk)
npm install
npm run dev                      # http://localhost:5173
```

The build scripts are currently **PowerShell-only** (targeting the primary developer's Windows environment). Linux / macOS support is tracked — the actual commands are short enough to port by hand if you need to build today:

```bash
# emsdk-activated shell
mkdir -p build && cd build
emcmake cmake .. -G "Unix Makefiles"
emmake make rdm7-sandbox -j$(nproc)
cp rdm7-sandbox.{js,wasm} ../public/
```

---

## 📦 Embed in your site

```bash
npm install @rdm7/sandbox
```

```tsx
// Any framework that can render a Web Component (React, Vue, Svelte, vanilla):
import '@rdm7/sandbox';

export default function Onboarding() {
  return (
    <dash-sandbox
      script="/tours/first-boot.json"
      wasm="/rdm7-sandbox.wasm"
      autoplay
    />
  );
}
```

Ship `rdm7-sandbox.wasm` + `rdm7-sandbox.js` (from the package's `public/` directory) alongside your site. Vite, Next.js, and Nuxt all serve static assets from `/public/` by default.

### Attributes

| Attribute  | Type    | Default                 | Purpose                                                    |
|------------|---------|-------------------------|------------------------------------------------------------|
| `script`   | URL     | (none)                  | Tour JSON. Without it, the sandbox renders in free-play.   |
| `wasm`     | URL     | `/rdm7-sandbox.wasm`    | Override the WASM asset location.                          |
| `autoplay` | boolean | `false`                 | Start the tour on page load.                               |
| `theme`    | enum    | `"dark"`                | `"dark"` or `"light"` — matches your host page styling.    |

### Content Security Policy note

`<dash-sandbox>` boots the WASM module by dynamically injecting a `<script>` tag at `wasm=`. Sites with a strict CSP (`script-src 'self'`) work out of the box because the default URL is same-origin. If you override `wasm=` with a CDN URL, ensure your `script-src` directive includes that origin, or self-host the artifact. Cross-origin `wasm=` values are refused at runtime and fall back to the default.

### JS API

```js
const sb = document.querySelector('dash-sandbox');
await sb.setScript('/tours/quick.json');  // hot-swap tours without reboot
sb.runner.play(sb.script, 0);              // programmatic control
sb.runner.pause();
sb.runner.next(sb.script);
sb.runner.back(sb.script);
```

---

## ✍️ Authoring tours

A tour is a JSON array of steps. Each step can speak, highlight a region, inject a click, and tell the wizard what scene to be in. See **[TOUR_FORMAT.md](./docs/TOUR_FORMAT.md)** for the full spec.

Minimal example:

```json
{
  "id": "hello",
  "title": "30-second hello",
  "steps": [
    { "scene": "step1_done", "voice": "Scan complete — here's 500 kilobits.", "wait": 3000 },
    { "voice": "Apply to continue.", "click": [400, 313], "delay": 1500 },
    { "scene": "step2",      "voice": "Pick your ECU.", "wait": 3000 }
  ]
}
```

---

## 🏗️ Architecture

```
┌── RDM Web Studio / docs site / your landing page ──────┐
│   <dash-sandbox script="first-boot.json" />            │
├── @rdm7/sandbox web component (TypeScript) ────────────┤
│   tutorial-runner  ·  highlight overlay  ·  voiceover  │
│   pointer-injector (JS ⇄ virtual LVGL indev)           │
│   wasm-loader (Emscripten factory bridge)              │
├── rdm7-sandbox.wasm (emcc-compiled C) ─────────────────┤
│   LVGL v8.3  ·  SDL2 canvas driver                     │
│   wizard_sandbox.c  (pure LVGL, firmware-matched)      │
│   esp_idf_shim (NVS/FreeRTOS/timer → no-ops)           │
└────────────────────────────────────────────────────────┘
```

Key design decisions:

- **Hand-rolled wizard vs. firmware source port.** The firmware's `first_run_wizard.c` transitively pulls in 20+ ESP-IDF subsystems (TWAI, WiFi manager, NVS, web server, widget registry...) — stubbing all of that would balloon the build. Instead, `wizard_sandbox.c` replicates the visual + behavioural language in ~550 lines of pure LVGL, with colours, fonts, and dimensions copied verbatim from [theme.h](https://github.com/Trizzoto/rdm-7_Dash/blob/master/main/ui/theme.h).
- **Scene-based sync.** Tour steps declare a `scene` (`step1`, `step1_done`, `step2`, `step3`, `wifi_picker`, `wifi_connected`) so Back / Next rewind the canvas in lockstep with the audio — no "caption says step 2 but visuals show step 3" glitches.
- **Virtual pointer indev.** Same pattern as the firmware's [remote_touch.c](https://github.com/Trizzoto/rdm-7_Dash/blob/master/main/system/remote_touch.c): scripted clicks are latched into a virtual LVGL pointer, with deferred-release so a same-frame down+up still registers as a click.
- **Web Speech first, ElevenLabs later.** The voiceover adapter is pluggable. v1 ships with free browser TTS; swap in pre-baked MP3s for production polish without changing any tour scripts.

---

## 🧪 Development

```bash
npm run dev         # Vite dev server with HMR
npm run build       # Produces dist/ for npm publishing
npm run typecheck   # tsc --noEmit
npm run build-wasm  # Rebuilds the C → WASM artifact (emsdk required)
```

Firmware C sources aren't committed — `scripts/sync-firmware.ps1` pulls them on demand from the local `../RDM-7_Dash/` checkout and records SHAs in `firmware-sources.lock.json` for drift detection.

### Project layout

```
rdm7-sandbox/
├── src/                   TypeScript web component + tour runtime
│   ├── dash-sandbox.ts     the <dash-sandbox> custom element
│   ├── tutorial-runner.ts  step sequencer with scene sync
│   ├── highlight-overlay.ts SVG pulse rings
│   ├── pointer-injector.ts JS → virtual LVGL pointer
│   ├── voiceover.ts        Web Speech adapter (ElevenLabs-ready)
│   ├── wasm-loader.ts      emcc factory wrapper
│   └── c/                 C sources (compiled to WASM)
│       ├── main_sandbox.c
│       ├── hal_init.c
│       ├── pointer_injector.c
│       └── wizard_sandbox.c
├── stubs/                 ESP-IDF shim headers + small impl
├── tours/
│   ├── manifest.json       tour registry
│   ├── first-boot.json     full walkthrough (~45s)
│   └── quick.json          marketing-grade condensed tour (~30s)
├── scripts/               setup / sync / build
└── docs/
    ├── TOUR_FORMAT.md      authoring spec
    └── images/             hero + feature screenshots
```

---

## 🗺️ Roadmap

- [x] First-run wizard scenes (CAN scan, ECU picker, WiFi, Connect)
- [x] Tour runtime (runner, scene sync, highlights, Web Speech voiceover)
- [x] Tour picker + multiple tours
- [ ] Device Settings scenes — second wizard-sized C module
- [ ] Layout editor demo — widgets rendering with mock signals
- [ ] ElevenLabs pre-baked MP3 voiceover adapter
- [ ] Mobile gesture support (pinch-zoom the device preview)
- [ ] Interactive docs site — MDX pages embedding `<dash-sandbox>` inline

---

## 📄 License

Same as the parent RDM-7 firmware repo. See [LICENSE](./LICENSE).

---

<p align="center">
  <sub>Part of the <a href="https://github.com/Trizzoto/rdm-7_Dash">RDM-7 project</a> — firmware, Studio, Marketplace, Sandbox.</sub>
</p>
