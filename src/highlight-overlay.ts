/**
 * highlight-overlay.ts — blue highlight shown while a tour step is
 * active.
 *
 * Uses a plain absolutely-positioned <div> with box-shadow and a
 * tinted background rather than an SVG + stacked drop-shadows. The
 * resulting glow renders smoother across browsers (drop-shadow on
 * SVG strokes doubles up on each side of the line, producing the
 * slightly muddy look the old version had).
 *
 * Coordinates in TourStep.highlight are device pixels (800×480).
 * The overlay maps them to CSS pixels via the canvas's current
 * bounding rect and offsets by the canvas position inside the
 * sandbox root, so highlights can legitimately extend off the
 * device screen — over the transport buttons or narration card —
 * without being clipped.
 */

import type { Highlight } from './tour-types.js';

const BLUE = '#2d8ceb';

export class HighlightOverlay {
  private box: HTMLDivElement;
  private root: HTMLElement | null = null;

  constructor(private canvas: HTMLCanvasElement) {
    this.box = document.createElement('div');
    this.box.setAttribute('class', 'dash-sandbox-highlight');
    Object.assign(this.box.style, {
      position:        'absolute',
      border:          `2px solid ${BLUE}`,
      borderRadius:    '0',                               // sharp corners
      background:      'rgba(45, 140, 235, 0.08)',        // subtle inner tint
      boxShadow:       `0 0 0 1px rgba(45, 140, 235, 0.15), ` +
                       `0 0 22px 4px rgba(45, 140, 235, 0.38)`,
      pointerEvents:   'none',
      display:         'none',
      zIndex:          '5',
      // Subtle breath on opacity only — no scale transform, so the
      // rectangle never visibly grows or shrinks.
      transition:      'opacity .15s ease',
      animation:       'dash-sandbox-glow 2s ease-in-out infinite',
    });

    if (!document.getElementById('dash-sandbox-keyframes')) {
      const s = document.createElement('style');
      s.id = 'dash-sandbox-keyframes';
      s.textContent = `
        @keyframes dash-sandbox-glow {
          0%, 100% {
            box-shadow:
              0 0 0 1px rgba(45, 140, 235, 0.15),
              0 0 18px 3px rgba(45, 140, 235, 0.32);
          }
          50% {
            box-shadow:
              0 0 0 1px rgba(45, 140, 235, 0.25),
              0 0 28px 6px rgba(45, 140, 235, 0.52);
          }
        }`;
      document.head.appendChild(s);
    }
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
