"""Opus encode/decode helpers (16 kHz mono, fixed frame duration)."""
import io

import opuslib_next as opus
from pydub import AudioSegment


class OpusDecoder:
    def __init__(self, rate: int, frame_ms: int):
        self.frame_samples = rate * frame_ms // 1000
        self._dec = opus.Decoder(rate, 1)

    def decode(self, packet: bytes) -> bytes:
        return self._dec.decode(packet, self.frame_samples)


class OpusEncoder:
    def __init__(self, rate: int, frame_ms: int):
        self.frame_samples = rate * frame_ms // 1000
        self._enc = opus.Encoder(rate, 1, opus.APPLICATION_AUDIO)

    def encode_pcm(self, pcm: bytes) -> list[bytes]:
        """Split 16-bit mono PCM into frames and encode each one."""
        step = self.frame_samples * 2
        frames = []
        for i in range(0, len(pcm), step):
            chunk = pcm[i:i + step]
            if len(chunk) < step:
                chunk += b"\x00" * (step - len(chunk))
            frames.append(self._enc.encode(chunk, self.frame_samples))
        return frames


def mp3_to_pcm(mp3: bytes, rate: int) -> bytes:
    seg = AudioSegment.from_file(io.BytesIO(mp3), format="mp3")
    return seg.set_channels(1).set_frame_rate(rate).set_sample_width(2).raw_data
