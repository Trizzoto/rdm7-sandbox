/**
 * voiceover.ts — thin wrapper around the Web Speech API.
 *
 * v1: browser-built-in SpeechSynthesis. Free, zero setup, decent quality
 *      on Chrome / Edge / Safari. Quality varies by OS voice.
 * v2 plan: swap this implementation for a fetch-based ElevenLabs adapter
 *      that plays pre-baked MP3s from /tours/<id>/audio/<step>.mp3.
 *      Interface stays the same so the tutorial-runner doesn't change.
 */

export interface Voice {
  speak(text: string): Promise<void>;
  cancel(): void;
}

/** Web Speech API implementation. */
class WebSpeechVoice implements Voice {
  private voice: SpeechSynthesisVoice | null = null;

  constructor(private lang = 'en-US') {
    this.loadVoice();
    // Some browsers populate voices asynchronously.
    window.speechSynthesis.addEventListener('voiceschanged', () => this.loadVoice());
  }

  private loadVoice() {
    const voices = window.speechSynthesis.getVoices();
    this.voice =
      voices.find((v) => v.lang === this.lang && v.name.includes('Natural')) ??
      voices.find((v) => v.lang === this.lang) ??
      voices[0] ??
      null;
  }

  speak(text: string): Promise<void> {
    return new Promise((resolve) => {
      const u = new SpeechSynthesisUtterance(text);
      u.lang = this.lang;
      u.rate = 1.0;
      u.pitch = 1.0;
      if (this.voice) u.voice = this.voice;
      u.onend = () => resolve();
      u.onerror = () => resolve(); // don't block the tour on TTS errors
      window.speechSynthesis.speak(u);
    });
  }

  cancel() {
    window.speechSynthesis.cancel();
  }
}

/** No-op for environments without TTS (jsdom tests, muted mode). */
class SilentVoice implements Voice {
  async speak(_text: string): Promise<void> {
    /* no-op */
  }
  cancel() {
    /* no-op */
  }
}

export function createVoice(kind: 'web-speech' | 'silent' = 'web-speech'): Voice {
  if (kind === 'silent' || typeof window === 'undefined' || !window.speechSynthesis) {
    return new SilentVoice();
  }
  return new WebSpeechVoice();
}
