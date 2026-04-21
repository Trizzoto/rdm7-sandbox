/**
 * sandbox.ts — entry point for the @rdm7/sandbox package.
 * Registers the <dash-sandbox> web component.
 */
import { DashSandboxElement } from './dash-sandbox.js';

if (!customElements.get('dash-sandbox')) {
  customElements.define('dash-sandbox', DashSandboxElement);
}

export { DashSandboxElement };
export type { TourScript, TourStep } from './tour-types.js';
