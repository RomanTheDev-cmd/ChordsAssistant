#include "HarmonicEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <numeric>
#include <set>
#include <tuple>

namespace harmonic_shadow {
namespace {

constexpr std::array<const char*, 12> names {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
constexpr std::array<std::array<int, 7>, 7> modes {{
    {{0, 2, 4, 5, 7, 9, 11}}, {{0, 2, 3, 5, 7, 9, 10}}, {{0, 1, 3, 5, 7, 8, 10}},
    {{0, 2, 4, 6, 7, 9, 11}}, {{0, 2, 4, 5, 7, 9, 10}}, {{0, 2, 3, 5, 7, 8, 10}},
    {{0, 1, 3, 5, 6, 8, 10}}
}};

std::vector<int> intervals(Quality q) {
    switch (q) {
        case Quality::Major: return {0, 4, 7};
        case Quality::Minor: return {0, 3, 7};
        case Quality::Diminished: return {0, 3, 6};
        case Quality::Augmented: return {0, 4, 8};
        case Quality::Sus2: return {0, 2, 7};
        case Quality::Sus4: return {0, 5, 7};
        case Quality::Power: return {0, 7};
        case Quality::Dominant7: return {0, 4, 7, 10};
        case Quality::Major7: return {0, 4, 7, 11};
        case Quality::Minor7: return {0, 3, 7, 10};
        case Quality::MinorMajor7: return {0, 3, 7, 11};
        case Quality::HalfDiminished7: return {0, 3, 6, 10};
        case Quality::Diminished7: return {0, 3, 6, 9};
        case Quality::DominantFlat9: return {0, 4, 7, 10, 1};
        case Quality::Add9: return {0, 2, 4, 7};
        case Quality::MinorAdd9: return {0, 2, 3, 7};
    }
    return {0, 4, 7};
}

std::string suffix(Quality q) {
    switch (q) {
        case Quality::Major: return ""; case Quality::Minor: return "m";
        case Quality::Diminished: return "°"; case Quality::Augmented: return "+";
        case Quality::Sus2: return "sus2"; case Quality::Sus4: return "sus4";
        case Quality::Power: return "5"; case Quality::Dominant7: return "7";
        case Quality::Major7: return "maj7"; case Quality::Minor7: return "m7";
        case Quality::MinorMajor7: return "m(maj7)"; case Quality::HalfDiminished7: return "ø7";
        case Quality::Diminished7: return "°7"; case Quality::DominantFlat9: return "7b9";
        case Quality::Add9: return "add9"; case Quality::MinorAdd9: return "m(add9)";
    }
    return "";
}

struct ChordGuess { int root = 0; Quality quality = Quality::Power; double confidence = 0.0; int bassPc = -1; };

ChordGuess detectChord(const std::vector<Note>& notes, double start, double end) {
    std::array<double, 12> histogram {};
    int bassPitch = 128;
    for (const auto& n : notes) {
        const double overlap = std::max(0.0, std::min(end, n.endBeat) - std::max(start, n.startBeat));
        if (overlap <= 0.0) continue;
        histogram[static_cast<size_t>((n.pitch % 12 + 12) % 12)] += overlap * (0.6 + n.velocity / 180.0);
        if (n.pitch <= 58) bassPitch = std::min(bassPitch, n.pitch);
    }
    double total = std::accumulate(histogram.begin(), histogram.end(), 0.0);
    if (total <= 0.0) return {};
    int distinct = static_cast<int>(std::count_if(histogram.begin(), histogram.end(), [](double x) { return x > 0.0; }));
    ChordGuess best;
    double bestScore = -1.0e9;
    const std::array<Quality, 16> qualities {{Quality::Major, Quality::Minor, Quality::Diminished,
        Quality::Augmented, Quality::Sus2, Quality::Sus4, Quality::Power, Quality::Dominant7,
        Quality::Major7, Quality::Minor7, Quality::MinorMajor7, Quality::HalfDiminished7,
        Quality::Diminished7, Quality::DominantFlat9, Quality::Add9, Quality::MinorAdd9}};
    for (int root = 0; root < 12; ++root) for (auto quality : qualities) {
        auto iv = intervals(quality);
        std::set<int> chordPcs;
        for (int interval : iv) chordPcs.insert((root + interval) % 12);
        double hit = 0.0, outside = 0.0;
        int missing = 0;
        for (int pc = 0; pc < 12; ++pc) {
            if (chordPcs.count(pc)) hit += histogram[static_cast<size_t>(pc)];
            else outside += histogram[static_cast<size_t>(pc)];
        }
        for (int pc : chordPcs) if (histogram[static_cast<size_t>(pc)] == 0.0) ++missing;
        double score = (hit / total) * 1.55 - (outside / total) * 0.60
                     - (static_cast<double>(missing) / std::max(3, static_cast<int>(chordPcs.size()))) * 0.28;
        if (bassPitch < 128) {
            const int bassPc = bassPitch % 12;
            score += bassPc == root ? 0.16 : chordPcs.count(bassPc) ? 0.09 : -0.03;
        }
        if (quality == Quality::DominantFlat9 || quality == Quality::Add9 || quality == Quality::MinorAdd9) score -= 0.035;
        if (distinct < 3 && quality != Quality::Power) score -= 0.08;
        if (score > bestScore) {
            bestScore = score;
            best = {root, quality, std::clamp((score + 0.2) / 1.4, 0.0, 1.0)};
        }
    }
    best.bassPc = bassPitch < 128 ? bassPitch % 12 : -1;
    return best;
}

std::string romanFunction(const Chord& chord, int tonic, Mode mode) {
    static const std::array<const char*,7> roman {"I","II","III","IV","V","VI","VII"};
    const auto& scale = modes[static_cast<size_t>(mode)];
    const int delta = (chord.root - tonic + 12) % 12;
    int degree = 0, alteration = 0;
    bool matched = false;
    for (int i=0; i<7; ++i) if (scale[static_cast<size_t>(i)] == delta) { degree=i; matched=true; break; }
    if (!matched) {
        int bestDistance=13;
        for (int i=0; i<7; ++i) {
            const int raw=delta-scale[static_cast<size_t>(i)];
            const int distance=std::min((raw+12)%12,(12-raw%12)%12);
            if(distance<bestDistance){bestDistance=distance;degree=i;alteration=raw>0?1:-1;}
        }
    }
    std::string numeral=roman[static_cast<size_t>(degree)];
    if(alteration<0) numeral="b"+numeral; else if(alteration>0) numeral="#"+numeral;
    const bool lowercase=chord.quality==Quality::Minor || chord.quality==Quality::Minor7 || chord.quality==Quality::MinorMajor7 || chord.quality==Quality::MinorAdd9;
    if(lowercase) std::transform(numeral.begin(),numeral.end(),numeral.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(chord.quality==Quality::Diminished || chord.quality==Quality::Diminished7 || chord.quality==Quality::HalfDiminished7) numeral += "°";
    else if(chord.quality==Quality::Augmented) numeral += "+";
    return numeral;
}

std::vector<Chord> detectProgression(const std::vector<Note>& notes, double origin, double end,
                                    double beatsPerBar) {
    std::vector<Chord> out;
    if (notes.empty() || end <= origin) return out;
    const double frame = 0.5;
    for (double left = origin; left < end; left += frame) {
        const double right = std::min(end, left + frame);
        bool any = false;
        for (const auto& n : notes) if (n.startBeat < right && n.endBeat > left) { any = true; break; }
        if (!any) continue;
        auto guess = detectChord(notes, left, right);
        const auto label = chordName(guess.root, guess.quality);
        if (!out.empty() && out.back().label == label && std::abs(out.back().endBeat - (left - origin)) < 0.001) {
            out.back().endBeat = right - origin;
            out.back().confidence = std::min(out.back().confidence, guess.confidence);
        } else {
            Chord c;
            c.startBeat = left - origin; c.endBeat = right - origin; c.root = guess.root;
            c.quality = guess.quality; c.confidence = guess.confidence;
            c.bassPitchClass = guess.bassPc;
            c.measure = static_cast<int>(std::floor((left - origin) / std::max(1.0, beatsPerBar)));
            c.label = label;
            if (guess.bassPc >= 0 && guess.bassPc != guess.root) {
                c.label += "/" + noteName(guess.bassPc);
            }
            out.push_back(std::move(c));
        }
    }
    return out;
}

double curveAt(const Controls& c, int measure, int measures) {
    if (!c.tensionCurve.empty()) {
        const auto index = static_cast<size_t>(std::clamp(static_cast<int>(std::lround(
            static_cast<double>(measure) * (c.tensionCurve.size() - 1) / std::max(1, measures - 1))), 0,
            static_cast<int>(c.tensionCurve.size() - 1)));
        return std::clamp(c.tensionCurve[index], 0.0, 1.0);
    }
    const double x = static_cast<double>(measure) / std::max(1, measures - 1);
    switch (c.mood) {
        case Mood::Tension: return std::min(1.0, 0.18 + 0.78 * x);
        case Mood::Release: return std::max(0.0, 0.82 - 0.72 * x);
        case Mood::Epic: return 0.35 + 0.46 * (0.5 + 0.5 * std::sin(x * 3.141592653589793));
        case Mood::Horror: return 0.68 + 0.18 * (measure % 2);
        default: return 0.36;
    }
}

double moodTension(const Controls& c) {
    double value = 0.0;
    switch (c.mood) {
        case Mood::Dark: value = 0.48; break; case Mood::Melancholic: value = 0.27; break;
        case Mood::Tension: value = 0.64; break; case Mood::Horror: value = 0.90; break;
        case Mood::Epic: value = 0.43; break; case Mood::Mystery: value = 0.56; break;
        case Mood::Release: value = 0.12; break; case Mood::Custom: value = c.customDissonance / 100.0; break;
    }
    return std::clamp(value + (c.darkness - 50) * 0.003, 0.0, 1.0);
}

double chordTension(int root, Quality q, int tonic) {
    double t = 0.14;
    switch (q) {
        case Quality::Dominant7: t += .25; break; case Quality::HalfDiminished7: t += .28; break;
        case Quality::Diminished: t += .30; break; case Quality::Diminished7: t += .38; break;
        case Quality::Augmented: t += .32; break; case Quality::DominantFlat9: t += .47; break;
        case Quality::MinorMajor7: t += .30; break; case Quality::Minor7: t += .13; break;
        case Quality::Major7: t += .09; break; default: t += .02; break;
    }
    const int delta = (root - tonic + 12) % 12;
    if (delta == 1 || delta == 6) t += .12;
    return std::min(1.0, t);
}

struct Candidate { int root; Quality quality; std::string tag; };

std::vector<Candidate> candidatesFor(const Chord& c, const Chord* next, const Analysis& analysis,
                                     int index, const std::string& strategy,
                                     double target) {
    std::vector<Candidate> v {{c.root, c.quality, "source"}};
    const int tonic = analysis.tonic;
    const bool majorish = analysis.mode == Mode::Ionian || analysis.mode == Mode::Lydian || analysis.mode == Mode::Mixolydian;
    if (strategy == "Modal Shadow") {
        const std::array<Candidate, 4> borrowed {{{(tonic + 10) % 12, Quality::Major7, "borrowed bVII"},
            {(tonic + 8) % 12, Quality::Major7, "borrowed bVI"}, {(tonic + 5) % 12, Quality::Minor7, "borrowed iv"},
            {(tonic + 1) % 12, majorish ? Quality::Major7 : Quality::Diminished, "Phrygian bII"}}};
        auto b = borrowed[static_cast<size_t>(index % borrowed.size())]; if (target > .25) v.push_back(b);
        for (auto q : {Quality::Minor7, Quality::MinorMajor7, Quality::HalfDiminished7, Quality::Dominant7, Quality::Sus2, Quality::Major7})
            v.push_back({c.root, q, "modal quality"});
    } else if (strategy == "Chromatic Mediants") {
        const int shift = std::array<int,4>{{3,-3,4,-4}}[static_cast<size_t>(index % 4)];
        v.push_back({(c.root + shift + 12) % 12, Quality::Minor7, "chromatic mediant"});
        v.push_back({(c.root - shift + 12) % 12, index % 2 ? Quality::Major7 : Quality::Minor7, "chromatic mediant"});
        if (next) v.push_back({(next->root + 7) % 12, Quality::Dominant7, "secondary dominant"});
    } else if (strategy == "Tension Architecture") {
        if (next) {
            const int dom = (next->root + 7) % 12;
            v.push_back({dom, Quality::Dominant7, "secondary dominant"});
            v.push_back({(dom + 6) % 12, Quality::Dominant7, "tritone substitute"});
            v.push_back({(next->root + 11) % 12, Quality::Diminished7, "leading diminished"});
        }
        for (auto q : {Quality::DominantFlat9, Quality::HalfDiminished7, Quality::Diminished7, Quality::MinorMajor7, Quality::Minor7})
            v.push_back({c.root, q, "tension extension"});
        if (target < .35) v.push_back({tonic, majorish ? Quality::Major : Quality::Minor, "tonic release"});
    } else if (strategy == "Common Tone Drift") {
        for (auto q : {Quality::Major7, Quality::Minor7, Quality::Add9, Quality::MinorAdd9, Quality::Sus2, Quality::Sus4, Quality::MinorMajor7})
            v.push_back({c.root, q, "common-tone color"});
        v.push_back({(c.root + 1) % 12, target > .5 ? Quality::Diminished : Quality::Minor7, "neighbor-tone"});
        v.push_back({(c.root + 11) % 12, target > .5 ? Quality::Diminished : Quality::Minor7, "neighbor-tone"});
    } else {
        for (auto q : {Quality::Major7, Quality::Minor7, Quality::Sus2, Quality::Dominant7}) v.push_back({c.root, q, "pedal color"});
        if (next && target > .48) v.push_back({(next->root + 7) % 12, Quality::Dominant7, "secondary dominant"});
    }
    std::vector<Candidate> unique;
    std::set<std::pair<int,int>> seen;
    for (auto candidate : v) {
        const auto key = std::make_pair(candidate.root, static_cast<int>(candidate.quality));
        if (seen.insert(key).second) unique.push_back(std::move(candidate));
    }
    return unique;
}

double candidateTension(int root, Quality q, int tonic, double curve) {
    double value = chordTension(root, q, tonic);
    if (curve > .66 && (q == Quality::Dominant7 || q == Quality::DominantFlat9 || q == Quality::Diminished7 || q == Quality::HalfDiminished7)) value += .05;
    return std::min(1.0, value);
}

std::vector<int> chordVoicing(int root, Quality q, int density, int low, int high,
                              const std::vector<int>& previous, bool preserveCommon) {
    auto iv = intervals(q);
    std::vector<int> pcs {root};
    for (int n : iv) if (std::find(pcs.begin(), pcs.end(), (root + n) % 12) == pcs.end()) pcs.push_back((root + n) % 12);
    if (pcs.size() < 3) { pcs.push_back((root + 3) % 12); pcs.push_back((root + 7) % 12); }
    if (density >= 4 && pcs.size() < 4) pcs.push_back((root + ((q == Quality::Major || q == Quality::Major7) ? 11 : 10)) % 12);
    if (density >= 5 && pcs.size() < 5) pcs.push_back((root + 2) % 12);
    if (pcs.size() > static_cast<size_t>(std::clamp(density, 2, 5))) pcs.resize(static_cast<size_t>(std::clamp(density, 2, 5)));
    low = std::clamp(low, 24, 95); high = std::clamp(high, low + 12, 108);
    std::vector<std::vector<int>> options;
    for (int pc : pcs) {
        std::vector<int> pitches;
        for (int p = low; p <= high; ++p) if (p % 12 == pc) pitches.push_back(p);
        if (pitches.empty()) pitches.push_back(low + ((pc - low % 12 + 12) % 12));
        options.push_back(std::move(pitches));
    }
    std::vector<int> best, current;
    double bestCost = 1.0e12;
    std::function<void(size_t)> visit = [&](size_t pos) {
        if (pos == options.size()) {
            auto sorted = current; std::sort(sorted.begin(), sorted.end());
            if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) return;
            double cost = 0.0;
            if (previous.empty()) {
                for (int p : sorted) cost += std::abs(p - (low + high) / 2.0) * .1;
            } else {
                const size_t count = std::min(sorted.size(), previous.size());
                for (size_t i = 0; i < count; ++i) {
                    const int d = std::abs(sorted[i] - previous[i]);
                    cost += std::min({d, std::abs(d - 12), std::abs(d + 12)});
                }
                cost += static_cast<double>(std::max(sorted.size(), previous.size()) - count) * 5.0;
                if (preserveCommon) {
                    int common = 0;
                    for (int a : sorted) for (int b : previous) if (a % 12 == b % 12) { ++common; break; }
                    cost -= common * 4.0;
                }
            }
            if (cost < bestCost) { bestCost = cost; best = std::move(sorted); }
            return;
        }
        for (int p : options[pos]) { current.push_back(p); visit(pos + 1); current.pop_back(); }
    };
    visit(0);
    return best;
}

std::vector<std::uint8_t> vlq(std::uint32_t value) {
    std::uint8_t buffer[4]{}; int count = 0;
    buffer[count++] = static_cast<std::uint8_t>(value & 0x7fU);
    while ((value >>= 7U) != 0U) buffer[count++] = static_cast<std::uint8_t>((value & 0x7fU) | 0x80U);
    std::vector<std::uint8_t> out; while (count) out.push_back(buffer[--count]); return out;
}

void push32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 24)); b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8)); b.push_back(static_cast<std::uint8_t>(v));
}
void push16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v >> 8)); b.push_back(static_cast<std::uint8_t>(v)); }
void chunk(std::vector<std::uint8_t>& out, const char* id, const std::vector<std::uint8_t>& payload) {
    out.insert(out.end(), id, id + 4); push32(out, static_cast<std::uint32_t>(payload.size())); out.insert(out.end(), payload.begin(), payload.end());
}
void meta(std::vector<std::uint8_t>& out, std::uint8_t type, const std::vector<std::uint8_t>& data) {
    out.push_back(0xff); out.push_back(type); auto len = vlq(static_cast<std::uint32_t>(data.size())); out.insert(out.end(), len.begin(), len.end()); out.insert(out.end(), data.begin(), data.end());
}

} // namespace

