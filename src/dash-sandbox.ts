/**
 * dash-sandbox.ts — the <dash-sandbox> custom element.
 *
 * Public attributes:
 *   script     URL to a TourScript JSON file. Without it, the sandbox
 *              renders the wizard in free-play mode (no narration).
 *   autoplay   Boolean — run the tour on load.
 *   wasm       Override the WASM URL (default /rdm7-sandbox.wasm).
 *   theme      "dark" (default) | "light" — matches host site styling.
 *
 * The component is a self-contained "TV set" for a Guided Tour: device
 * bezel + screen, caption card, step timeline, transport controls. All
 * styling is inlined so the component works on any host page without
 * requiring a CSS import.
 */

import { loadSandbox, type SandboxModule } from './wasm-loader.js';
import { PointerInjector } from './pointer-injector.js';
import { HighlightOverlay } from './highlight-overlay.js';
import { createVoice, type Voice } from './voiceover.js';
import { TutorialRunner } from './tutorial-runner.js';
import type { TourScript, TourStep } from './tour-types.js';

const DEFAULT_WASM_URL = '/rdm7-sandbox.wasm';
const DEVICE_W = 800;
const DEVICE_H = 480;

/* Inline SVG icons — crisp at all DPRs, scale with font-size via em
 * dims, and inherit stroke colour so themes pick them up automatically. */
const ICON_PLAY = `
<svg class="icon" viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
  <path d="M4 3l9 5-9 5V3z" fill="currentColor"/>
</svg>`;
const ICON_PAUSE = `
<svg class="icon" viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
  <rect x="4" y="3" width="3" height="10" fill="currentColor"/>
  <rect x="9" y="3" width="3" height="10" fill="currentColor"/>
</svg>`;
const ICON_BACK = `
<svg class="icon" viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
  <path d="M11 3L4 8l7 5V3z" fill="currentColor"/>
  <rect x="3" y="3" width="1.5" height="10" fill="currentColor"/>
</svg>`;
const ICON_NEXT = `
<svg class="icon" viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
  <path d="M5 3l7 5-7 5V3z" fill="currentColor"/>
  <rect x="11.5" y="3" width="1.5" height="10" fill="currentColor"/>
</svg>`;
const ICON_RESTART = `
<svg class="icon" viewBox="0 0 16 16" width="14" height="14" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
  <path d="M2 3v4h4"/>
  <path d="M2 7a6 6 0 1 1 1.7 4.2"/>
</svg>`;

/* eslint-disable @typescript-eslint/no-explicit-any */

export class DashSandboxElement extends HTMLElement {
  private canvas!: HTMLCanvasElement;
  private captionEl!: HTMLDivElement;
  private titleEl!: HTMLDivElement;
  private timelineEl!: HTMLDivElement;
  private stepLabelEl!: HTMLSpanElement;
  private playBtn!: HTMLButtonElement;
  private backBtn!: HTMLButtonElement;
  private nextBtn!: HTMLButtonElement;
  private restartBtn!: HTMLButtonElement;
  private loadingEl!: HTMLDivElement;
  private errorEl!: HTMLDivElement;
  private ledEl!: HTMLSpanElement;
  private keyHandler?: (e: KeyboardEvent) => void;

  private overlay!: HighlightOverlay;
  private voice!: Voice;
  private mod?: SandboxModule;
  private runner?: TutorialRunner;
  private script?: TourScript;
  private rafHandle = 0;
  private isPlaying = false;

  connectedCallback() {
    this.buildDom();
    void this.boot();
  }

  disconnectedCallback() {
    if (this.rafHandle) cancelAnimationFrame(this.rafHandle);
    this.runner?.pause();
    this.voice?.dispose?.();
    if (this.keyHandler) {
      window.removeEventListener('keydown', this.keyHandler);
      this.keyHandler = undefined;
    }
    // Emscripten's Modularize mode exposes _free via the Module — we
    // let the GC collect the heap buffer but we at least null our
    // strong refs so SPAs remounting this component don't pile up
    // Module instances. The script <script> tag stays — re-loading
    // it is cheap (browser cache) and avoids double-init if the
    // component reconnects to the same document.
    this.mod = undefined;
    this.runner = undefined;
  }

