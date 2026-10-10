"""Tiny local additive preview synthesizer; intentionally needs no sound bank."""
from __future__ import annotations

import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave

from .midi import Note


def _seconds_at(tick: int, tempos: list[tuple[int, int]], ppq: int) -> float:
    events = sorted(tempos or [(0, 500_000)])
    seconds = 0.0
    cursor = 0
    micros = events[0][1]
    for at, new in events[1:]:
        if at >= tick:
            break
        seconds += (at - cursor) * micros / (ppq * 1_000_000)
        cursor, micros = at, new
    seconds += (tick - cursor) * micros / (ppq * 1_000_000)
    return max(0.0, seconds)


def render_wav(path: str | Path, notes: list[Note], tempos: list[tuple[int, int]], ppq: int,
               *, sample_rate: int = 22050, max_seconds: int = 180) -> Path:
    events = [(n, _seconds_at(n.start, tempos, ppq), _seconds_at(n.end, tempos, ppq)) for n in notes]
    duration = min(max_seconds, max((end for _, _, end in events), default=1.0) + .35)
    frame_count = max(1, int(duration * sample_rate))
    out = bytearray(frame_count * 2)
    active: list[tuple[Note, float, float, float]] = []
    starts = sorted(events, key=lambda x: x[1])
    j = 0
    for frame in range(frame_count):
        t = frame / sample_rate
        while j < len(starts) and starts[j][1] <= t:
            note, start, end = starts[j]
            active.append((note, start, end, 440.0 * (2.0 ** ((note.pitch - 69) / 12.0))))
            j += 1
        active = [item for item in active if item[2] + .20 > t]
        sample = 0.0
        for note, start, end, frequency in active:
            age = t - start
            release = min(1.0, max(0.0, (end - t) / .20)) if t > end else 1.0
            envelope = min(1.0, age / .018) * release
            phase = 2 * math.pi * frequency * age
            tone = math.sin(phase) + .25 * math.sin(phase * 2) + .10 * math.sin(phase * 3)
            sample += tone * envelope * note.velocity / 127.0
        sample = max(-1.0, min(1.0, sample * .16))
        struct.pack_into("<h", out, frame * 2, int(sample * 32767))
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(sample_rate)
        file.writeframes(out)
    return path


def play_local(path: str | Path) -> subprocess.Popen | None:
    player = shutil.which("afplay") or shutil.which("paplay") or shutil.which("aplay")
    if not player:
        return None
    args = [player, str(path)]
    if Path(player).name == "aplay":
        args = [player, "-q", str(path)]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def temporary_wav(notes: list[Note], tempos: list[tuple[int, int]], ppq: int) -> Path:
    temp = Path(tempfile.gettempdir()) / "harmonic-shadow-preview.wav"
    return render_wav(temp, notes, tempos, ppq)

