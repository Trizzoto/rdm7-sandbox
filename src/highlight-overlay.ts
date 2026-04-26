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

/* Amber highlight matched to Claude Design's RDM-7 Sandbox - C bundle.
 * The design's `dashc-gauge.is-hl` style uses #F2C14E with a soft outer
 * halo. We pad the rectangle slightly so the glow reads as a deliberate
 * "spotlight on this widget" rather than a tight outline that just hugs
 * the pixel bounds. */
const ACCENT     = '#F2C14E';
const RADIUS     = '6px';
const PAD_PX     = 10;     // visual breathing room around the target rect

export class HighlightOverlay {
  private box: HTMLDivElement;
  private root: HTMLElement | null = null;

  constructor(private canvas: HTMLCanvasElement) {
    this.box = document.createElement('div');
    this.box.setAttribute('class', 'dash-sandbox-highlight');
    Object.assign(this.box.style, {
      position:        'absolute',
      border:          `2px solid ${ACCENT}`,
      borderRadius:    RADIUS,
      background:      'transparent',
      /* Layered halo: a 2px inner ring for crisp definition, then a
       * fat 56px soft glow, then a wider faint diffusion that hints
       * "look here" without overpowering the underlying widget. */
      boxShadow:       `0 0 0 2px ${ACCENT}, ` +
                       `0 0 56px -2px rgba(242, 193, 78, 0.65), ` +
                       `0 0 0 10px rgba(242, 193, 78, 0.10)`,
      pointerEvents:   'none',
      display:         'none',
      zIndex:          '5',
      /* Only animate opacity (visibility). Position/size changes on
       * step advance must be INSTANT — animating them lets the user
       * see a highlight sliding over irrelevant content on its way
       * to the real target, which looks like a bug. */
      transition:      'opacity .15s ease',
    });

    /* Amber corner brackets — subtle reticle that reinforces the
     * spotlight without overpowering the underlying widget. */
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
    // position within the root, then expand by PAD_PX on every side
    // so the glow has breathing room around the target widget.
    const sx = canvasRect.width  / 800;
    const sy = canvasRect.height / 480;
    const x  = (canvasRect.left - rootRect.left) + h.x * sx - PAD_PX;
    const y  = (canvasRect.top  - rootRect.top ) + h.y * sy - PAD_PX;
    const w  = h.w * sx + PAD_PX * 2;
    const ht = h.h * sy + PAD_PX * 2;

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