  /* ── DOM + styles (inlined so the component drops into any host page) ── */

  private buildDom() {
    // Default to light to match studio.realtimedatamonitoring.com.au.
    // Host pages can force the editor-dark aesthetic with theme="dark".
    const theme = this.getAttribute('theme') === 'dark' ? 'dark' : 'light';

    this.style.display = 'block';
    this.innerHTML = `
      <style>
        /* Light-mode palette per the current brief: light-grey page
         * surfaces, dark-grey button bodies, red CTA accent. Fonts
         * (Sora + JetBrains Mono) carry over from the Studio editor
         * so typography feels continuous when this component embeds
         * inside studio.realtimedatamonitoring.com.au.
         *
         * Dark mode retained behind the theme="dark" attribute for
         * host pages that want the original editor-feel (Studio is
         * still dark in its own canvas workspace). */
        :host, dash-sandbox {
          --sb-bg:         ${theme === 'light' ? '#e8e8e8' : '#060606'};
          --sb-surface:    ${theme === 'light' ? '#ffffff' : '#111111'};
          --sb-surface-2:  ${theme === 'light' ? '#f5f5f5' : '#1a1a1a'};
          --sb-panel:      ${theme === 'light' ? '#f0f0f0' : '#303030'};
          --sb-panel-2:    ${theme === 'light' ? '#e2e2e2' : '#393939'};
          --sb-border:     ${theme === 'light' ? '#d0d0d0' : '#1e1e1e'};
          --sb-border-2:   ${theme === 'light' ? '#b0b0b0' : '#555555'};
          --sb-text:       ${theme === 'light' ? '#1a1a1a' : '#e8e8e8'};
          --sb-muted:      ${theme === 'light' ? '#666666' : '#777777'};
          --sb-faint:      ${theme === 'light' ? '#9a9a9a' : '#444444'};
          /* Dark-grey button surfaces — Studio-editor panel tone
           * inverted onto a light page. Text on them is off-white. */
          --sb-btn-bg:     ${theme === 'light' ? '#393939' : '#1a1a1a'};
          --sb-btn-bg-2:   ${theme === 'light' ? '#4a4a4a' : '#303030'};
          --sb-btn-text:   ${theme === 'light' ? '#f5f5f5' : '#e8e8e8'};
          --sb-accent:     #cc0000;
          --sb-accent-hot: #ff1a1a;
          --sb-accent-2:   #2d8ceb;
          --sb-success:    #00cc44;
          --sb-danger:     #cc0000;
          --sb-radius-sm:  2px;
          --sb-radius:     4px;
          --sb-radius-lg:  6px;
          --sb-frame:      ${theme === 'light' ? '#2a2a2a' : '#050505'};
          font-family: 'Sora', ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
          font-size: 14px;
          line-height: 1.45;
          font-weight: 400;
        }
        .dsb-root {
          display: flex; flex-direction: column; gap: 14px;
          max-width: 860px; margin: 0 auto;
          color: var(--sb-text);
        }
        /* Premium device bezel.
         *
         * Built as three nested layers:
         *   shell   → the physical housing (deep shadow + subtle
         *             brushed-metal linear gradient along vertical axis)
         *   bezel   → the black frame between housing and screen
         *             (~14 px, with a top-edge glint + bottom rebate)
         *   screen  → the canvas + a thin glass-reflection overlay
         *
         * On top of the bezel I drop a tiny status LED (red when the
         * tour is playing, faint when paused) and a wordmark so the
         * unit looks like a real dashboard, not a CSS tile. */
        .dsb-device-shell {
          position: relative;
          padding: 8px 8px 8px;
          border-radius: 14px;
          background:
            linear-gradient(180deg, #2e2e2e 0%, #1c1c1c 45%, #242424 55%, #131313 100%);
          box-shadow:
            0 28px 80px -10px rgba(0, 0, 0, 0.55),
            0 2px 4px rgba(0, 0, 0, 0.3),
            inset 0 1px 0 rgba(255, 255, 255, 0.07),
            inset 0 -1px 0 rgba(0, 0, 0, 0.6);
        }
        /* Thin top-edge highlight — gives the housing a cast-aluminium
         * look rather than flat grey plastic. */
        .dsb-device-shell::before {
          content: '';
          position: absolute;
          inset: 0 20% auto 20%;
          height: 1px;
          background: linear-gradient(90deg,
            transparent, rgba(255, 255, 255, 0.15), transparent);
          pointer-events: none;
        }

        .dsb-bezel {
          position: relative;
          padding: 14px 16px 18px;
          border-radius: 10px;
          background:
            linear-gradient(180deg, #0a0a0a 0%, #070707 100%);
          box-shadow:
            inset 0 0 0 1px rgba(255, 255, 255, 0.04),
            inset 0 2px 6px rgba(0, 0, 0, 0.65);
        }

        .dsb-device {
          position: relative;
          aspect-ratio: 800 / 480;
          background: #000;
          border-radius: 3px;
          overflow: hidden;
          box-shadow:
            inset 0 0 0 1px rgba(255, 255, 255, 0.04),
            inset 0 1px 2px rgba(0, 0, 0, 0.5),
            0 0 0 1px rgba(0, 0, 0, 0.7);
        }
        .dsb-device canvas {
          display: block; width: 100%; height: 100%;
          touch-action: none; outline: none;
        }
        /* Subtle glass glint across the top-left quarter of the
         * screen. Non-interactive, low opacity so it doesn't compete
         * with the UI underneath. */
        .dsb-glint {
          position: absolute;
          inset: 0;
          border-radius: 3px;
          pointer-events: none;
          background:
            linear-gradient(115deg,
              rgba(255, 255, 255, 0.045) 0%,
              rgba(255, 255, 255, 0.015) 22%,
              transparent 45%);
          mix-blend-mode: screen;
        }

        /* Dashboard wordmark, lower-right. Monospace tag with the
         * dimensions etched into it — confirms the user is looking at
         * the real 800 × 480 panel, not a resized preview. */
        .dsb-mark {
          position: absolute;
          bottom: 4px; right: 16px;
          color: #3a3a3a;
          font-family: 'JetBrains Mono', ui-monospace, monospace;
          font-size: 9px;
          letter-spacing: 0.3em;
          text-transform: uppercase;
          user-select: none;
          pointer-events: none;
        }
        /* Small LED in the lower-left of the bezel. Red-on when a
         * tour is playing, faint red otherwise. Class toggled in the
         * transport handler. */
        .dsb-led {
          position: absolute;
          bottom: 6px; left: 18px;
          width: 6px; height: 6px;
          border-radius: 50%;
          background: #4a0404;
          box-shadow:
            inset 0 1px 1px rgba(0, 0, 0, 0.5),
            0 0 0 1px #000;
          transition: background .2s, box-shadow .2s;
        }
        .dsb-led.live {
          background: var(--sb-accent-hot);
          box-shadow:
            inset 0 1px 1px rgba(0, 0, 0, 0.3),
            0 0 6px rgba(255, 26, 26, 0.8),
            0 0 0 1px #000;
        }
        /* Loading / error overlays sit on top of the screen only,
         * not the surrounding bezel. Inset matches .dsb-bezel padding
         * so the bezel housing stays visible behind. */
        .dsb-loading, .dsb-error {
          position: absolute; inset: 0;
          border-radius: 3px;
          display: flex; align-items: center; justify-content: center;
          flex-direction: column; gap: 10px;
          color: #e8e8e8;
          background: rgba(0, 0, 0, 0.88);
          text-align: center; padding: 24px;
          font-family: 'JetBrains Mono', ui-monospace, monospace;
          font-size: 12px;
          letter-spacing: 0.05em;
        }
        .dsb-error { color: var(--sb-accent); }
        .dsb-spinner {
          width: 32px; height: 32px;
          border: 2px solid rgba(255, 255, 255, 0.08);
          border-top-color: var(--sb-accent);
          border-radius: 50%;
          animation: dsb-spin 0.9s linear infinite;
        }
        @keyframes dsb-spin { to { transform: rotate(360deg); } }

        /* Narration card: Studio sidebar panel aesthetic — a flat
         * surface with a slim bottom border accent when active. */
        .dsb-narration {
          background: var(--sb-panel);
          border: 1px solid var(--sb-border);
          border-radius: var(--sb-radius);
          padding: 14px 18px;
          display: flex; flex-direction: column; gap: 6px;
          min-height: 74px;
        }
        .dsb-title {
          font-family: 'JetBrains Mono', ui-monospace, monospace;
          font-size: 10px;
          letter-spacing: 0.14em;
          text-transform: uppercase;
          color: var(--sb-muted);
          font-weight: 500;
        }
        .dsb-caption {
          font-size: 15px; line-height: 1.5;
          color: var(--sb-text);
          font-weight: 300;
          transition: opacity .25s ease;
          min-height: 22px;
        }
        .dsb-caption.swap { opacity: 0; }

        /* Transport bar: Studio toolbar colour + mono step label,
         * red primary button with a subtle glow on hover. */
        .dsb-controls {
          display: grid;
          grid-template-columns: auto 1fr auto;
          gap: 14px; align-items: center;
          background: var(--sb-panel);
          border: 1px solid var(--sb-border);
          border-radius: var(--sb-radius);
          padding: 8px 12px;
        }
        .dsb-ctrl-group { display: flex; gap: 4px; }
        .dsb-btn {
          background: var(--sb-btn-bg);
          border: 1px solid transparent;
          color: var(--sb-btn-text);
          border-radius: var(--sb-radius-sm);
          padding: 7px 12px;
          font: inherit;
          font-size: 13px;
          cursor: pointer;
          display: inline-flex; align-items: center; gap: 6px;
          transition: background .15s, box-shadow .15s, transform .1s;
        }
        .dsb-btn:hover  { background: var(--sb-btn-bg-2); }
        .dsb-btn:active { transform: translateY(1px); }
        .dsb-btn.primary {
          background: var(--sb-accent);
          border-color: var(--sb-accent);
          color: #fff;
          font-weight: 500;
        }
        .dsb-btn.primary:hover {
          background: var(--sb-accent-hot);
          border-color: var(--sb-accent-hot);
          box-shadow: 0 0 20px rgba(255, 26, 26, 0.28);
        }
        .dsb-btn:disabled { opacity: 0.35; cursor: default; }
        .dsb-btn .icon { font-size: 15px; line-height: 1; }
        .dsb-btn .label {
          font-family: 'JetBrains Mono', ui-monospace, monospace;
          font-size: 11px; letter-spacing: 0.1em;
          text-transform: uppercase;
        }

        .dsb-timeline {
          display: flex; gap: 5px; align-items: center;
          flex-wrap: wrap;
        }
        .dsb-dot {
          width: 7px; height: 7px;
          border-radius: 50%;
          background: var(--sb-faint);
          cursor: pointer;
          transition: background .15s, transform .15s, box-shadow .15s;
          flex: 0 0 auto;
        }
        .dsb-dot:hover { transform: scale(1.4); background: var(--sb-muted); }
        .dsb-dot.done   { background: var(--sb-muted); }
        .dsb-dot.active {
          background: var(--sb-accent);
          transform: scale(1.6);
          box-shadow: 0 0 8px rgba(204, 0, 0, 0.6);
        }

        .dsb-step-label {
          font-family: 'JetBrains Mono', ui-monospace, monospace;
          font-variant-numeric: tabular-nums;
          color: var(--sb-muted); font-size: 11px;
          letter-spacing: 0.08em;
          min-width: 48px; text-align: right;
        }
      </style>

      <div class="dsb-root">
        <div class="dsb-device-shell">
          <div class="dsb-bezel">
            <div class="dsb-device">
              <canvas id="canvas" width="${DEVICE_W}" height="${DEVICE_H}" tabindex="-1"></canvas>
              <div class="dsb-glint"></div>
              <div class="dsb-loading">
                <div class="dsb-spinner"></div>
                <div>LOADING SANDBOX</div>
              </div>
              <div class="dsb-error" style="display:none"></div>
            </div>
            <span class="dsb-led" aria-hidden="true"></span>
            <span class="dsb-mark">RDM · 7 · 800 × 480</span>
          </div>
        </div>

        <div class="dsb-narration">
          <div class="dsb-title">Guided Tour</div>
          <div class="dsb-caption"></div>
        </div>

        <div class="dsb-controls" role="toolbar" aria-label="Tour transport">
          <div class="dsb-ctrl-group">
            <button class="dsb-btn" data-act="back" title="Previous step (←)" aria-label="Previous step">
              ${ICON_BACK}
            </button>
            <button class="dsb-btn primary" data-act="play" title="Play / Pause (Space)" aria-label="Play">
              ${ICON_PLAY}<span class="label">Play</span>
            </button>
            <button class="dsb-btn" data-act="next" title="Next step (→)" aria-label="Next step">
              ${ICON_NEXT}
            </button>
            <button class="dsb-btn" data-act="restart" title="Restart from the beginning (R)" aria-label="Restart">
              ${ICON_RESTART}
            </button>
          </div>
          <div class="dsb-timeline" role="group" aria-label="Step timeline"></div>
          <span class="dsb-step-label" aria-live="polite">—</span>
        </div>
      </div>
    `;

    this.canvas      = this.querySelector('#canvas') as HTMLCanvasElement;
    this.loadingEl   = this.querySelector('.dsb-loading') as HTMLDivElement;
    this.errorEl     = this.querySelector('.dsb-error') as HTMLDivElement;
    this.titleEl     = this.querySelector('.dsb-title') as HTMLDivElement;
    this.captionEl   = this.querySelector('.dsb-caption') as HTMLDivElement;
    this.timelineEl  = this.querySelector('.dsb-timeline') as HTMLDivElement;
    this.stepLabelEl = this.querySelector('.dsb-step-label') as HTMLSpanElement;
    this.ledEl       = this.querySelector('.dsb-led') as HTMLSpanElement;
    this.playBtn     = this.querySelector('[data-act="play"]')    as HTMLButtonElement;
    this.backBtn     = this.querySelector('[data-act="back"]')    as HTMLButtonElement;
    this.nextBtn     = this.querySelector('[data-act="next"]')    as HTMLButtonElement;
    this.restartBtn  = this.querySelector('[data-act="restart"]') as HTMLButtonElement;

    this.overlay = new HighlightOverlay(this.canvas);
    this.overlay.attach(this.querySelector('.dsb-device') as HTMLElement);
    this.voice = createVoice();

    this.playBtn.addEventListener('click',    () => this.togglePlay());
    this.backBtn.addEventListener('click',    () => this.runner?.back(this.script!));
    this.nextBtn.addEventListener('click',    () => void this.runner?.next(this.script!));
    this.restartBtn.addEventListener('click', () => void this.runner?.play(this.script!, 0));

    // Keyboard shortcuts. Only active while the pointer is inside
    // this component so the sandbox doesn't hijack the host page's
    // global keybindings. Space / arrows / R — familiar video-player
    // ergonomics.
    let focused = false;
    this.addEventListener('pointerenter', () => { focused = true; });
    this.addEventListener('pointerleave', () => { focused = false; });
    this.keyHandler = (e: KeyboardEvent) => {
      if (!focused || !this.runner || !this.script) return;
      if (e.target instanceof HTMLElement && /INPUT|TEXTAREA|SELECT/.test(e.target.tagName)) return;
      switch (e.key) {
        case ' ':        e.preventDefault(); this.togglePlay(); break;
        case 'ArrowLeft':  e.preventDefault(); this.runner.back(this.script); break;
        case 'ArrowRight': e.preventDefault(); void this.runner.next(this.script); break;
        case 'r': case 'R': e.preventDefault(); void this.runner.play(this.script, 0); break;
      }
    };
    window.addEventListener('keydown', this.keyHandler);

    this.setTransportEnabled(false);
  }

