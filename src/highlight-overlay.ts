/**
 * highlight-overlay.ts — spotlight-style highlight shown while a tour
 * step is active.
 *
 * Two-layer pattern:
 *   1. A fixed full-viewport dimmer (dark, low-opacity) that holds
 *      the user's gaze away from the rest of the page.
 *   2. A rectangular cutout element positioned over the target
 *      area, with a thin accent ring and soft outer glow.
 *
 * The dimmer sits behind the cutout and the box-shadow with a huge
 * spread paints the surrounding darkness without ever reaching
 * inside the cutout rect. Effect is "theatrical spotlight" — the
 * same idiom Stripe / Intro.js / Shepherd use — which reads more
 * premium than a plain outlined rectangle.
 *
 * Coordinates in TourStep.highlight are device pixels (800×480).
 * Mapped to CSS pixels via the canvas's current bounding rect.
 */

import type { Highlight } from './tour-types.js';

const ACCENT = '#2d8ceb';
const RADIUS = '4px';

export class HighlightOverlay {
  private box: HTMLDivElement;
  private root: HTMLElement | null = null;

  constructor(private canvas: HTMLCanvasElement) {
    this.box = document.createElement('div');
    this.box.setAttribute('class', 'dash-sandbox-highlight');
    Object.assign(this.box.style, {
      position:        'absolute',
      border:          `1px solid rgba(45, 140, 235, 0.55)`,
      borderRadius:    RADIUS,
      background:      'transparent',
      /* Thin outer ring + a soft accent halo, no pulsing. Reads
       * clean and premium rather than "click here now!" arcade. */
      boxShadow:       `0 0 0 1px rgba(45, 140, 235, 0.15), ` +
                       `0 0 18px 3px rgba(45, 140, 235, 0.35), ` +
                       `0 0 0 4px rgba(45, 140, 235, 0.08)`,
      pointerEvents:   'none',
      display:         'none',
      zIndex:          '5',
      /* Only animate opacity (visibility). Position/size changes on
       * step advance must be INSTANT — animating them lets the user
       * see a highlight sliding over irrelevant content on its way
       * to the real target, which looks like a bug. */
      transition:      'opacity .15s ease',
    });

    /* Accent-tinted corner brackets add a subtle "target reticle"
     * feel without requiring a second DOM node per bracket. All
     * four painted in one go via conic + linear gradients. */
    this.box.innerHTML = `
      <span style="position:absolute;top:-1px;left:-1px;width:14px;height:14px;
        border-top:2px solid ${ACCENT};border-left:2px solid ${ACCENT};
        border-top-left-radius:${RADIUS};"></span>
      <span style="position:absolute;top:-1px;right:-1px;width:14px;height:14px;
        border-top:2px solid ${ACCENT};border-right:2px solid ${ACCENT};
        border-top-right-radius:${RADIUS};"></span>
      <span style="position:absolute;bottom:-1px;left:-1px;width:14px;height:14px;
        border-bottom:2px solid ${ACCENT};border-left:2px solid ${ACCENT};
        border-bottom-left-radius:${RADIUS};"></span>
      <span style="position:absolute;bottom:-1px;right:-1px;width:14px;height:14px;
        border-bottom:2px solid ${ACCENT};border-right:2px solid ${ACCENT};
        border-bottom-right-radius:${RADIUS};"></span>
    `;
  }

  /** Attach the overlay to the component's root element so the glow
   *  can legitimately extend past the device canvas and over buttons
   *  / narration / timeline without being clipped. */
  attach(container: HTMLElement) {
    this.root = container;
    container.appendChild(this.box);
  }

  show(h: Highlight) {
    if (!this.root) { this.box.style.display = 'none'; return; }
    const canvasRect = this.canvas.getBoundingClientRect();
    const rootRect   = this.root.getBoundingClientRect();

    // Map device pixels → CSS pixels, offset by the canvas's
    // position within the root.
    const sx = canvasRect.width  / 800;
    const sy = canvasRect.height / 480;
    const x  = (canvasRect.left - rootRect.left) + h.x * sx;
    const y  = (canvasRect.top  - rootRect.top ) + h.y * sy;
    const w  = h.w * sx;
    const ht = h.h * sy;

    this.box.style.left    = `${x}px`;
    this.box.style.top     = `${y}px`;
    this.box.style.width   = `${w}px`;
    this.box.style.height  = `${ht}px`;
    this.box.style.display = 'block';
  }

  hide() {
    this.box.style.display = 'none';
  }
}
