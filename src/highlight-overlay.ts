/**
 * highlight-overlay.ts — SVG pulse ring that draws over the canvas at a
 * device-pixel region while a tour step is active.
 *
 * Coordinate system: the SVG is sized to match the canvas's CSS bounds,
 * and we map device pixels (800×480) into the current canvas size.
 */

import type { Highlight } from './tour-types';

export class HighlightOverlay {
  private svg: SVGSVGElement;
  private rect: SVGRectElement;

  constructor(private canvas: HTMLCanvasElement) {
    const svgNs = 'http://www.w3.org/2000/svg';
    this.svg = document.createElementNS(svgNs, 'svg');
    this.svg.setAttribute('class', 'dash-sandbox-highlight');
    Object.assign(this.svg.style, {
      position: 'absolute',
      inset: '0',
      width: '100%',
      height: '100%',
      pointerEvents: 'none',
      display: 'none',
    });

    this.rect = document.createElementNS(svgNs, 'rect');
    this.rect.setAttribute('fill', 'none');
    this.rect.setAttribute('stroke', '#3dd8ff');
    this.rect.setAttribute('stroke-width', '3');
    this.rect.setAttribute('rx', '10');
    this.rect.setAttribute('ry', '10');
    this.rect.style.filter = 'drop-shadow(0 0 10px #3dd8ff)';

    // A second, animated rectangle that pulses outwards — cheap attention
    // grabber without hammering layout.
    const pulse = this.rect.cloneNode() as SVGRectElement;
    pulse.setAttribute('stroke-opacity', '0.6');
    pulse.style.animation = 'dash-sandbox-pulse 1.6s ease-out infinite';

    this.svg.appendChild(pulse);
    this.svg.appendChild(this.rect);

    // Inject keyframes once per document.
    if (!document.getElementById('dash-sandbox-keyframes')) {
      const s = document.createElement('style');
      s.id = 'dash-sandbox-keyframes';
      s.textContent = `
        @keyframes dash-sandbox-pulse {
          0%   { transform: scale(1);   opacity: 0.8; }
          100% { transform: scale(1.15); opacity: 0; }
        }`;
      document.head.appendChild(s);
    }
  }

  attach(container: HTMLElement) {
    container.appendChild(this.svg);
  }

  show(h: Highlight) {
    const rect = this.canvas.getBoundingClientRect();
    const sx = rect.width / 800;
    const sy = rect.height / 480;

    const x = h.x * sx;
    const y = h.y * sy;
    const w = h.w * sx;
    const h2 = h.h * sy;

    this.svg.setAttribute('viewBox', `0 0 ${rect.width} ${rect.height}`);
    for (const el of [this.svg.children[0], this.rect]) {
      (el as SVGRectElement).setAttribute('x', String(x));
      (el as SVGRectElement).setAttribute('y', String(y));
      (el as SVGRectElement).setAttribute('width', String(w));
      (el as SVGRectElement).setAttribute('height', String(h2));
      // Transform-origin at the rect center so the pulse scales about it.
      (el as SVGRectElement).style.transformOrigin = `${x + w / 2}px ${y + h2 / 2}px`;
    }
    this.svg.style.display = 'block';
  }

  hide() {
    this.svg.style.display = 'none';
  }
}