  /* ── Boot sequence ───────────────────────────────────────────────────── */

  private async boot() {
    const wasmUrl = this.resolveWasmUrl();

    try {
      this.mod = await loadSandbox(wasmUrl, this.canvas, this.hasAttribute('debug'));
    } catch (err) {
      this.showError(`Failed to load sandbox: ${(err as Error).message}`);
      return;
    }

    this.loadingEl.style.display = 'none';
    this.startRaf();

    const scriptUrl = this.getAttribute('script');
    if (!scriptUrl) {
      this.titleEl.textContent = 'Free-play mode';
      this.captionEl.textContent = 'Use your mouse on the device to interact.';
      return;
    }

    try {
      const resp = await fetch(scriptUrl);
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      this.script = (await resp.json()) as TourScript;
    } catch (err) {
      this.showError(`Failed to load tour: ${(err as Error).message}`);
      return;
    }

    this.titleEl.textContent = this.script.title ?? 'Guided Tour';
    this.buildTimeline(this.script);

    const pointer = new PointerInjector(this.mod);
    this.runner = new TutorialRunner(pointer, this.overlay, this.voice, this.mod, {
      onCaption: (c) => this.setCaption(c),
      onStep:    (i, s) => this.handleStepChange(i, s),
      onEnd:     () => this.handleTourEnd(),
    });

    this.setTransportEnabled(true);
    this.captionEl.textContent = 'Press Play to begin.';

    // Expose for external automation (tests, console debugging).
    (this as any).runner = this.runner;
    (this as any).script = this.script;

    if (this.hasAttribute('autoplay')) void this.togglePlay();
  }

