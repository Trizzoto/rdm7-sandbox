/**
 * wasm-loader.ts — wraps the Emscripten-generated module (rdm7-sandbox.js)
 * with a typed interface so the web component doesn't sprinkle `any` around.
 */

export interface SandboxModule {
  /** Advance one frame of LVGL. Called from requestAnimationFrame. */
  _sandbox_step(): void;
  /** Inject a pointer event at device-pixel coordinates. */
  _sandbox_set_pointer(x: number, y: number, pressed: number): void;
  /** Jump the wizard to a scene checkpoint. Used when the tour rewinds
   *  (Back button) so the LVGL render stays in sync with the audio. */
  _sandbox_set_scene(scene: number): void;

  /** Scroll the Device Settings scrollable content to a named section
   *  (see DS_SECTION_* in device_settings_sandbox.c). No-op when the
   *  settings screen isn't currently active. */
  _sandbox_device_settings_scroll?(sectionId: number): void;

  /** Open / close the widget config modal over the dashboard. Ignored
   *  when the dashboard isn't the active screen. */
  _sandbox_open_widget_config?(): void;
  _sandbox_close_widget_config?(): void;

  /** Emscripten-provided call helpers. */
  ccall: (
    fn: string,
    returnType: string | null,
    argTypes: string[],
    args: unknown[],
  ) => unknown;
  cwrap: (fn: string, ret: string | null, args: string[]) => (...a: unknown[]) => unknown;

  /** Emscripten virtual file system. */
  FS: {
    writeFile(path: string, data: Uint8Array): void;
    readFile(path: string): Uint8Array;
  };

  /** Canvas the SDL2 driver renders to. Must be set BEFORE main() runs. */
  canvas?: HTMLCanvasElement;
}

type ModuleFactory = (opts?: Partial<SandboxModule>) => Promise<SandboxModule>;

/** Dynamically load the emcc-generated module. The JS file attaches
 *  itself as `window.RDM7Sandbox` (we set EXPORT_NAME to that in
 *  CMakeLists). Called once per sandbox component instance.
 *
 *  @param wasmUrl   Path to rdm7-sandbox.wasm
 *  @param canvas    Canvas SDL2 renders into
 *  @param debug     If false (default), silences every print/ESP_LOG
 *                   line the wasm fires. Set via the component's
 *                   `debug` attribute. */
export async function loadSandbox(
  wasmUrl: string,
  canvas: HTMLCanvasElement,
  debug = false,
): Promise<SandboxModule> {
  const scriptUrl = wasmUrl.replace(/\.wasm$/, '.js');

  await new Promise<void>((resolve, reject) => {
    const s = document.createElement('script');
    s.src = scriptUrl;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error(`Failed to load ${scriptUrl}`));
    document.head.appendChild(s);
  });

  const factory = (window as unknown as { RDM7Sandbox?: ModuleFactory }).RDM7Sandbox;
  if (!factory) {
    throw new Error(
      'RDM7Sandbox factory not found on window — check EXPORT_NAME in CMakeLists.',
    );
  }

  // Emscripten's SDL2 port keys off Module.canvas for its GL context.
  // The preRun hook fires BEFORE main() runs — guaranteeing SDL sees the
  // canvas when it calls SDL_CreateWindow. Passing canvas as a plain
  // factory option isn't always enough; Emscripten's internal code paths
  // reference Module.canvas in multiple places during init.
  const opts = {
    canvas,
    preRun: [
      function (Module: { canvas: HTMLCanvasElement }) {
        Module.canvas = canvas;
      },
    ],
    locateFile: (p: string) => {
      if (p.endsWith('.wasm')) return wasmUrl;
      return p;
    },
    // In debug mode surface everything; otherwise silence print/printErr
    // so consumer DevTools consoles aren't spammed by every frame's
    // ESP_LOGI chatter. Known-harmless SDL event-handler errors are
    // suppressed unconditionally (they're not functional problems).
    print: debug ? console.log.bind(console) : () => { /* silent */ },
    printErr: (msg: string) => {
      if (typeof msg === 'string' && msg.includes('registerOrRemoveHandler')) return;
      if (debug) console.error(msg);
    },
  };
  return factory(opts as Partial<SandboxModule>);
}
