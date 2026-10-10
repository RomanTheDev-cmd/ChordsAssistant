"""Deterministic phrase-level harmony transformation and candidate ranking."""
from __future__ import annotations

from dataclasses import dataclass, replace
from itertools import product
import math

from .analysis import Analysis, ChordEvent
from .midi import MidiFile, Note
from .theory import CHORDS, MOODS, MODES, chord_label, chord_pcs, scale_pcs


VARIATION_STRATEGIES = (
    ("Modal Shadow", "modal"),
    ("Chromatic Mediants", "mediant"),
    ("Tension Architecture", "tension"),
    ("Common Tone Drift", "common"),
    ("Pedal & Return", "pedal"),
)


@dataclass
class Controls:
    mood: str = "Dark"
    darkness: int = 45
    transformation: int = 42
    tension_curve: list[float] | None = None
    preserve_rhythm: bool = True
    preserve_bass: bool = True
    preserve_common_tones: bool = True
    low_note: int = 42
    high_note: int = 84
    density: int = 4
    custom_dissonance: int = 35


@dataclass
class Variation:
    name: str
    strategy: str
    chords: list[ChordEvent]
    notes: list[Note]
    bass: list[Note]
    score: float
    score_parts: dict[str, float]
    ppq: int
    tempos: list[tuple[int, int]]
    signatures: list[tuple[int, int, int, int, int]]
    end_tick: int


def _curve_value(curve: list[float] | None, measure: int, measures: int, mood: str) -> float:
    if curve:
        idx = min(len(curve) - 1, round(measure * (len(curve) - 1) / max(1, measures - 1)))
        return max(0.0, min(1.0, curve[idx]))
    if mood == "Tension":
        return min(1.0, 0.18 + 0.78 * measure / max(1, measures - 1))
    if mood == "Release":
        return max(0.0, 0.82 - 0.72 * measure / max(1, measures - 1))
    if mood == "Epic":
        return 0.35 + 0.46 * (0.5 + 0.5 * math.sin(measure * math.pi / max(1, measures - 1)))
    if mood == "Horror":
        return 0.42 + 0.25 * (measure % 2)
    return 0.36


def _mood_tension(mood: str, darkness: int, custom: int) -> float:
    base = {"Dark": 0.44, "Melancholic": 0.25, "Tension": 0.62, "Horror": 0.78,
            "Epic": 0.40, "Mystery": 0.53, "Release": 0.14, "Custom": custom / 100}.get(mood, 0.4)
    return max(0, min(1, base + (darkness - 50) * 0.003))


def _quality_options(root: int, base_quality: str, mood: str, tension: float) -> list[str]:
    if mood == "Horror" or tension > 0.76:
        return ["dim7", "ø7", "mMaj7", "7♭9", "dim", "min7"]
    if mood in ("Dark", "Mystery") or tension > 0.52:
        return ["min7", "mMaj7", "ø7", "7", "sus2", "maj7"]
    if mood == "Melancholic":
        return ["min7", "mMaj7", "min9", "min", "maj7"]
    if mood == "Epic":
        return ["maj7", "add9", "7", "sus4", "min7"]
    if mood == "Release":
        return ["maj", "maj7", "min", "add9", "sus2"]
    return [base_quality, "maj7", "min7", "sus4", "add9"]


