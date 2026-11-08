"""Utterance endpointing on top of WebRTC VAD."""
import webrtcvad

FRAME_MS = 30  # WebRTC VAD accepts 10, 20 or 30 ms frames


class Endpointer:
    """Feed 16 kHz PCM; reports when the speaker has finished talking."""

    def __init__(self, rate: int, aggressiveness: int, silence_ms: int,
                 min_speech_ms: int, max_utterance_s: int):
        self.rate = rate
        self._vad = webrtcvad.Vad(aggressiveness)
        self._frame_bytes = rate * FRAME_MS // 1000 * 2
        self._silence_limit = silence_ms // FRAME_MS
        self._min_speech = min_speech_ms // FRAME_MS
        self._max_frames = max_utterance_s * 1000 // FRAME_MS
        self.reset()

    def reset(self):
        self.pcm = bytearray()      # everything since the last reset
        self._pending = bytearray()  # bytes not yet forming a full VAD frame
        self._speech = 0
        self._silence = 0
        self._frames = 0
        self.started = False

    def feed(self, pcm: bytes) -> bool:
        """Return True once the utterance is complete."""
        self.pcm += pcm
        self._pending += pcm
        done = False
        while len(self._pending) >= self._frame_bytes:
            frame = bytes(self._pending[:self._frame_bytes])
            del self._pending[:self._frame_bytes]
            self._frames += 1
            if self._vad.is_speech(frame, self.rate):
                self._speech += 1
                self._silence = 0
                if self._speech >= self._min_speech:
                    self.started = True
            elif self.started:
                self._silence += 1
                if self._silence >= self._silence_limit:
                    done = True
            if self._frames >= self._max_frames and self.started:
                done = True
        return done
