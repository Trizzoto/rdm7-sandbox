# Tour JSON Format

A **tour** is a narrated, scripted walkthrough of the sandbox wizard. Tours are plain JSON so they can be hand-authored, translated, or generated from a markdown doc by your tooling.

## Top-level shape

```jsonc
{
  "id":    "first-boot",          // unique, URL-safe
  "title": "First-Boot Walkthrough",
  "steps": [ /* TourStep[] */ ]
}
```

## `TourStep` fields

All fields are optional. A step with no fields is a no-op.

| Field        | Type                                                  | Purpose |
|--------------|-------------------------------------------------------|---------|
| `scene`      | `"step1" \| "step1_done" \| "step2" \| "step3" \| "wifi_picker" \| "wifi_connected"` | Jump the wizard to this visual state. Sticky: subsequent steps without `scene` stay in the last-declared scene. |
| `voice`      | `string`                                              | Line spoken by TTS and shown in the caption card. |
| `highlight`  | `{ x, y, w, h }`                                      | Draw a pulsing cyan rectangle over this region, in device pixels (800×480). |
| `click`      | `[x, y]`                                              | Inject a scripted tap at device coordinates. Fires mid-caption so the user sees the button light up before the action. |
| `drag`       | `{ from: [x,y], to: [x,y], durationMs?: number }`     | Inject a drag. 250 ms default duration. |
| `wait`       | `number` (ms)                                         | Delay AFTER the step (in addition to voice duration). |
| `delay`      | `number` (ms)                                         | Alias for `wait` (same semantics, retained for readability). |
| `caption`    | `string`                                              | Extra line shown below the voice caption. |

## Scene reference

Scenes match states in the sandbox wizard. Use these to ensure the LVGL canvas is always in sync with your narration, including when the user taps **Back** to rewind.

| Scene              | Visual state                                                                    |
|--------------------|---------------------------------------------------------------------------------|
| `step1`            | CAN scan running from a fresh start (progress bar, 4 bitrate rows testing)     |
| `step1_done`       | CAN scan complete — 500 kbps highlighted, Apply button visible                  |
| `step2`            | ECU picker overlay — Make + Version dropdowns (Haltech Elite selected)          |
| `step3`            | Connect Your Device — WiFi / USB / Hotspot options                              |
| `wifi_picker`      | WiFi scan list with 5 mock SSIDs                                                |
| `wifi_connected`   | WiFi scan list with "Connected to Home_2.4" status                              |

**Tip:** when a step transitions the canvas via a `click`, you don't need to set `scene` on the *next* step too — the click drives the state change inside the wizard. But declaring `scene` anyway keeps Back / step-dot jumping robust against timing races.

## Device pixel coordinate system

The wizard canvas is **800 × 480**. `(0, 0)` is the top-left corner. Coordinates in `click`, `drag`, and `highlight` are always in device pixels, never CSS pixels — the sandbox component handles CSS scaling.

Key firmware wizard button positions (for coordinate reference):

| UI element                  | Centre (x, y) |
|-----------------------------|---------------|
| Step 1 — Apply & Continue   | `(400, 313)`  |
| Step 1 — Continue without CAN | `(400, 362)` |
| Step 2 — Apply              | `(400, 320)`  |
| Step 3 — Join a WiFi Network | `(400, 173)` |
| Step 3 — Finish Setup       | `(400, 361)`  |
| WiFi picker — Home_2.4 row  | `(400, 150)`  |
| WiFi picker — Back button   | `(722, 38)`   |

## Minimal example

```json
{
  "id": "quick-hello",
  "title": "30-second Hello",
  "steps": [
    {
      "scene": "step1_done",
      "voice": "Scan done — 500 kilobits detected.",
      "wait":  3000
    },
    {
      "voice": "Tap Apply.",
      "click": [400, 313],
      "delay": 1200
    },
    {
      "scene": "step2",
      "voice": "Now pick your ECU.",
      "wait":  3000
    }
  ]
}
```

## Authoring guidelines

1. **Write captions to be spoken, not read.** Short sentences, contractions, natural rhythm. Avoid parenthetical asides — they don't survive TTS.
2. **Narration pacing ≈ 3 words / second.** A 60-word caption needs ≥ 20 s before a click fires.
3. **Always set `scene` on Back-reachable steps.** The runner scans backward for the nearest declared scene when rewinding, so skipping one or two mid-tour steps is fine — but the first step of a phase should always pin the scene.
4. **Keep `click` highlights honest.** If you tell the user to tap a button, highlight the same button.
5. **Test with Back / dot-jumping.** A polished tour survives the user scrubbing around. If jumping to step 7 lands on the wrong canvas state, add an explicit `scene` earlier.
6. **60-second limit for landing-page tours.** Longer works for docs site walkthroughs; shorter is better for marketing.

## Registering a tour

Drop the JSON in `tours/` and add an entry to `tours/manifest.json`:

```jsonc
{
  "id":          "my-tour",
  "title":       "My Custom Tour",
  "file":        "my-tour.json",
  "description": "What this tour teaches.",
  "durationSec": 40
}
```

The dev host page's picker reads `manifest.json` on load. npm consumers can ignore the manifest and reference a single tour directly via the `script` attribute.
