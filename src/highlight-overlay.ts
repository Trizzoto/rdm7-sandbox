/**
 * highlight-overlay.ts — blue highlight + glow shown while a tour step
 * is active.
 *
 * The overlay spans the whole sandbox component (not just the canvas)
 * so highlights can legitimately extend off the device screen — over
 * the transport buttons, narration card, or anywhere else the tour
 * wants to draw attention. Coordinates in TourStep.highlight are
 * still in device-pixel space (800 × 480), which stays intuitive
 * when you're authoring tours that point at on-canvas widgets. The
 * overlay does the canvas-in-page math so the rectangle lands on
 * the right CSS pixels regardless of zoom / responsive scaling.
 *
 * Visual language:
 *   - Sharp rectangle (no rounded corners)
 *   - Studio-grade clean blue #2d8ceb
 *   - A proud outward glow via stacked drop-shadows
 *   - One slow opacity pulse so the eye stays drawn to it without
 *     the CSS scale-jumping the old version did
 */

import type { Highlight } from './tour-types.js';

const BLUE      = '#2d8ceb';  // Studio accent-2
const BLUE_SOFT = 'rgba(45, 140, 235, 0.55)';
const STROKE_WIDTH = 2;

export class HighlightOverlay {
  private svg: SVGSVGElement;
  private rect: SVGRectElement;
  private root: HTMLElement | null = null;

  constructor(private canvas: HTMLCanvasElement) {
    const svgNs = 'http://www.w3.org/2000/svg';
    this.svg = document.createElementNS(svgNs, 'svg');
    this.svg.setAttribute('class', 'dash-sandbox-highlight');
    Object.assign(this.svg.style, {
      position:      'absolute',
      inset:         '0',
      width:         '100%',
      height:        '100%',
      pointerEvents: 'none',
      overflow:      'visible',  // let the glow extend past the SVG bounds
      display:       'none',
      // Stack above the device bezel but below any modal captions.
      zIndex:        '5',
    });

    this.rect = document.createElementNS(svgNs, 'rect');
    this.rect.setAttribute('fill', 'none');
    this.rect.setAttribute('stroke', BLUE);
    this.rect.setAttribute('stroke-width', String(STROKE_WIDTH));
    this.rect.setAttribute('stroke-linejoin', 'miter');  // sharp corners
    this.rect.style.animation = 'dash-sandbox-glow 1.8s ease-in-out infinite';
    // Stacked drop-shadows = a real outward glow. filter compounds
    // nicely so three passes at increasing blur give a smooth falloff.
    this.rect.style.filter =
      `drop-shadow(0 0 4px ${BLUE}) ` +
      `drop-shadow(0 0 10px ${BLUE_SOFT}) ` +
      `drop-shadow(0 0 20px ${BLUE_SOFT})`;

    this.svg.appendChild(this.rect);

    // Inject keyframes once per document. Use opacity + stroke-width
    // rather than transform scale so the glow pulses gently without
    // the rectangle visibly growing/shrinking (which old version did
    // and looked a bit amateur).
    if (!document.getElementById('dash-sandbox-keyframes')) {
      const s = document.createElement('style');
      s.id = 'dash-sandbox-keyframes';
      s.textContent = `
        @keyframes dash-sandbox-glow {
          0%, 100% { opacity: 0.85; }
          50%      { opacity: 1; }
        }`;
      document.head.appendChild(s);
    }
  }

  /** Attach the overlay to the component's ROOT element — not the
   *  device-only subtree — so highlight rectangles can extend across
   *  the transport bar, narration card, etc. without being clipped. */
  attach(container: HTMLElement) {
    this.root = container;
    container.appendChild(this.svg);
  }

  show(h: Highlight) {
    if (!this.root) {
      // Fallback: if someone forgot to call attach(), bail rather
      // than silently render to the wrong coord space.
      this.svg.style.display = 'none';
      return;
    }
    const canvasRect = this.canvas.getBoundingClientRect();
    const rootRect   = this.root.getBoundingClientRect();

    // Map device pixels (0..800, 0..480) onto the canvas's CSS size,
    // then offset by the canvas's position inside the root so the
    // rectangle lands at the right spot when the overlay spans the
    // whole component.
    const sx = canvasRect.width  / 800;
    const sy = canvasRect.height / 480;
    const x  = (canvasRect.left - rootRect.left) + h.x * sx;
    const y  = (canvasRect.top  - rootRect.top ) + h.y * sy;
    const w  = h.w * sx;
    const hh = h.h * sy;

    this.svg.setAttribute('viewBox', `0 0 ${rootRect.width} ${rootRect.height}`);
    this.rect.setAttribute('x',      String(x));
    this.rect.setAttribute('y',      String(y));
    this.rect.setAttribute('width',  String(w));
    this.rect.setAttribute('height', String(hh));
    this.svg.style.display = 'block';
  }

  hide() {
    this.svg.style.display = 'none';
  }
}
