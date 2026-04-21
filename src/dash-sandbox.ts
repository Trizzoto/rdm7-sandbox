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
    const theme = this.getAttribute('theme') === 'light' ? 'light' : 'dark';

    this.style.display = 'block';
    this.innerHTML = `
      <style>
        :host, dash-sandbox {
          --sb-bg:       ${theme === 'light' ? '#f4f5f9' : '#0a0e1a'};
          --sb-panel:    ${theme === 'light' ? '#ffffff' : '#141827'};
          --sb-panel-2:  ${theme === 'light' ? '#eef0f7' : '#1b2238'};
          --sb-border:   ${theme === 'light' ? '#dfe3ec' : '#2a3556'};
          --sb-text:     ${theme === 'light' ? '#0d1220' : '#e8edf7'};
          --sb-muted:    ${theme === 'light' ? '#5c6578' : '#9aa7c2'};
          --sb-accent:   #2196F3;
          --sb-accent-2: #3dd8ff;
          --sb-success:  #4ade80;
          --sb-danger:   #ef4444;
          --sb-radius:   14px;
          --sb-bezel:    ${theme === 'light' ? '#1b2238' : '#05070d'};
          font: 14px/1.45 ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
        }
        .dsb-root {
          display: flex; flex-direction: column; gap: 16px;
          max-width: 860px; margin: 0 auto;
          color: var(--sb-text);
        }
        .dsb-device-shell {
          position: relative;
          padding: 18px;
          background: linear-gradient(145deg, var(--sb-bezel), #000);
          border-radius: calc(var(--sb-radius) + 6px);
          box-shadow:
            0 20px 60px rgba(0,0,0,0.45),
            inset 0 1px 0 rgba(255,255,255,0.06);
        }
        .dsb-device {
          position: relative;
          aspect-ratio: 800 / 480;
          background: #000;
          border-radius: var(--sb-radius);
          overflow: hidden;
          box-shadow:
            inset 0 0 0 1px rgba(255,255,255,0.04),
            inset 0 0 24px rgba(0,0,0,0.6);
        }
        .dsb-device canvas {
          display: block; width: 100%; height: 100%;
          touch-action: none; outline: none;
        }
        .dsb-overlay-box {
          position: absolute; inset: 18px;
          border-radius: var(--sb-radius);
          pointer-events: none;
          display: flex; align-items: center; justify-content: center;
          background: radial-gradient(ellipse at center, rgba(0,0,0,0) 40%, rgba(0,0,0,0.6) 100%);
          color: var(--sb-text);
          font-size: 16px;
          opacity: 0;
          transition: opacity .3s ease;
        }
        .dsb-overlay-box.visible { opacity: 1; }
        .dsb-loading, .dsb-error {
          position: absolute; inset: 18px;
          border-radius: var(--sb-radius);
          display: flex; align-items: center; justify-content: center;
          flex-direction: column; gap: 10px;
          color: var(--sb-muted);
          background: rgba(5,7,13,0.85);
          backdrop-filter: blur(4px);
          text-align: center; padding: 24px;
        }
        .dsb-error { color: var(--sb-danger); }
        .dsb-spinner {
          width: 36px; height: 36px;
          border: 3px solid rgba(255,255,255,0.08);
          border-top-color: var(--sb-accent);
          border-radius: 50%;
          animation: dsb-spin 0.9s linear infinite;
        }
        @keyframes dsb-spin { to { transform: rotate(360deg); } }

        .dsb-narration {
          background: var(--sb-panel);
          border: 1px solid var(--sb-border);
          border-radius: var(--sb-radius);
          padding: 16px 20px;
          display: flex; flex-direction: column; gap: 8px;
          min-height: 78px;
        }
        .dsb-title {
          font-size: 11px;
          letter-spacing: 0.12em;
          text-transform: uppercase;
          color: var(--sb-accent-2);
          font-weight: 600;
        }
        .dsb-caption {
          font-size: 16px; line-height: 1.5;
          color: var(--sb-text);
          transition: opacity .25s ease;
          min-height: 22px;
        }
        .dsb-caption.swap { opacity: 0; }

        .dsb-controls {
          display: grid;
          grid-template-columns: auto 1fr auto;
          gap: 16px; align-items: center;
          background: var(--sb-panel);
          border: 1px solid var(--sb-border);
          border-radius: var(--sb-radius);
          padding: 10px 14px;
        }
        .dsb-ctrl-group { display: flex; gap: 6px; }
        .dsb-btn {
          background: var(--sb-panel-2);
          border: 1px solid var(--sb-border);
          color: var(--sb-text);
          border-radius: 10px;
          padding: 8px 14px;
          font: inherit;
          cursor: pointer;
          display: inline-flex; align-items: center; gap: 6px;
          transition: background .15s, border-color .15s, transform .1s;
        }
        .dsb-btn:hover  { background: color-mix(in srgb, var(--sb-panel-2) 85%, white); }
        .dsb-btn:active { transform: translateY(1px); }
        .dsb-btn.primary {
          background: var(--sb-accent); border-color: var(--sb-accent);
          color: #fff;
        }
        .dsb-btn.primary:hover { filter: brightness(1.1); background: var(--sb-accent); }
        .dsb-btn:disabled { opacity: 0.4; cursor: default; }
        .dsb-btn .icon { font-size: 16px; line-height: 1; }

        .dsb-timeline {
          display: flex; gap: 6px; align-items: center;
          flex-wrap: wrap;
        }
        .dsb-dot {
          width: 8px; height: 8px;
          border-radius: 50%;
          background: var(--sb-border);
          cursor: pointer;
          transition: background .15s, transform .15s;
          flex: 0 0 auto;
        }
        .dsb-dot:hover { transform: scale(1.4); background: var(--sb-muted); }
        .dsb-dot.done   { background: var(--sb-muted); }
        .dsb-dot.active { background: var(--sb-accent); transform: scale(1.5); box-shadow: 0 0 8px var(--sb-accent); }

        .dsb-step-label {
          font-variant-numeric: tabular-nums;
          color: var(--sb-muted); font-size: 12px;
          min-width: 44px; text-align: right;
        }
      </style>

      <div class="dsb-root">
        <div class="dsb-device-shell">
          <div class="dsb-device">
            <canvas id="canvas" width="${DEVICE_W}" height="${DEVICE_H}" tabindex="-1"></canvas>
          </div>
          <div class="dsb-loading">
            <div class="dsb-spinner"></div>
            <div>Loading sandbox…</div>
          </div>
          <div class="dsb-error" style="display:none"></div>
        </div>

        <div class="dsb-narration">
          <div class="dsb-title">Guided Tour</div>
          <div class="dsb-caption"></div>
        </div>

        <div class="dsb-controls">
          <div class="dsb-ctrl-group">
            <button class="dsb-btn" data-act="back"    title="Previous step">
              <span class="icon">◀</span>
            </button>
            <button class="dsb-btn primary" data-act="play" title="Play / Pause">
              <span class="icon">▶</span><span class="label">Play</span>
            </button>
            <button class="dsb-btn" data-act="next"    title="Next step">
              <span class="icon">▶</span>
            </button>
            <button class="dsb-btn" data-act="restart" title="Restart from the beginning">
              <span class="icon">↻</span>
            </button>
          </div>
          <div class="dsb-timeline"></div>
          <span class="dsb-step-label">—</span>
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
    this.playBtn     = this.querySelector('[data-act="play"]')    as HTMLButtonElement;
    this.backBtn     = this.querySelector('[data-act="back"]')    as HTMLButtonElement;
    this.nextBtn     = this.querySelector('[data-act="next"]')    as HTMLButtonElement;
    this.restartBtn  = this.querySelector('[data-act="restart"]') as HTMLButtonElement;

    this.overlay = new HighlightOverlay(this.canvas);
    this.overlay.attach(this.querySelector('.dsb-device') as HTMLElement);
    this.voice = createVoice('web-speech');

    this.playBtn.addEventListener('click',    () => this.togglePlay());
    this.backBtn.addEventListener('click',    () => this.runner?.back(this.script!));
    this.nextBtn.addEventListener('click',    () => void this.runner?.next(this.script!));
    this.restartBtn.addEventListener('click', () => void this.runner?.play(this.script!, 0));

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
    const icon  = this.playBtn.querySelector('.icon')!;
    label.textContent = playing ? 'Pause' : 'Play';
    icon.textContent  = playing ? '❚❚'   : '▶';
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
    script.steps.forEach((_s, i) => {
      const dot = document.createElement('div');
      dot.className = 'dsb-dot';
      dot.title = `Step ${i + 1}${_s.voice ? ' — ' + _s.voice : ''}`;
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
