/**
 * pointer-injector.ts — JS side of the virtual LVGL pointer indev.
 *
 * The C side (src/c/pointer_injector.c) latches whatever coordinates we
 * last set; the tutorial-runner can therefore schedule a click as two
 * calls separated by any delay, and LVGL's press→release state machine
 * fires cleanly. Drag is just a series of moves.
 */

import type { SandboxModule } from './wasm-loader';

export class PointerInjector {
  constructor(private mod: SandboxModule) {}

  /** Fire a single tap at device coordinates. Default press duration
   *  120 ms is comfortably above LVGL's click detection threshold and
   *  below the 400 ms long-press threshold. */
  async click(x: number, y: number, pressMs = 120): Promise<void> {
    this.mod._sandbox_set_pointer(x, y, 1);
    await wait(pressMs);
    this.mod._sandbox_set_pointer(x, y, 0);
  }

  /** Interpolated drag from (x1,y1) to (x2,y2) over durationMs. */
  async drag(
    x1: number,
    y1: number,
    x2: number,
    y2: number,
    durationMs = 250,
  ): Promise<void> {
    const steps = Math.max(8, Math.floor(durationMs / 16));
    this.mod._sandbox_set_pointer(x1, y1, 1);
    for (let i = 1; i <= steps; i++) {
      const t = i / steps;
      const x = Math.round(x1 + (x2 - x1) * t);
      const y = Math.round(y1 + (y2 - y1) * t);
      this.mod._sandbox_set_pointer(x, y, 1);
      await wait(durationMs / steps);
    }
    this.mod._sandbox_set_pointer(x2, y2, 0);
  }
}

function wait(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