  /** Hot-swap the active tour without reloading the WASM module.
   *  Called by host pages that offer a tour picker. */
  async setScript(url: string): Promise<void> {
    if (!this.mod) throw new Error('Sandbox not booted yet');
    this.runner?.pause();
    this.setPlayLabel(false);

    try {
      const resp = await fetch(url);
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      this.script = (await resp.json()) as TourScript;
    } catch (err) {
      this.showError(`Failed to load tour: ${(err as Error).message}`);
      return;
    }

    this.titleEl.textContent = this.script.title ?? 'Guided Tour';
    this.buildTimeline(this.script);
    this.captionEl.textContent = 'Press Play to begin.';
    this.overlay.hide();

    // Re-instantiate the runner so index + lastScene reset cleanly.
    const pointer = new PointerInjector(this.mod);
    this.runner = new TutorialRunner(pointer, this.overlay, this.voice, this.mod, {
      onCaption: (c) => this.setCaption(c),
      onStep:    (i, s) => this.handleStepChange(i, s),
      onEnd:     () => this.handleTourEnd(),
    });
    (this as any).runner = this.runner;
    (this as any).script = this.script;

    // Reset the wizard to its fresh-boot scene so every tour starts
    // from the same visual baseline.
    this.mod._sandbox_set_scene(0);
  }

