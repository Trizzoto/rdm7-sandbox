/** Shape of a Guided Tour JSON script. */

export interface Highlight {
  /** Box in device pixels (800×480 coordinate space). */
  x: number;
  y: number;
  w: number;
  h: number;
}

/** Named checkpoint in the LVGL wizard state. The tour runner calls
 *  sandbox_set_scene() when the step enters, so Back/Next always leaves
 *  the render in sync with the caption. Mirror SCENE_* in wizard_sandbox.c. */
export type WizardScene =
  | 'step1'            /* CAN scan running (fresh start)   */
  | 'step1_done'       /* CAN scan complete, Apply visible */
  | 'step2'            /* ECU picker overlay               */
  | 'step3'            /* Connect Your Device              */
  | 'wifi_picker'      /* WiFi scan list                   */
  | 'wifi_connected'   /* WiFi list with "Connected" label */
  | 'dashboard';       /* Default layout running with mock driving data */

export interface TourStep {
  /** Spoken line. Rendered as a caption underneath; also fed to TTS. */
  voice?: string;

  /** Which LVGL scene should be visible for this step. Runner switches
   *  scenes on rewind / skip-forward so the render matches the audio. */
  scene?: WizardScene;

  /** Draw a pulsing rectangle over this region while the step is active. */
  highlight?: Highlight;

  /** Inject a pointer click at device-pixel coordinates. */
  click?: [number, number];

  /** Inject a drag from p1 to p2. 250 ms by default. */
  drag?: { from: [number, number]; to: [number, number]; durationMs?: number };

  /** Pause after the step (additional to voice duration). */
  delay?: number;

  /** Wait an absolute amount before advancing, regardless of voice. */
  wait?: number;

  /** Free-form caption shown below the voice line — useful for hints. */
  caption?: string;
}

export interface TourScript {
  id: string;
  title: string;
  steps: TourStep[];
}
