"""Pitch, chord and tonal-context helpers shared by analysis and generation."""
from __future__ import annotations

import math

NOTE_NAMES = ("C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B")
PC_NAMES = {name: i for i, name in enumerate(NOTE_NAMES)}
PC_NAMES.update({"Db": 1, "Eb": 3, "Gb": 6, "Ab": 8, "Bb": 10, "B#": 0, "Cb": 11, "D#": 3, "E#": 5, "Fb": 4, "G#": 8, "A#": 10})

CHORDS = {
    "maj": (0, 4, 7), "min": (0, 3, 7), "dim": (0, 3, 6), "aug": (0, 4, 8),
    "sus2": (0, 2, 7), "sus4": (0, 5, 7), "5": (0, 7),
    "7": (0, 4, 7, 10), "maj7": (0, 4, 7, 11), "min7": (0, 3, 7, 10),
    "mMaj7": (0, 3, 7, 11), "ø7": (0, 3, 6, 10), "dim7": (0, 3, 6, 9),
    "7♭9": (0, 4, 7, 10, 1), "9": (0, 4, 7, 10, 2), "min9": (0, 3, 7, 10, 2),
    "add9": (0, 2, 4, 7), "minAdd9": (0, 2, 3, 7),
}
CHORD_LABELS = {"maj": "", "min": "m", "dim": "°", "aug": "+", "sus2": "sus2", "sus4": "sus4",
                "5": "5", "7": "7", "maj7": "maj7", "min7": "m7", "mMaj7": "m(maj7)",
                "ø7": "ø7", "dim7": "°7", "7♭9": "7♭9", "9": "9", "min9": "m9",
                "add9": "add9", "minAdd9": "m(add9)"}
MAJOR = (0, 2, 4, 5, 7, 9, 11)
NATURAL_MINOR = (0, 2, 3, 5, 7, 8, 10)
MODES = {
    "Ionian": MAJOR, "Dorian": (0, 2, 3, 5, 7, 9, 10), "Phrygian": (0, 1, 3, 5, 7, 8, 10),
    "Lydian": (0, 2, 4, 6, 7, 9, 11), "Mixolydian": (0, 2, 4, 5, 7, 9, 10),
    "Aeolian": NATURAL_MINOR, "Locrian": (0, 1, 3, 5, 6, 8, 10),
}
MOODS = ("Dark", "Melancholic", "Tension", "Horror", "Epic", "Mystery", "Release", "Custom")


def chord_label(root: int, quality: str) -> str:
    return NOTE_NAMES[root % 12] + CHORD_LABELS.get(quality, quality)


def chord_pcs(root: int, quality: str) -> set[int]:
    return {(root + i) % 12 for i in CHORDS.get(quality, CHORDS["maj"])}


def parse_chord(text: str) -> tuple[int, str]:
    value = text.strip()
    for length in (2, 1):
        root = value[:length]
        if root in PC_NAMES:
            suffix = value[length:].strip()
            reverse = {v: k for k, v in CHORD_LABELS.items()}
            if suffix in reverse:
                return PC_NAMES[root], reverse[suffix]
            aliases = {"m": "min", "minor": "min", "major": "maj", "": "maj", "dim": "dim", "aug": "aug", "min7": "min7", "m7": "min7", "maj7": "maj7"}
            if suffix in aliases:
                return PC_NAMES[root], aliases[suffix]
    raise ValueError("Use e.g. Am7, F♯, B♭dim7 or Dsus4")


def scale_pcs(tonic: int, mode: str) -> set[int]:
    return {(tonic + n) % 12 for n in MODES.get(mode, MAJOR)}


def roman(root: int, tonic: int, mode: str) -> str:
    scale = MODES.get(mode, MAJOR)
    diff = (root - tonic) % 12
    roman_names = ("I", "II", "III", "IV", "V", "VI", "VII")
    for i, step in enumerate(scale):
        if step == diff:
            return roman_names[i]
    nearest = min(range(7), key=lambda i: min((diff - scale[i]) % 12, (scale[i] - diff) % 12))
    delta = (diff - scale[nearest]) % 12
    return ("♭" if delta in (10, 11) else "♯" if delta in (1, 2) else "") + roman_names[nearest]


def key_name(tonic: int, mode: str) -> str:
    return f"{NOTE_NAMES[tonic % 12]} {mode}"


def estimate_key(hist: list[float]) -> tuple[int, str, float, list[tuple[int, str, float]]]:
    major_profile = (6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88)
    minor_profile = (6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17)
    mean_h = sum(hist) / 12 if hist else 0
    centered = [x - mean_h for x in hist]
    candidates: list[tuple[int, str, float]] = []
    for root in range(12):
        for mode, profile in (("Ionian", major_profile), ("Aeolian", minor_profile)):
            p = [profile[(i - root) % 12] for i in range(12)]
            pm = sum(p) / 12
            p = [x - pm for x in p]
            den = math.sqrt(sum(x*x for x in centered) * sum(x*x for x in p))
            corr = sum(a*b for a, b in zip(centered, p)) / den if den else 0.0
            candidates.append((root, mode, corr))
    candidates.sort(key=lambda x: x[2], reverse=True)
    return (*candidates[0], candidates[:5])