  /** Resolve the `wasm=` attribute into a safe URL. Only same-origin
   *  paths and explicit data: blobs are allowed — cross-origin would
   *  be an open script-injection hole, since wasm-loader.ts appends a
   *  <script> tag to document.head with this URL. */
  private resolveWasmUrl(): string {
    const raw = this.getAttribute('wasm') ?? DEFAULT_WASM_URL;
    try {
      const u = new URL(raw, window.location.href);
      const sameOrigin = u.origin === window.location.origin;
      if (!sameOrigin && u.protocol !== 'data:') {
        console.warn(`[dash-sandbox] Refusing cross-origin wasm URL "${raw}"; using default`);
        return DEFAULT_WASM_URL;
      }
      return u.toString();
    } catch {
      console.warn(`[dash-sandbox] Invalid wasm URL "${raw}"; using default`);
      return DEFAULT_WASM_URL;
    }
  }

  /* ── UI helpers ──────────────────────────────────────────────────────── */

  private setTransportEnabled(on: boolean) {
    for (const b of [this.playBtn, this.backBtn, this.nextBtn, this.restartBtn]) {
      b.disabled = !on;
    }
  }

  private setPlayLabel(playing: boolean) {
    this.isPlaying = playing;
    const label = this.playBtn.querySelector('.label')!;
    const oldIcon = this.playBtn.querySelector('svg.icon');
    label.textContent = playing ? 'Pause' : 'Play';
    this.playBtn.setAttribute('aria-label', playing ? 'Pause' : 'Play');
    if (oldIcon) {
      // Replace the first svg in place; keep the <span class="label">
      // as-is so layout doesn't jump.
      oldIcon.outerHTML = playing ? ICON_PAUSE : ICON_PLAY;
    }
    this.ledEl?.classList.toggle('live', playing);
  }