std::string noteName(int pc) { return names[static_cast<size_t>((pc % 12 + 12) % 12)]; }
std::string moodName(Mood mood) {
    switch (mood) { case Mood::Dark: return "Dark"; case Mood::Melancholic: return "Melancholic";
        case Mood::Tension: return "Tension"; case Mood::Horror: return "Horror"; case Mood::Epic: return "Epic";
        case Mood::Mystery: return "Mystery"; case Mood::Release: return "Release"; case Mood::Custom: return "Custom"; }
    return "Dark";
}
std::string modeName(Mode mode) {
    switch (mode) { case Mode::Ionian: return "Ionian"; case Mode::Dorian: return "Dorian"; case Mode::Phrygian: return "Phrygian";
        case Mode::Lydian: return "Lydian"; case Mode::Mixolydian: return "Mixolydian"; case Mode::Aeolian: return "Aeolian"; case Mode::Locrian: return "Locrian"; }
    return "Ionian";
}
std::string chordName(int root, Quality quality) { return noteName(root) + suffix(quality); }
Quality parseQuality(const std::string& value) {
    if (value == "min" || value == "m") return Quality::Minor;
    if (value == "dim" || value == "°") return Quality::Diminished;
    if (value == "aug" || value == "+") return Quality::Augmented;
    if (value == "sus2") return Quality::Sus2;
    if (value == "sus4") return Quality::Sus4;
    if (value == "5") return Quality::Power;
    if (value == "7") return Quality::Dominant7;
    if (value == "maj7") return Quality::Major7;
    if (value == "m7") return Quality::Minor7;
    if (value == "m(maj7)") return Quality::MinorMajor7;
    if (value == "ø7") return Quality::HalfDiminished7;
    if (value == "°7") return Quality::Diminished7;
    if (value == "7b9") return Quality::DominantFlat9;
    if (value == "add9") return Quality::Add9;
    if (value == "m(add9)") return Quality::MinorAdd9;
    return Quality::Major;
}