def _candidates(chord: ChordEvent, nxt: ChordEvent | None, tonic: int, mode: str,
                mood: str, strategy: str, target: float, index: int) -> list[tuple[int, str, str]]:
    result: list[tuple[int, str, str]] = [(chord.root, chord.quality, "source")]
    scale = scale_pcs(tonic, mode)
    majorish = mode in ("Ionian", "Lydian", "Mixolydian")
    # Strategy-specific harmonic colors are introduced only when requested tension supports them.
    if strategy == "modal":
        palette = [((tonic + 10) % 12, "maj7", "borrowed bVII"),
                   ((tonic + 8) % 12, "maj7", "borrowed bVI"),
                   ((tonic + 5) % 12, "min7", "borrowed iv"),
                   ((tonic + 1) % 12, "maj7" if majorish else "dim", "Phrygian bII")]
        root, quality, tag = palette[index % len(palette)]
        if target > 0.25:
            result.append((root, quality, tag))
        # Preserve the current root while borrowing modal thirds/extensions.
        result.extend((chord.root, q, "modal quality") for q in _quality_options(chord.root, chord.quality, mood, target))
    elif strategy == "mediant":
        shift = (3, -3, 4, -4)[index % 4]
        result.append(((chord.root + shift) % 12, "min7" if abs(shift) in (3, 4) else "maj7", "chromatic mediant"))
        result.append(((chord.root - shift) % 12, "maj7" if index % 2 else "min7", "chromatic mediant"))
        if nxt:
            dominant = (nxt.root + 7) % 12
            result.append((dominant, "7", "secondary dominant"))
    elif strategy == "tension":
        if nxt:
            dom = (nxt.root + 7) % 12
            result.extend(((dom, "7", "secondary dominant"), ((dom + 6) % 12, "7", "tritone substitute"),
                           ((nxt.root - 1) % 12, "dim7", "leading diminished")))
        result.extend((chord.root, q, "tension extension") for q in ("7♭9", "ø7", "dim7", "mMaj7", "min9"))
        if target < 0.35:
            result.append((tonic, "min" if mode in ("Aeolian", "Dorian", "Phrygian") else "maj", "tonic release"))
    elif strategy == "common":
        result.extend((chord.root, q, "common-tone color") for q in ("maj7", "min7", "add9", "minAdd9", "sus2", "sus4", "mMaj7"))
        for shift in (-1, 1):
            root = (chord.root + shift) % 12
            result.append((root, "dim" if target > 0.5 else "min7", "neighbor-tone"))
    else:  # pedal & return
        result.extend((chord.root, q, "pedal color") for q in ("maj7", "min7", "sus2", "7"))
        if nxt and target > 0.48:
            result.append(((nxt.root + 7) % 12, "7", "secondary dominant"))
    # Add diatonic neighboring chords as context-aware alternatives.
    if target < 0.60:
        for step in MODES.get(mode, MODES["Ionian"]):
            root = (tonic + step) % 12
            if root != chord.root:
                quality = "min" if len(scale & chord_pcs(root, "min")) > len(scale & chord_pcs(root, "maj")) else "maj"
                result.append((root, quality, "diatonic neighbor"))
    # Stable unique order.
    seen = set()
    return [c for c in result if not ((c[0] % 12, c[1]) in seen or seen.add((c[0] % 12, c[1])))]


def _candidate_tension(root: int, quality: str, tonic: int, target_curve: float) -> float:
    value = 0.16
    value += {"7": .25, "ø7": .28, "dim": .30, "dim7": .38, "aug": .32,
              "7♭9": .47, "mMaj7": .30, "min7": .13, "maj7": .09, "9": .20}.get(quality, .02)
    value += .12 if (root - tonic) % 12 in (1, 6) else 0.0
    value += .05 if target_curve > .66 and quality in ("7", "7♭9", "dim7", "ø7") else 0
    return min(1.0, value)