  private setCaption(text: string) {
    // Cross-fade the caption. Using a class + rAF double-buffer avoids
    // visible text flashes when two steps land close together.
    this.captionEl.classList.add('swap');
    requestAnimationFrame(() => {
      this.captionEl.textContent = text;
      requestAnimationFrame(() => this.captionEl.classList.remove('swap'));
    });
  }

  private buildTimeline(script: TourScript) {
    this.timelineEl.innerHTML = '';
    script.steps.forEach((step, i) => {
      const dot = document.createElement('div');
      dot.className = 'dsb-dot';
      dot.setAttribute('role', 'button');
      dot.setAttribute('aria-label',
        `Step ${i + 1} of ${script.steps.length}${step.voice ? ': ' + step.voice.slice(0, 60) : ''}`);
      // Tooltip: step number + voice preview. Browser renders its
      // native title — low-effort, keyboard-reachable via focus.
      const preview = step.voice ? `\n${step.voice}` : '';
      dot.title = `Step ${i + 1} of ${script.steps.length}${preview}`;
      dot.addEventListener('click', () => {
        if (!this.runner || !this.script) return;
        // Jump semantics: ensure scene matches target by triggering
        // a one-step playback, then pause immediately after the first
        // runStep completes. The runner handles scene-sync via
        // sceneAt() in play().
        void this.runner.play(this.script, i);
        this.setPlayLabel(true);
      });
      this.timelineEl.appendChild(dot);
    });
    this.refreshTimeline(0, script.steps.length);
  }

