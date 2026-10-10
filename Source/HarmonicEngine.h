#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace harmonic_shadow {

struct Note {
    double startBeat = 0.0;
    double endBeat = 0.0;
    int pitch = 60;
    int velocity = 88;
    int channel = 0;
};

struct TrackPart {
    std::uint32_t id = 0;
    std::string name;
    std::vector<Note> notes;
};

struct Session {
    std::vector<TrackPart> tracks;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
    double ppq = 480.0;
};

enum class Mood { Dark, Melancholic, Tension, Horror, Epic, Mystery, Release, Custom };
enum class Mode { Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian };
enum class Quality { Major, Minor, Diminished, Augmented, Sus2, Sus4, Power,
                     Dominant7, Major7, Minor7, MinorMajor7, HalfDiminished7,
                     Diminished7, DominantFlat9, Add9, MinorAdd9 };

struct Chord {
    double startBeat = 0.0;
    double endBeat = 1.0;
    int root = 0;
    Quality quality = Quality::Major;
    double confidence = 0.0;
    int bassPitchClass = -1;
    int measure = 0;
    std::string label;
    std::string function;
};

struct Analysis {
    int tonic = 0;
    Mode mode = Mode::Ionian;
    double keyConfidence = 0.0;
    double originBeat = 0.0;
    double beatsPerBar = 4.0;
    int measures = 1;
    std::vector<Chord> chords;
    std::unordered_map<std::uint32_t, std::vector<Chord>> perTrackChords;
    std::unordered_map<std::uint32_t, std::string> trackNames;
    std::unordered_map<std::uint32_t, double> trackMedianPitch;
};

struct Controls {
    Mood mood = Mood::Dark;
    int darkness = 45;
    int transformation = 42;
    int density = 4;
    int lowPitch = 42;
    int highPitch = 84;
    int customDissonance = 45;
    bool preserveCommonTones = true;
    bool preserveBass = true;
    bool changeHarmonicRhythm = false;
    std::vector<double> tensionCurve;
};

struct ScoreParts {
    double voiceLeading = 0.0;
    double coherence = 0.0;
    double transformation = 0.0;
    double tensionCurve = 0.0;
};

struct Variation {
    std::string name;
    std::string targetTrack;
    std::vector<Chord> chords;
    std::vector<Note> notes;
    double score = 0.0;
    double meanTension = 0.0;
    ScoreParts scores;
};

struct AnalysisResult {
    Analysis context;
    std::vector<Variation> suggestions;
};

std::string noteName(int pitchClass);
std::string moodName(Mood mood);
std::string modeName(Mode mode);
std::string chordName(int root, Quality quality);
Quality parseQuality(const std::string& value);

Analysis analyzeSession(const Session& session);
std::vector<Variation> generateVariations(const Session& session, const Analysis& analysis,
                                          std::uint32_t targetTrack, const Controls& controls);
AnalysisResult generateFromScratch(int tonic, Mode mode, int measures, const Session& timing,
                                   const Controls& controls);
std::vector<std::uint8_t> writeMidiFile(const Variation& variation, const Session& session);

} // namespace harmonic_shadow
