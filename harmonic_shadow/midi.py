"""Small dependency-free Standard MIDI File reader and writer (formats 0 and 1)."""
from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import Iterable


class MidiError(ValueError):
    pass


@dataclass
class Note:
    start: int
    end: int
    pitch: int
    velocity: int = 88
    channel: int = 0
    track: int = 0


@dataclass
class MidiTrack:
    name: str
    notes: list[Note] = field(default_factory=list)


@dataclass
class MidiFile:
    ppq: int = 480
    format: int = 1
    tracks: list[MidiTrack] = field(default_factory=list)
    tempos: list[tuple[int, int]] = field(default_factory=lambda: [(0, 500_000)])
    signatures: list[tuple[int, int, int, int, int]] = field(default_factory=lambda: [(0, 4, 4, 24, 8)])
    end_tick: int = 0


def _read_vlq(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    for _ in range(4):
        if pos >= len(data):
            raise MidiError("Unexpected end of variable-length quantity")
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, pos
    raise MidiError("Invalid variable-length quantity")


def _vlq(value: int) -> bytes:
    value = max(0, int(value))
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def read_midi(data: bytes) -> MidiFile:
    if len(data) < 14 or data[:4] != b"MThd":
        raise MidiError("Not a Standard MIDI File")
    hlen = int.from_bytes(data[4:8], "big")
    if hlen < 6 or 8 + hlen > len(data):
        raise MidiError("Invalid MIDI header")
    fmt = int.from_bytes(data[8:10], "big")
    count = int.from_bytes(data[10:12], "big")
    division = int.from_bytes(data[12:14], "big")
    if fmt not in (0, 1):
        raise MidiError("Only MIDI Type 0 and Type 1 are supported")
    if division & 0x8000 or division == 0:
        raise MidiError("SMPTE time division is not supported; use PPQ MIDI")
    result = MidiFile(ppq=division, format=fmt, tracks=[])
    pos = 8 + hlen
    all_tempos: list[tuple[int, int]] = []
    all_sigs: list[tuple[int, int, int, int, int]] = []
    for ti in range(count):
        while pos + 8 <= len(data) and data[pos:pos + 4] != b"MTrk":
            pos += 1
        if pos + 8 > len(data):
            raise MidiError(f"Missing MIDI track {ti + 1}")
        length = int.from_bytes(data[pos + 4:pos + 8], "big")
        start = pos + 8
        stop = start + length
        if stop > len(data):
            raise MidiError("Truncated MIDI track")
        chunk = data[start:stop]
        pos = stop
        tick = 0
        cursor = 0
        running = 0
        name = f"Track {ti + 1}"
        pending: dict[tuple[int, int], deque[tuple[int, int]]] = defaultdict(deque)
        notes: list[Note] = []
        while cursor < len(chunk):
            delta, cursor = _read_vlq(chunk, cursor)
            tick += delta
            if cursor >= len(chunk):
                break
            status = chunk[cursor]
            if status & 0x80:
                cursor += 1
                if status < 0xF0:
                    running = status
            else:
                if running == 0:
                    raise MidiError("Invalid MIDI running status")
                status = running
            if status == 0xFF:
                if cursor >= len(chunk):
                    break
                kind = chunk[cursor]
                cursor += 1
                size, cursor = _read_vlq(chunk, cursor)
                payload = chunk[cursor:cursor + size]
                cursor += size
                if kind == 0x03:
                    name = payload.decode("utf-8", "replace") or name
                elif kind == 0x51 and len(payload) == 3:
                    all_tempos.append((tick, int.from_bytes(payload, "big")))
                elif kind == 0x58 and len(payload) >= 4:
                    all_sigs.append((tick, payload[0], 2 ** payload[1], payload[2], payload[3]))
                elif kind == 0x2F:
                    break
                continue
            if status in (0xF0, 0xF7):
                size, cursor = _read_vlq(chunk, cursor)
                cursor += size
                continue
            family = status & 0xF0
            channel = status & 0x0F
            needed = 1 if family in (0xC0, 0xD0) else 2
            if cursor + needed > len(chunk):
                break
            d1 = chunk[cursor]
            d2 = chunk[cursor + 1] if needed == 2 else 0
            cursor += needed
            if family == 0x90 and d2 > 0:
                pending[(channel, d1)].append((tick, d2))
            elif family == 0x80 or (family == 0x90 and d2 == 0):
                queue = pending[(channel, d1)]
                if queue:
                    onset, velocity = queue.popleft()
                    notes.append(Note(onset, max(onset + 1, tick), d1, velocity, channel, ti))
        for (channel, pitch), queue in pending.items():
            for onset, velocity in queue:
                notes.append(Note(onset, max(onset + 1, tick), pitch, velocity, channel, ti))
        result.tracks.append(MidiTrack(name, sorted(notes, key=lambda n: (n.start, n.pitch))))
        result.end_tick = max(result.end_tick, tick)
    result.tempos = sorted(all_tempos) or [(0, 500_000)]
    result.signatures = sorted(all_sigs) or [(0, 4, 4, 24, 8)]
    result.end_tick = max(result.end_tick, max((n.end for tr in result.tracks for n in tr.notes), default=0))
    if fmt == 0 and count > 1:
        raise MidiError("MIDI Type 0 files must contain exactly one track")
    return result


def _meta(kind: int, payload: bytes) -> bytes:
    return b"\xff" + bytes([kind]) + _vlq(len(payload)) + payload


def _track_chunk(events: list[tuple[int, int, bytes]]) -> bytes:
    events.sort(key=lambda e: (e[0], e[1]))
    body = bytearray()
    last = 0
    for tick, _, payload in events:
        body.extend(_vlq(max(0, tick - last)))
        body.extend(payload)
        last = tick
    body.extend(b"\x00\xff\x2f\x00")
    return b"MTrk" + len(body).to_bytes(4, "big") + bytes(body)


def write_midi(notes: Iterable[Note], ppq: int, tempos: list[tuple[int, int]],
               signatures: list[tuple[int, int, int, int, int]], *,
               bass: Iterable[Note] = (), title: str = "Harmonic Shadow") -> bytes:
    ppq = max(24, min(32767, int(ppq)))
    meta: list[tuple[int, int, bytes]] = [(0, -5, _meta(0x03, b"Conductor"))]
    for tick, micros in tempos or [(0, 500_000)]:
        meta.append((max(0, tick), -4, _meta(0x51, int(micros).to_bytes(3, "big"))))
    for tick, num, den, clocks, thirtyseconds in signatures or [(0, 4, 4, 24, 8)]:
        power = max(0, min(7, int(den).bit_length() - 1))
        meta.append((max(0, tick), -3, _meta(0x58, bytes([num, power, clocks, thirtyseconds]))))
    chunks = [_track_chunk(meta)]
    for name, items, channel in ((title, list(notes), 0), ("Bass", list(bass), 1)):
        events: list[tuple[int, int, bytes]] = [(0, -5, _meta(0x03, name.encode("utf-8")[:120]))]
        for note in items:
            ch = max(0, min(15, note.channel if len(items) and note.channel < 16 else channel))
            events.append((max(0, note.start), 1, bytes([0x90 | ch, max(0, min(127, note.pitch)), max(1, min(127, note.velocity))])))
            events.append((max(note.start + 1, note.end), 0, bytes([0x80 | ch, max(0, min(127, note.pitch)), 0])))
        if not items:
            continue
        chunks.append(_track_chunk(events))
    fmt = 1 if len(chunks) > 1 else 0
    header = b"MThd" + (6).to_bytes(4, "big") + fmt.to_bytes(2, "big") + len(chunks).to_bytes(2, "big") + ppq.to_bytes(2, "big")
    return header + b"".join(chunks)

