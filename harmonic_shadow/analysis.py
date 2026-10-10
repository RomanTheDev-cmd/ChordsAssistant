"""Polyphonic phrase and chord analysis for multi-track MIDI."""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field
import math

from .midi import MidiFile, Note
from .theory import CHORDS, NOTE_NAMES, chord_label, estimate_key, key_name, roman


@dataclass
class ChordEvent:
    start: int
    end: int
    root: int
    quality: str
    label: str
    confidence: float
    pitch_classes: tuple[int, ...] = ()
    bass_pc: int | None = None
    function: str = ""
    tension: float = 0.0
    measure: int = 0


@dataclass
class Analysis:
    midi: MidiFile
    chords: list[ChordEvent]
    tonic: int
    mode: str
    confidence: float
    alternates: list[tuple[int, str, float]]
    bass_notes: list[Note]
    histogram: list[float]
    measures: int
    beats_per_bar: float
    repeats: list[str] = field(default_factory=list)
    local_centers: list[tuple[int, str]] = field(default_factory=list)

    @property
    def key(self) -> str:
        return key_name(self.tonic, self.mode)

    @property
    def chord_summary(self) -> str:
        return "  ·  ".join(c.label for c in self.chords) or "No chord events detected"


def _best_chord(pcs: Counter[int], bass_pc: int | None) -> tuple[int, str, float]:
    if not pcs:
        return 0, "5", 0.0
    total = sum(pcs.values()) or 1.0
    observed = set(pcs)
    best = (0, "5", -1e9)
    for root in range(12):
        for quality, intervals in CHORDS.items():
            template = {(root + i) % 12 for i in intervals}
            hit = sum(pcs[p] for p in template if p in pcs) / total
            outside = sum(pcs[p] for p in observed - template) / total
            missing = len(template - observed) / max(3, len(template))
            score = hit * 1.55 - outside * 0.60 - missing * 0.28
            if bass_pc is not None:
                score += (0.16 if bass_pc == root else 0.09 if bass_pc in template else -0.03)
            # Prefer a useful triad/seventh template over an extension when evidence is sparse.
            if quality in ("7♭9", "9", "min9", "add9", "minAdd9"):
                score -= 0.035
            if score > best[2]:
                best = (root, quality, score)
    return best[0], best[1], max(0.0, min(1.0, (best[2] + 0.2) / 1.4))


def analyze(midi: MidiFile) -> Analysis:
    notes = [n for tr in midi.tracks for n in tr.notes]
    ppq = midi.ppq
    num, den = midi.signatures[0][1:3] if midi.signatures else (4, 4)
    beats_per_bar = max(1.0, num * 4.0 / max(1, den))
    bar_ticks = int(round(ppq * beats_per_bar))
    end_tick = max(midi.end_tick, max((n.end for n in notes), default=0), bar_ticks)
    measures = max(1, math.ceil(end_tick / max(1, bar_ticks)))
    # Half-beat frames retain harmonic rhythm while smoothing short passing notes.
    frame = max(1, ppq // 2)
    chords: list[ChordEvent] = []
    histogram = [0.0] * 12
    for n in notes:
        histogram[n.pitch % 12] += max(0.05, n.end - n.start) * (0.55 + n.velocity / 180.0)
    for tick in range(0, end_tick, frame):
        stop = min(end_tick, tick + frame)
        counts: Counter[int] = Counter()
        bass_candidates: list[Note] = []
        for n in notes:
            overlap = max(0, min(n.end, stop) - max(n.start, tick))
            if overlap:
                counts[n.pitch % 12] += overlap * (0.55 + n.velocity / 180.0)
                if n.pitch <= 58:
                    bass_candidates.append(n)
        if not counts:
            continue
        bass_pc = min(bass_candidates, key=lambda n: n.pitch).pitch % 12 if bass_candidates else None
        root, quality, score = _best_chord(counts, bass_pc)
        label = chord_label(root, quality)
        if chords and chords[-1].label == label and chords[-1].end == tick:
            chords[-1].end = stop
            chords[-1].confidence = min(chords[-1].confidence, score)
            chords[-1].pitch_classes = tuple(sorted(set(chords[-1].pitch_classes) | set(counts)))
            if chords[-1].bass_pc is None:
                chords[-1].bass_pc = bass_pc
        else:
            chords.append(ChordEvent(tick, stop, root, quality, label, score,
                                     tuple(sorted(counts)), bass_pc, measure=tick // bar_ticks))
    tonic, mode, confidence, alternates = estimate_key(histogram)
    for chord in chords:
        chord.function = roman(chord.root, tonic, mode)
        chord.tension = _chord_tension(chord, tonic)
    # Bass is chosen from predominantly low-register tracks; if there is no such track,
    # retain the lowest voice as the bass guide rather than discarding it.
    track_low = []
    for ti, track in enumerate(midi.tracks):
        pitches = [n.pitch for n in track.notes]
        if pitches and sorted(pitches)[len(pitches) // 2] <= 54:
            track_low.append(ti)
    bass_notes = [n for n in notes if n.track in track_low] if track_low else [n for n in notes if n.pitch <= 48]
    if not bass_notes and notes:
        bass_notes = [min(notes, key=lambda n: n.pitch)]
    repeats = _repeating_motifs([c.label for c in chords])
    local_centers = _local_keys(notes, midi, measures, bar_ticks)
    return Analysis(midi, chords, tonic, mode, confidence, alternates,
                    sorted(bass_notes, key=lambda n: (n.start, n.pitch)), histogram,
                    measures, beats_per_bar, repeats, local_centers)


def _chord_tension(chord: ChordEvent, tonic: int) -> float:
    intervals = {(p - chord.root) % 12 for p in chord.pitch_classes}
    score = 0.14
    score += 0.23 if chord.quality in ("7", "ø7", "dim", "dim7", "aug", "7♭9") else 0.0
    score += 0.19 if chord.quality in ("7♭9", "dim7", "mMaj7") else 0.0
    score += 0.12 if (chord.root - tonic) % 12 in (1, 6) else 0.0
    score += 0.08 if 1 in intervals or 6 in intervals else 0.0
    return min(1.0, score)


def _repeating_motifs(labels: list[str]) -> list[str]:
    found: list[str] = []
    for size in range(2, min(5, len(labels) // 2 + 1)):
        for start in range(len(labels) - size * 2 + 1):
            phrase = labels[start:start + size]
            if phrase == labels[start + size:start + size * 2]:
                desc = " → ".join(phrase)
                if desc not in found:
                    found.append(desc)
                break
        if len(found) >= 3:
            break
    return found


def _local_keys(notes: list[Note], midi: MidiFile, measures: int, bar_ticks: int) -> list[tuple[int, str]]:
    result = []
    for bar in range(measures):
        hist = [0.0] * 12
        left, right = bar * bar_ticks, (bar + 1) * bar_ticks
        for n in notes:
            overlap = max(0, min(right, n.end) - max(left, n.start))
            hist[n.pitch % 12] += overlap
        if sum(hist):
            tonic, mode, confidence, _ = estimate_key(hist)
            if confidence >= 0.32:
                result.append((bar + 1, key_name(tonic, mode)))
    return result