def _select_progression(chords: list[ChordEvent], tonic: int, mode: str, mood: str,
                        strategy: str, controls: Controls, measures: int) -> tuple[list[ChordEvent], list[float]]:
    out: list[ChordEvent] = []
    chosen_tension: list[float] = []
    previous: ChordEvent | None = None
    transform = controls.transformation / 100.0
    darkness = controls.darkness / 100.0
    mood_t = _mood_tension(mood, controls.darkness, controls.custom_dissonance)
    for idx, chord in enumerate(chords):
        measure = min(measures - 1, max(0, chord.start // max(1, controls._bar_ticks))) if hasattr(controls, "_bar_ticks") else chord.measure
        curve = _curve_value(controls.tension_curve, measure, measures, mood)
        target = max(0, min(1, 0.22 + 0.42 * curve + 0.38 * mood_t + 0.18 * darkness))
        next_chord = chords[idx + 1] if idx + 1 < len(chords) else None
        candidates = _candidates(chord, next_chord, tonic, mode, mood, strategy, target, idx)
        best = None
        for root, quality, tag in candidates:
            tension = _candidate_tension(root, quality, tonic, curve)
            root_distance = min((root - chord.root) % 12, (chord.root - root) % 12)
            transform_cost = 0.015 * root_distance + (0.09 if quality != chord.quality else 0)
            # At low transformation, source identity carries most of the score.
            preservation = (1 - transform) * (0.48 if root != chord.root else 0.18 if quality != chord.quality else 0)
            motion = 0
            common = 0
            if previous:
                motion = min((root - previous.root) % 12, (previous.root - root) % 12)
                common = len(chord_pcs(root, quality) & chord_pcs(previous.root, previous.quality))
            voice_cost = 0.014 * motion - (0.025 * common if controls.preserve_common_tones else 0)
            target_cost = abs(tension - target) * (0.42 if strategy in ("tension", "pedal") else 0.24)
            tag_bias = 0
            if strategy == "modal" and tag.startswith("borrowed"):
                tag_bias = -0.07 * darkness
            elif strategy == "mediant" and tag == "chromatic mediant":
                tag_bias = -0.055 * transform
            elif strategy == "tension" and tag in ("secondary dominant", "tritone substitute", "leading diminished"):
                tag_bias = -0.11 * curve * transform
            elif strategy == "common" and tag == "common-tone color":
                tag_bias = -0.055
            elif strategy == "pedal" and tag == "pedal color":
                tag_bias = -0.035
            if mood == "Release" and curve < .4:
                tag_bias -= .08 if root == tonic else 0
            if tag == "tritone substitute" and (curve < .55 or mode in ("Ionian", "Aeolian") and controls.darkness < 30):
                tag_bias += .18
            score = transform * transform_cost + preservation + voice_cost + target_cost + tag_bias
            if best is None or score < best[0]:
                best = (score, root, quality, tension, tag)
        _, root, quality, tension, tag = best
        # Let the transformed phrase return through a common tone or dominant in its last bars.
        if strategy == "pedal" and idx == len(chords) - 1 and transform > .22:
            root = tonic
            quality = "min" if mode in ("Aeolian", "Dorian", "Phrygian") else "maj"
            tension = .12
            tag = "tonic return"
        elif idx == len(chords) - 1 and mood == "Release" and curve < .4:
            root = tonic
            quality = "min" if mode in ("Aeolian", "Dorian", "Phrygian") else "maj"
            tension = .12
            tag = "tonic release"
        new = replace(chord, root=root, quality=quality, label=chord_label(root, quality), tension=tension)
        setattr(new, "transformation_tag", tag)
        out.append(new)
        chosen_tension.append(tension)
        previous = new
    return out, chosen_tension


def _selected_pcs(root: int, quality: str, density: int) -> list[int]:
    intervals = list(CHORDS.get(quality, CHORDS["maj"]))
    ordered = [0]
    for interval in intervals[1:]:
        if interval not in ordered:
            ordered.append(interval)
    if len(ordered) < 3:
        ordered.extend([3, 7])
    if density >= 4 and len(ordered) < 4:
        ordered.append(10 if quality in ("7", "min7", "ø7", "dim7") else 11)
    if density >= 5 and len(ordered) < 5:
        ordered.append(2 if quality not in ("7♭9",) else 1)
    return [(root + x) % 12 for x in ordered[:max(2, min(5, density))]]


def _voice(root: int, quality: str, density: int, low: int, high: int,
           previous: list[int] | None, keep_common: bool) -> list[int]:
    low, high = max(24, min(95, low)), max(low + 12, min(108, high))
    pcs = _selected_pcs(root, quality, density)
    options = [[p for p in range(low, high + 1) if p % 12 == pc] for pc in pcs]
    options = [x or [min(range(24, 109), key=lambda p: (p % 12 - pc) % 12)] for x, pc in zip(options, pcs)]
    best: tuple[float, list[int]] | None = None
    for candidate in product(*options):
        notes = sorted(candidate)
        if len(set(notes)) < len(notes):
            continue
        if previous:
            aligned = list(previous)
            target = list(notes)
            # Compare corresponding voices, including octave-equivalent pitches.
            width = max(len(aligned), len(target))
            padded_prev = aligned + [aligned[-1] + 12] * (width - len(aligned))
            padded_new = target + [target[-1] - 12] * (width - len(target))
            movement = sum(min(abs(a-b), abs(a-b-12), abs(a-b+12)) for a, b in zip(padded_prev, padded_new))
            shared = len({n % 12 for n in previous} & {n % 12 for n in notes})
            score = movement - (4 * shared if keep_common else 0) + max(0, notes[-1] - notes[0] - 30) * 0.08
        else:
            score = sum(abs(n - (low + high) / 2) for n in notes) * .1 + max(0, notes[0] - low - 18) * .1
        if best is None or score < best[0]:
            best = (score, notes)
    chosen = best[1] if best else [low + ((root - low) % 12)]
    while len(chosen) < max(2, min(density, 6)):
        root_pitch = next((p for p in range(low, high + 1) if p % 12 == root and p not in chosen), chosen[0] + 12)
        if root_pitch > high:
            break
        chosen.append(root_pitch)
        chosen.sort()
    return chosen


def _harmonic_events(chords: list[ChordEvent], controls: Controls, analysis: Analysis) -> list[ChordEvent]:
    if controls.preserve_rhythm:
        return chords
    bar_ticks = int(round(analysis.midi.ppq * analysis.beats_per_bar))
    expanded: list[ChordEvent] = []
    for chord in chords:
        if chord.end - chord.start > analysis.midi.ppq * 2:
            middle = chord.start + (chord.end - chord.start) // 2
            expanded.append(replace(chord, end=middle))
            alt_root = (chord.root + (7 if chord.quality in ("min", "min7") else 5)) % 12
            expanded.append(replace(chord, start=middle, root=alt_root,
                                    quality="7" if chord.quality in ("maj", "maj7") else "min7",
                                    label=chord_label(alt_root, "7" if chord.quality in ("maj", "maj7") else "min7")))
        else:
            expanded.append(chord)
    return expanded


def _build_variation(analysis: Analysis, controls: Controls, tonic: int, mode: str,
                     name: str, strategy: str, mood: str) -> Variation:
    source = _harmonic_events(analysis.chords, controls, analysis)
    if not source:
        return Variation(name, strategy, [], [], [], 0, {}, analysis.midi.ppq,
                         analysis.midi.tempos, analysis.midi.signatures, analysis.midi.end_tick)
    bar_ticks = int(round(analysis.midi.ppq * analysis.beats_per_bar))
    # `_bar_ticks` is local engine context, not a persisted user preference.
    setattr(controls, "_bar_ticks", bar_ticks)
    chords, tension_values = _select_progression(source, tonic, mode, mood, strategy, controls, analysis.measures)
    delattr(controls, "_bar_ticks")
    notes: list[Note] = []
    previous: list[int] | None = None
    voice_motion = 0.0
    common_count = 0
    for chord in chords:
        voicing = _voice(chord.root, chord.quality, controls.density, controls.low_note,
                         controls.high_note, previous, controls.preserve_common_tones)
        if previous:
            voice_motion += sum(min(abs(a-b), abs(a-b-12), abs(a-b+12)) for a, b in zip(previous, voicing)) / max(1, min(len(previous), len(voicing)))
            common_count += len({n % 12 for n in previous} & {n % 12 for n in voicing})
        duration = max(1, chord.end - chord.start)
        for pitch in voicing:
            notes.append(Note(chord.start, chord.start + duration, pitch, 82, 0, 0))
        previous = voicing
    if controls.preserve_bass and analysis.bass_notes:
        bass = [replace(n, channel=1, track=1) for n in analysis.bass_notes]
    else:
        bass = []
        pedal = chords[0].root
        for i, chord in enumerate(chords):
            measure = min(analysis.measures - 1, chord.start // max(1, bar_ticks))
            curve = _curve_value(controls.tension_curve, measure, analysis.measures, mood)
            root = pedal if strategy == "pedal" and i < len(chords) - 1 else chord.root
            pitch = next((p for p in range(28, 55) if p % 12 == root), 36 + root % 12)
            if curve > .72 and i < len(chords) - 1:
                next_root = chords[i + 1].root
                if (next_root - root) % 12 > 6:
                    pitch = max(28, pitch - 1)
                elif 0 < (next_root - root) % 12 <= 6:
                    pitch = min(52, pitch + 1)
            bass.append(Note(chord.start, max(chord.start + 1, chord.end), pitch, 92, 1, 1))
    transform_score = sum(1 for a, b in zip(analysis.chords, chords) if a.label != b.label) / max(1, min(len(analysis.chords), len(chords)))
    curve = [_curve_value(controls.tension_curve, m, analysis.measures, mood) for m in range(analysis.measures)]
    expected = sum(curve) / max(1, len(curve))
    actual = sum(tension_values) / max(1, len(tension_values))
    curve_fit = max(0.0, 1.0 - abs(expected - actual))
    continuity = max(0.0, 1.0 - voice_motion / max(1, len(chords)) / 18.0)
    harmonic = 1.0 - (sum(1 for c in chords if c.confidence < .18) / max(1, len(chords)))
    transform_fit = max(0.0, 1.0 - abs(transform_score - controls.transformation / 100))
    score_parts = {"voice_leading": continuity, "coherence": harmonic,
                   "transformation": transform_fit, "tension_curve": curve_fit}
    weights = {"voice_leading": .32, "coherence": .28, "transformation": .20, "tension_curve": .20}
    score = sum(score_parts[k] * weights[k] for k in weights)
    return Variation(name, strategy, chords, notes, bass, score, score_parts,
                     analysis.midi.ppq, analysis.midi.tempos, analysis.midi.signatures,
                     analysis.midi.end_tick)


def generate_variations(analysis: Analysis, controls: Controls) -> list[Variation]:
    mood = controls.mood if controls.mood in MOODS else "Dark"
    # Score and sort, while keeping strategy names visible to the user.
    variations = [_build_variation(analysis, controls, analysis.tonic, analysis.mode,
                                  name, strategy, mood)
                 for name, strategy in VARIATION_STRATEGIES]
    return sorted(variations, key=lambda v: v.score, reverse=True)


def generate_from_scratch(tonic: int, mode: str, measures: int, controls: Controls,
                          ppq: int = 480, numerator: int = 4, denominator: int = 4) -> tuple[Analysis, list[Variation]]:
    """Secondary path: make a functional/mode-aware seed phrase, then transform it."""
    measures = max(2, min(64, measures))
    beat = ppq * 4 // max(1, denominator)
    bar = beat * numerator
    scale = MODES.get(mode, MODES["Ionian"])
    minor = mode in ("Aeolian", "Dorian", "Phrygian", "Locrian")
    degrees = (0, 5, 2, 6) if minor else (0, 4, 5, 3)
    qualities = ("min", "maj", "maj", "maj") if minor else ("maj", "maj", "min", "maj")
    chords = []
    for i in range(measures):
        degree = degrees[i % len(degrees)]
        root = (tonic + scale[degree]) % 12
        quality = qualities[i % len(qualities)]
        chords.append(ChordEvent(i * bar, (i + 1) * bar, root, quality,
                                 chord_label(root, quality), .7, tuple(sorted(chord_pcs(root, quality))), root,
                                 measure=i))
    midi = MidiFile(ppq, 1, [], [(0, 500_000)], [(0, numerator, denominator, 24, 8)], measures * bar)
    analysis = Analysis(midi, chords, tonic, mode, .7, [], [], [0.0] * 12, measures,
                        float(numerator * 4 / denominator))
    variations = [_build_variation(analysis, controls, tonic, mode, name, strategy, controls.mood)
                  for name, strategy in VARIATION_STRATEGIES]
    return analysis, sorted(variations, key=lambda v: v.score, reverse=True)

