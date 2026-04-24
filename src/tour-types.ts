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
  | 'dashboard'        /* Default layout running with mock driving data */
  | 'dashboard_menu'   /* Dashboard + floating Menu button revealed */
  | 'setup_menu'       /* Setup Menu modal (Layout / Splash / Device Settings) */
  | 'device_settings'  /* Device Settings screen */
  | 'widget_config'    /* Widget config modal over dashboard */
  | 'wifi_settings'    /* Multi-SSID Wi-Fi Settings screen */
  | 'peaks'            /* Signal peak / min / max table */
  | 'diagnostics';     /* System health / diagnostics */

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

  /** Scroll the Device Settings content column to a named section so
   *  the tour can highlight content below the initial fold. Only
   *  meaningful when the current scene is `device_settings`. See the
   *  `DS_SECTION_*` enum in device_settings_sandbox.c for valid ids. */
  scrollSection?: ScrollSection;
}

export type ScrollSection =
  | 'top'          /* CAN BUS + DEVICE INFO row (id 0)              */
  | 'network'      /* NETWORK & UPDATES / DISPLAY row (id 1)        */
  | 'display'      /* Same as network, different highlight target (id 5) */
  | 'logging'      /* DATA LOGGING / PEAK HOLD / TESTING (id 2)     */
  | 'peaks'        /* Same as logging, middle column (id 6)         */
  | 'can_diag'     /* CAN BUS diagnostics (id 3)                    */
  | 'footer';      /* System Diag / Setup Wizard / Reset (id 4)     */

export interface TourScript {
  id: string;
  title: string;
  steps: TourStep[];
}