Analysis analyzeSession(const Session& session) {
    Analysis a;
    a.beatsPerBar = std::max(1.0, session.numerator * 4.0 / std::max(1, session.denominator));
    std::vector<Note> all;
    std::array<double, 12> histogram {};
    double minBeat = 1.0e12, maxBeat = 0.0;
    for (const auto& track : session.tracks) {
        a.trackNames[track.id] = track.name;
        std::vector<int> pitches;
        for (const auto& n : track.notes) {
            all.push_back(n); pitches.push_back(n.pitch);
            minBeat = std::min(minBeat, n.startBeat); maxBeat = std::max(maxBeat, n.endBeat);
            histogram[static_cast<size_t>((n.pitch % 12 + 12) % 12)] += std::max(.05, n.endBeat - n.startBeat) * (0.55 + n.velocity / 180.0);
        }
        if (!pitches.empty()) {
            std::sort(pitches.begin(), pitches.end());
            a.trackMedianPitch[track.id] = pitches[pitches.size() / 2];
        } else { a.trackMedianPitch[track.id] = 60.0; a.perTrackChords[track.id] = {}; }
    }
    if (all.empty()) { a.chords.clear(); return a; }
    a.originBeat = minBeat;
    a.measures = std::max(1, static_cast<int>(std::ceil((maxBeat - minBeat) / a.beatsPerBar)));
    a.chords = detectProgression(all, minBeat, maxBeat, a.beatsPerBar);
    for (const auto& track : session.tracks) if (!track.notes.empty())
        a.perTrackChords[track.id] = detectProgression(track.notes, minBeat, maxBeat, a.beatsPerBar);

    // Compare modal pitch profiles, tonic-note prominence, and chord-root support.
    double bestScore = -1.0e9, secondScore = -1.0e9;
    const std::array<double, 7> degreeProfile {{6.2, 2.5, 3.7, 2.8, 5.0, 2.8, 2.1}};
    const double total = std::accumulate(histogram.begin(), histogram.end(), 0.0);
    for (int root = 0; root < 12; ++root) for (int modeIndex = 0; modeIndex < 7; ++modeIndex) {
        const auto& scale = modes[static_cast<size_t>(modeIndex)];
        double inside = 0.0, dot = 0.0, pNorm = 0.0, hMean = total / 12.0, hNorm = 0.0;
        std::array<double, 12> profile{};
        for (int degree = 0; degree < 7; ++degree) {
            double weight = degreeProfile[static_cast<size_t>(degree)];
            if (degree == 6 && modeIndex == static_cast<int>(Mode::Ionian)) weight = 3.7;
            if (degree == 6 && (modeIndex == static_cast<int>(Mode::Dorian) || modeIndex == static_cast<int>(Mode::Mixolydian))) weight = 2.4;
            if (degree == 1 && modeIndex == static_cast<int>(Mode::Phrygian)) weight = 3.1;
            if (degree == 3 && modeIndex == static_cast<int>(Mode::Lydian)) weight = 3.6;
            if (degree == 5 && modeIndex == static_cast<int>(Mode::Dorian)) weight = 3.5;
            if (degree == 5 && modeIndex == static_cast<int>(Mode::Aeolian)) weight = 2.6;
            if (degree == 4 && modeIndex == static_cast<int>(Mode::Locrian)) weight = 2.0;
            const int pc = (root + scale[static_cast<size_t>(degree)]) % 12;
            profile[static_cast<size_t>(pc)] = weight;
            inside += histogram[static_cast<size_t>(pc)];
        }
        double pMean = std::accumulate(profile.begin(), profile.end(), 0.0) / 12.0;
        for (int pc = 0; pc < 12; ++pc) {
            const double h = histogram[static_cast<size_t>(pc)] - hMean;
            const double p = profile[static_cast<size_t>(pc)] - pMean;
            dot += h * p; hNorm += h * h; pNorm += p * p;
        }
        const double correlation = hNorm > 0.0 && pNorm > 0.0 ? dot / std::sqrt(hNorm * pNorm) : 0.0;
        double rootSupport = 0.0, supportTotal = 0.0;
        for (const auto& chord : a.chords) { const double w = std::max(.1, chord.confidence); supportTotal += w; if (chord.root == root) rootSupport += w; }
        const double support = supportTotal > 0.0 ? rootSupport / supportTotal : 0.0;
        const double prominence = total > 0.0 ? histogram[static_cast<size_t>(root)] / total : 0.0;
        const double score = .47 * correlation + .24 * (total > 0.0 ? inside / total : 0.0) + .17 * prominence + .12 * support;
        if (score > bestScore) { secondScore = bestScore; bestScore = score; a.tonic = root; a.mode = static_cast<Mode>(modeIndex); }
        else if (score > secondScore) secondScore = score;
    }
    a.keyConfidence = std::clamp((bestScore - std::max(0.0, secondScore)) * 4.0 + .35, 0.0, 1.0);
    for (auto& chord : a.chords) chord.function = romanFunction(chord,a.tonic,a.mode);
    for (auto& [trackId, chords] : a.perTrackChords) {
        (void) trackId;
        for (auto& chord : chords) chord.function = romanFunction(chord, a.tonic, a.mode);
    }
    return a;
}