  private refreshTimeline(activeIndex: number, total: number) {
    const dots = this.timelineEl.querySelectorAll<HTMLDivElement>('.dsb-dot');
    dots.forEach((d, i) => {
      d.classList.toggle('active', i === activeIndex);
      d.classList.toggle('done', i <  activeIndex);
    });
    this.stepLabelEl.textContent = `${activeIndex + 1} / ${total}`;
  }

  private handleStepChange(index: number, step: TourStep) {
    if (step.voice) this.setCaption(step.voice);
    if (this.script) this.refreshTimeline(index, this.script.steps.length);
  }

  private handleTourEnd() {
    this.setCaption('Tour complete.');
    this.setPlayLabel(false);
  }

  private async togglePlay() {
    if (!this.runner || !this.script) return;
    if (this.isPlaying) {
      this.runner.pause();
      this.setPlayLabel(false);
      return;
    }
    this.setPlayLabel(true);
    try {
      await this.runner.play(this.script, (this.runner as any).index ?? 0);
    } finally {
      this.setPlayLabel(false);
    }
  }

  private startRaf() {
    const loop = () => {
      this.mod?._sandbox_step();
      this.rafHandle = requestAnimationFrame(loop);
    };
    this.rafHandle = requestAnimationFrame(loop);
  }

  private showError(msg: string) {
    this.loadingEl.style.display = 'none';
    this.errorEl.style.display = 'flex';
    this.errorEl.textContent = msg;
  }
}
