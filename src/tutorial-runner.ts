/**
 * tutorial-runner.ts — drives a Guided Tour script against the sandbox.
 *
 * Each step can speak, highlight, click, drag, or wait. The runner owns
 * the sequencing; the web component exposes play / pause / next / back
 * wrappers on top of it.
 */

import type { TourScript, TourStep, WizardScene, ScrollSection } from './tour-types.js';
import type { PointerInjector } from './pointer-injector.js';
import type { HighlightOverlay } from './highlight-overlay.js';
import type { Voice } from './voiceover.js';
import type { SandboxModule } from './wasm-loader.js';

/** Scene name → numeric code shared with wizard_sandbox.c's SCENE_* defines.
 *  Changing these requires matching changes in C; integer-keyed so the
 *  C side doesn't need a string parser. */
export const SCENE_MAP: Record<WizardScene, number> = {
  step1:            0,
  step1_done:       1,
  step2:            2,
  step3:            3,
  wifi_picker:      4,
  wifi_connected:   5,
  dashboard:        6,
  dashboard_menu:   7,
  setup_menu:       8,
  device_settings:  9,
  widget_config:   10,
  wifi_settings:   11,
  peaks:           12,
  diagnostics:     13,
};

export interface TutorialEvents {
  onStep?(index: number, step: TourStep): void;
  onCaption?(caption: string): void;
  onEnd?(): void;
}

export class TutorialRunner {
  private index = 0;
  private running = false;
  private cancelToken = 0;
  private lastScene: WizardScene | null = null;

  constructor(
    private pointer: PointerInjector,
    private overlay: HighlightOverlay,
    private voice: Voice,
    private mod: SandboxModule,
    private events: TutorialEvents = {},
  ) {}

  async play(script: TourScript, startAt = 0): Promise<void> {
    this.running = true;
    this.index = Math.max(0, Math.min(startAt, script.steps.length - 1));
    const token = ++this.cancelToken;

    /* When seeking (startAt > 0) or rewinding, derive the correct scene
     * by scanning BACKWARD from the target step for the nearest explicit
     * `scene`. Without this, Back would leave the LVGL render stuck on
     * whatever state it advanced to previously. */
    const seekScene = this.sceneAt(script, this.index);
    if (seekScene) this.applyScene(seekScene);

    while (this.running && this.index < script.steps.length && token === this.cancelToken) {
      const step = script.steps[this.index];
      if (step.scene) this.applyScene(step.scene);
      this.events.onStep?.(this.index, step);
      await this.runStep(step, token);
      if (token !== this.cancelToken) return;
      this.index++;
    }

    if (token === this.cancelToken) {
      this.overlay.hide();
      this.events.onEnd?.();
      this.running = false;
    }
  }

  /** Walk backward from `idx` to find the most recently declared scene.
   *  Steps that omit `scene` inherit from the previous step — so a chunk
   *  of captions describing the same screen don't need to repeat it. */
  private sceneAt(script: TourScript, idx: number): WizardScene | null {
    for (let i = idx; i >= 0; i--) {
      const s = script.steps[i]?.scene;
      if (s) return s;
    }
    return null;
  }

  private applyScene(scene: WizardScene) {
    if (scene === this.lastScene) return;
    this.lastScene = scene;
    const code = SCENE_MAP[scene];
    if (typeof code === 'number') this.mod._sandbox_set_scene(code);
  }

  /** Clear the cached scene so the next applyScene() call always
   *  forwards to set_scene. Required when a host bypasses the runner
   *  to set a scene directly (e.g. scroll-driven idle / diagnostics);
   *  without this, scrolling back into a tour leaves the dash stuck on
   *  the last externally-set scene because applyScene short-circuits. */
  public invalidateSceneCache(): void {
    this.lastScene = null;
  }

  pause() {
    this.cancelToken++;
    this.voice.cancel();
    this.running = false;
  }

  async next(script: TourScript) {
    this.pause();
    await this.play(script, this.index + 1);
  }

  /** Rewind one step and PAUSE. Clicks in tour steps are state-changing
   *  (they tap device buttons that advance the wizard), so auto-replay
   *  on Back would re-fire them and leapfrog back to where we just
   *  left. Instead we jump the scene, set the caption/highlight for
   *  the target step, and wait for the user to hit Play again. */
  async back(script: TourScript) {
    await this.jumpTo(script, this.index - 1);
  }

  /** Jump to an arbitrary step and PAUSE — used by scroll-driven hosts.
   *  Same rationale as `back()`: we never want to re-execute clicks /
   *  drags / voice as a side effect of the user just scrolling past a
   *  section. Apply the scene, paint the highlight, fire the caption
   *  + step events so chrome can mirror the position; that's it. */
  async jumpTo(script: TourScript, target: number): Promise<void> {
    this.pause();
    const idx = Math.max(0, Math.min(target, script.steps.length - 1));
    this.index = idx;
    const step = script.steps[idx];
    if (!step) return;
    const scene = this.sceneAt(script, idx);
    if (scene) this.applyScene(scene);
    if (step.scrollSection && this.mod._sandbox_device_settings_scroll) {
      this.mod._sandbox_device_settings_scroll(this.scrollSectionId(step.scrollSection));
    }
    if (step.highlight) this.overlay.show(step.highlight);
    else this.overlay.hide();
    if (step.voice || step.caption) this.events.onCaption?.(step.caption ?? step.voice ?? '');
    this.events.onStep?.(idx, step);
  }

  /** Map a tour-script scroll target onto the DS_SECTION_* id the
   *  C layer exposes. Kept here (not in the C header) so tour
   *  authors get autocomplete over meaningful names. */
  private scrollSectionId(s: ScrollSection): number {
    switch (s) {
      case 'top':      return 0;
      case 'network':  return 1;
      case 'logging':  return 2;
      case 'can_diag': return 3;
      case 'footer':   return 4;
      case 'display':  return 5;
      case 'peaks':    return 6;
    }
  }

  private async runStep(step: TourStep, token: number): Promise<void> {
    if (step.scrollSection && this.mod._sandbox_device_settings_scroll) {
      this.mod._sandbox_device_settings_scroll(this.scrollSectionId(step.scrollSection));
    }
    if (step.highlight) this.overlay.show(step.highlight);
    else this.overlay.hide();

    /* Caption path: explicit caption wins, otherwise voice doubles
     * as the visible caption. (Voice playback is silent — see
     * voiceover.ts — so without this fallback, host chrome that
     * relies on the `caption` event never updates during autoplay.) */
    const captionText = step.caption ?? step.voice ?? null;
    if (captionText !== null) this.events.onCaption?.(captionText);

    const voicePromise = step.voice ? this.voice.speak(step.voice) : Promise.resolve();

    if (step.click) {
      // Let the user hear the instruction before the click happens,
      // so the highlight has a moment to be noticed. Half the voice line
      // feels right in testing; falls back to 800 ms if no voice.
      await wait(step.voice ? Math.min(1200, step.voice.length * 35) : 800);
      if (token !== this.cancelToken) return;
      await this.pointer.click(step.click[0], step.click[1]);
    }

    if (step.drag) {
      await wait(500);
      if (token !== this.cancelToken) return;
      await this.pointer.drag(
        step.drag.from[0],
        step.drag.from[1],
        step.drag.to[0],
        step.drag.to[1],
        step.drag.durationMs ?? 300,
      );
    }

    await voicePromise;
    if (step.delay) await wait(step.delay);
    if (step.wait) await wait(step.wait);
  }
}

function wait(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}