std::vector<Variation> generateVariations(const Session& session, const Analysis& analysis,
                                          std::uint32_t targetTrack, const Controls& controls) {
    const std::array<std::pair<std::string, std::string>, 5> strategies {{{"Modal Shadow", "Modal Shadow"},
        {"Chromatic Mediants", "Chromatic Mediants"}, {"Tension Architecture", "Tension Architecture"},
        {"Common Tone Drift", "Common Tone Drift"}, {"Pedal & Return", "Pedal & Return"}}};
    std::vector<Chord> source = analysis.chords;
    if (source.empty()) return {};
    if (controls.changeHarmonicRhythm) {
        std::vector<Chord> expanded;
        for (const auto& chord : source) {
            if (chord.endBeat - chord.startBeat >= 3.0) {
                Chord first = chord, second = chord;
                const double midpoint = chord.startBeat + (chord.endBeat - chord.startBeat) * .5;
                first.endBeat = midpoint; second.startBeat = midpoint;
                expanded.push_back(std::move(first)); expanded.push_back(std::move(second));
            } else expanded.push_back(chord);
        }
        source = std::move(expanded);
    }
    const bool bassTarget = analysis.trackMedianPitch.count(targetTrack) && analysis.trackMedianPitch.at(targetTrack) <= 50.0;
    const std::string targetName = analysis.trackNames.count(targetTrack) ? analysis.trackNames.at(targetTrack) : "Harmony";
    std::vector<Variation> variations;
    (void) session;
    const double transform = std::clamp(controls.transformation / 100.0, 0.0, 1.0);
    const double darkness = std::clamp(controls.darkness / 100.0, 0.0, 1.0);
    const double moodT = moodTension(controls);
    for (const auto& [name, strategy] : strategies) {
        Variation out; out.name = name; out.targetTrack = targetName;
        double motion = 0.0, coherenceTotal = 0.0, transformRatio = 0.0, tensionTotal = 0.0, tensionError = 0.0;
        std::vector<int> previous;
        for (size_t i = 0; i < source.size(); ++i) {
            const auto& original = source[i]; const Chord* next = i + 1 < source.size() ? &source[i + 1] : nullptr;
            const double curve = curveAt(controls, original.measure, analysis.measures);
            const double target = std::clamp(.22 + .42 * curve + .38 * moodT + .18 * darkness, 0.0, 1.0);
            auto candidates = candidatesFor(original, next, analysis, static_cast<int>(i), strategy, target);
            double bestCost = 1.0e9; Candidate best {original.root, original.quality, "source"}; double bestT = original.confidence;
            for (const auto& candidate : candidates) {
                const double tension = candidateTension(candidate.root, candidate.quality, analysis.tonic, curve);
                const int distance = std::min((candidate.root - original.root + 12) % 12, (original.root - candidate.root + 12) % 12);
                const double transformCost = .015 * distance + (candidate.quality == original.quality ? 0.0 : .09);
                const double preservation = (1.0 - transform) * (candidate.root != original.root ? .48 : candidate.quality != original.quality ? .18 : 0.0);
                double voiceCost = 0.0; int common = 0;
                if (!out.chords.empty()) {
                    const int prevRoot = out.chords.back().root;
                    voiceCost = .014 * std::min((candidate.root - prevRoot + 12) % 12, (prevRoot - candidate.root + 12) % 12);
                    std::set<int> prevPcs, nextPcs;
                    for (int n : intervals(out.chords.back().quality)) prevPcs.insert((prevRoot + n) % 12);
                    for (int n : intervals(candidate.quality)) nextPcs.insert((candidate.root + n) % 12);
                    for (int p : prevPcs) if (nextPcs.count(p)) ++common;
                    if (controls.preserveCommonTones) voiceCost -= .025 * common;
                }
                const double targetCost = std::abs(tension - target) * ((strategy == "Tension Architecture" || strategy == "Pedal & Return") ? .42 : .24);
                double bias = 0.0;
                if (strategy == "Modal Shadow" && candidate.tag.find("borrowed") == 0) bias -= .07 * darkness;
                if (strategy == "Chromatic Mediants" && candidate.tag == "chromatic mediant") bias -= .055 * transform;
                if (strategy == "Tension Architecture" && (candidate.tag == "secondary dominant" || candidate.tag == "tritone substitute" || candidate.tag == "leading diminished")) bias -= .11 * curve * transform;
                if (strategy == "Common Tone Drift" && candidate.tag == "common-tone color") bias -= .055;
                if (strategy == "Pedal & Return" && candidate.tag == "pedal color") bias -= .035;
                if (controls.mood == Mood::Release && curve < .4 && candidate.root == analysis.tonic) bias -= .08;
                if (candidate.tag == "tritone substitute" && (curve < .55 || controls.darkness < 30)) bias += .18;
                const double cost = transform * transformCost + preservation + voiceCost + targetCost + bias;
                if (cost < bestCost) { bestCost = cost; best = candidate; bestT = tension; }
            }
            if (i + 1 == source.size() && strategy == "Pedal & Return" && transform > .22) {
                best.root = analysis.tonic; best.quality = analysis.mode == Mode::Aeolian || analysis.mode == Mode::Dorian || analysis.mode == Mode::Phrygian ? Quality::Minor : Quality::Major; bestT = .12;
            } else if (i + 1 == source.size() && controls.mood == Mood::Release && curve < .4) {
                best.root = analysis.tonic; best.quality = analysis.mode == Mode::Aeolian ? Quality::Minor : Quality::Major; bestT = .12;
            }
            Chord changed = original; changed.root = best.root; changed.quality = best.quality; changed.label = chordName(best.root, best.quality);
            changed.bassPitchClass = best.root;
            changed.function = romanFunction(changed, analysis.tonic, analysis.mode);
            out.chords.push_back(changed); tensionTotal += bestT;
            tensionError += std::abs(bestT - target);
            if (changed.root != original.root || changed.quality != original.quality) transformRatio += 1.0;
            if (!i) {
                coherenceTotal += std::clamp(original.confidence, 0.0, 1.0);
            } else {
                const auto& previousChord = out.chords[out.chords.size() - 2];
                std::set<int> previousPcs, currentPcs;
                for (int n : intervals(previousChord.quality)) previousPcs.insert((previousChord.root + n) % 12);
                for (int n : intervals(changed.quality)) currentPcs.insert((changed.root + n) % 12);
                int common = 0;
                for (int pc : previousPcs) if (currentPcs.count(pc)) ++common;
                const double sharedToneRatio = static_cast<double>(common) /
                    std::max<size_t>(1, previousPcs.size() + currentPcs.size() - common);
                const int rootDistance = std::min((changed.root - previousChord.root + 12) % 12,
                                                  (previousChord.root - changed.root + 12) % 12);
                const double rootLink = 1.0 - rootDistance / 9.0;
                coherenceTotal += std::clamp(.62 * sharedToneRatio + .38 * rootLink, 0.0, 1.0);
            }
            auto voicing = bassTarget ? std::vector<int>{} : chordVoicing(changed.root, changed.quality, controls.density,
                                    controls.lowPitch, controls.highPitch, previous, controls.preserveCommonTones);
            if (bassTarget && !controls.preserveBass) {
                int pitch = 36 + changed.root;
                while (pitch > 52) pitch -= 12;
                voicing.push_back(pitch);
            }
            if (!previous.empty() && !voicing.empty()) {
                for (size_t v = 0; v < std::min(previous.size(), voicing.size()); ++v) motion += std::min({std::abs(voicing[v] - previous[v]), std::abs(voicing[v] - previous[v] - 12), std::abs(voicing[v] - previous[v] + 12)});
            }
            if (!bassTarget || !controls.preserveBass)
                for (int pitch : voicing) out.notes.push_back({changed.startBeat, changed.endBeat, pitch, bassTarget ? 90 : 82, 0});
            previous = std::move(voicing);
        }
        if (bassTarget && controls.preserveBass) {
            auto sourceTrack = std::find_if(session.tracks.begin(), session.tracks.end(),
                [targetTrack](const auto& track) { return track.id == targetTrack; });
            if (sourceTrack != session.tracks.end()) for (const auto& sourceNote : sourceTrack->notes) {
                const double beat = sourceNote.startBeat - analysis.originBeat;
                const auto active = std::find_if(out.chords.begin(), out.chords.end(), [beat](const auto& chord) {
                    return beat >= chord.startBeat && beat < chord.endBeat;
                });
                const int root = active == out.chords.end() ? out.chords.back().root : active->root;
                int pitch = 28 + ((root - 28 % 12 + 12) % 12);
                int bestPitch = pitch; int bestDistance = std::abs(pitch - sourceNote.pitch);
                for (int p = 28; p <= 55; ++p) if (p % 12 == root && std::abs(p-sourceNote.pitch) < bestDistance) {
                    bestDistance=std::abs(p-sourceNote.pitch); bestPitch=p;
                }
                out.notes.push_back({beat, std::max(beat + .01, sourceNote.endBeat - analysis.originBeat), bestPitch,
                                     sourceNote.velocity, sourceNote.channel});
            }
            std::vector<const Note*> orderedBass;
            orderedBass.reserve(out.notes.size());
            for (const auto& note : out.notes) orderedBass.push_back(&note);
            std::sort(orderedBass.begin(), orderedBass.end(), [](const Note* a, const Note* b) {
                return a->startBeat < b->startBeat;
            });
            for (size_t i = 1; i < orderedBass.size(); ++i) {
                const int distance = std::abs(orderedBass[i]->pitch - orderedBass[i - 1]->pitch);
                motion += std::min({distance, std::abs(distance - 12), std::abs(distance + 12)});
            }
        }
        const double count = std::max<size_t>(1, out.chords.size());
        out.meanTension = tensionTotal / count;
        out.scores.voiceLeading = std::clamp(1.0 - (motion / count) / 18.0, 0.0, 1.0);
        out.scores.coherence = coherenceTotal / count;
        out.scores.transformation = std::clamp(1.0 - std::abs(transformRatio / count - transform), 0.0, 1.0);
        out.scores.tensionCurve = std::clamp(1.0 - tensionError / count, 0.0, 1.0);
        out.score = .32 * out.scores.voiceLeading + .28 * out.scores.coherence + .20 * out.scores.transformation + .20 * out.scores.tensionCurve;
        variations.push_back(std::move(out));
    }
    std::sort(variations.begin(), variations.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    return variations;
}

AnalysisResult generateFromScratch(int tonic, Mode mode, int measures, const Session& timing,
                                   const Controls& controls) {
    Session seed = timing; seed.tracks.clear(); seed.numerator = std::max(1, timing.numerator);
    seed.denominator = std::max(1, timing.denominator);
    Analysis analysis; analysis.tonic = (tonic % 12 + 12) % 12; analysis.mode = mode;
    analysis.beatsPerBar = seed.numerator * 4.0 / seed.denominator; analysis.measures = std::clamp(measures, 2, 64);
    const auto& scale = modes[static_cast<size_t>(mode)];
    const bool minor = mode == Mode::Aeolian || mode == Mode::Dorian || mode == Mode::Phrygian || mode == Mode::Locrian;
    const std::array<int,4> degrees = minor ? std::array<int,4>{{0,5,2,6}} : std::array<int,4>{{0,4,5,3}};
    const std::array<Quality,4> qualities = minor ? std::array<Quality,4>{{Quality::Minor,Quality::Major,Quality::Major,Quality::Major}}
                                                  : std::array<Quality,4>{{Quality::Major,Quality::Major,Quality::Minor,Quality::Major}};
    for (int i=0; i<analysis.measures; ++i) {
        const int root=(analysis.tonic + scale[static_cast<size_t>(degrees[static_cast<size_t>(i%4)])])%12;
        Chord c; c.startBeat=i*analysis.beatsPerBar; c.endBeat=(i+1)*analysis.beatsPerBar; c.root=root;
        c.quality=qualities[static_cast<size_t>(i%4)]; c.confidence=.8; c.measure=i; c.label=chordName(root,c.quality); analysis.chords.push_back(c);
    }
    analysis.keyConfidence=.8;
    auto variations = generateVariations(seed, analysis, 0, controls);
    return {analysis, std::move(variations)};
}

std::vector<std::uint8_t> writeMidiFile(const Variation& variation, const Session& session) {
    constexpr std::uint16_t ppq=480;
    std::vector<std::uint8_t> conductor;
    conductor.push_back(0); meta(conductor,0x03,{'C','o','n','d','u','c','t','o','r'});
    const auto micros=static_cast<std::uint32_t>(std::clamp(60000000.0/std::max(1.0,session.bpm),1.0,16777215.0));
    conductor.push_back(0); meta(conductor,0x51,{static_cast<std::uint8_t>(micros>>16),static_cast<std::uint8_t>(micros>>8),static_cast<std::uint8_t>(micros)});
    int denPower=0; while ((1<<denPower)<std::max(1,session.denominator) && denPower<7) ++denPower;
    conductor.push_back(0); meta(conductor,0x58,{static_cast<std::uint8_t>(std::clamp(session.numerator,1,255)),static_cast<std::uint8_t>(denPower),24,8});
    conductor.insert(conductor.end(),{0,0xff,0x2f,0});
    struct Event { std::uint32_t tick; int order; std::vector<std::uint8_t> bytes; };
    std::vector<Event> events;
    const std::string label=variation.targetTrack.empty() ? variation.name : variation.targetTrack + " - " + variation.name;
    std::vector<std::uint8_t> trackName(label.begin(),label.begin()+static_cast<std::ptrdiff_t>(std::min<size_t>(label.size(),120)));
    std::vector<std::uint8_t> named; meta(named,0x03,trackName); events.push_back({0,-5,std::move(named)});
    for (const auto& n : variation.notes) {
        const auto start=static_cast<std::uint32_t>(std::max(0.0,std::round(n.startBeat*ppq)));
        const auto end=static_cast<std::uint32_t>(std::max<double>(start+1,std::round(n.endBeat*ppq)));
        events.push_back({start,1,{static_cast<std::uint8_t>(0x90 | (n.channel&0x0f)),static_cast<std::uint8_t>(std::clamp(n.pitch,0,127)),static_cast<std::uint8_t>(std::clamp(n.velocity,1,127))}});
        events.push_back({end,0,{static_cast<std::uint8_t>(0x80 | (n.channel&0x0f)),static_cast<std::uint8_t>(std::clamp(n.pitch,0,127)),0}});
    }
    std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return std::tie(a.tick,a.order)<std::tie(b.tick,b.order);});
    std::vector<std::uint8_t> noteTrack; std::uint32_t last=0;
    for (const auto& e:events) { auto delta=vlq(e.tick-last); noteTrack.insert(noteTrack.end(),delta.begin(),delta.end()); noteTrack.insert(noteTrack.end(),e.bytes.begin(),e.bytes.end()); last=e.tick; }
    noteTrack.insert(noteTrack.end(),{0,0xff,0x2f,0});
    std::vector<std::uint8_t> out {'M','T','h','d'}; push32(out,6); push16(out,1); push16(out,2); push16(out,ppq);
    chunk(out,"MTrk",conductor); chunk(out,"MTrk",noteTrack); return out;
}

} // namespace harmonic_shadow
