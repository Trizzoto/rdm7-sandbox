/**
 * voiceover.ts — caption-pacing adapter (no audio).
 *
 * The tour runtime calls `speak(text)` to block advancement until the
 * "voice" has "finished." With audio removed, we simulate a human
 * reading pace so tour timing still works without anyone hearing a
 * robot voice. Default cadence is ~17 chars/sec which matches a
 * moderately-paced narrator.
 *
 * Audio support is intentionally gone for v0.1.x — captions alone
 * read cleaner on embed pages, don't interrupt the user's own audio
 * (music, calls, other tabs), and don't need TTS permission prompts.
 * If you ever want it back, swap `createVoice()` for a Web Speech
 * or ElevenLabs implementation; the interface is stable.
 */

export interface Voice {
  /** Resolve after a read-pace timeout proportional to caption length. */
  speak(text: string): Promise<void>;
  /** Cancel a pending speak — used when Pause / Back / step-jump fire. */
  cancel(): void;
  /** Release any long-lived resources (listeners, cached state). */
  dispose?(): void;
}

/** Minimum time a caption stays on screen, even for a 3-word line, so
 *  readers aren't whiplashed through fast steps. */
const MIN_READ_MS = 1200;
/** Rough reading pace in milliseconds per character. 60 ms/char ≈
 *  17 chars/sec ≈ 3 words/sec — comfortable silent-reading speed. */
const MS_PER_CHAR = 60;

class SilentVoice implements Voice {
  private pending: ReturnType<typeof setTimeout> | null = null;

  speak(text: string): Promise<void> {
    this.cancel();
    const duration = Math.max(MIN_READ_MS, text.length * MS_PER_CHAR);
    return new Promise((resolve) => {
      this.pending = setTimeout(() => {
        this.pending = null;
        resolve();
      }, duration);
    });
  }

  cancel() {
    if (this.pending !== null) {
      clearTimeout(this.pending);
      this.pending = null;
    }
  }

  dispose() {
    this.cancel();
  }
}

/** Retained for signature compatibility — the `kind` arg is ignored
 *  in v0.1.x (all variants are silent). Kept so host pages wiring up
 *  future audio modes don't have to change imports. */
export function createVoice(_kind: 'silent' | 'web-speech' = 'silent'): Voice {
  return new SilentVoice();
}
