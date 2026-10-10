#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace harmonic_shadow;

HarmonicShadowProcessor::HarmonicShadowProcessor()
    : juce::AudioProcessor(BusesProperties()) {
    trackId_ = coordinator().addInstance(&eventQueue_, "");
    trackLabel_ = "MIDI Part " + juce::String(trackId_);
}

HarmonicShadowProcessor::~HarmonicShadowProcessor() {
    coordinator().removeInstance(trackId_);
}

void HarmonicShadowProcessor::prepareToPlay(double sampleRate, int) {
    sampleRate_ = std::max(1.0, sampleRate);
    fallbackBeat_ = 0.0;
}

void HarmonicShadowProcessor::releaseResources() {}

template <typename SampleType>
void HarmonicShadowProcessor::captureBlock(juce::MidiBuffer& midi, int numSamples) {
    double blockBeat = fallbackBeat_;
    double bpm = lastBpm_;
    int numerator = 4, denominator = 4;
    bool positioned = false;
    bool playing = true;

    if (auto* playHead = getPlayHead()) {
        const auto position = playHead->getPosition();
        if (position.hasValue()) {
            playing = position->getIsPlaying();
            bpm = position->getBpm().orFallback(lastBpm_);
            const auto ppq = position->getPpqPosition();
            if (ppq.hasValue()) { blockBeat = *ppq; positioned = true; }
            const auto signature = position->getTimeSignature();
            if (signature.hasValue()) {
                numerator = signature->numerator;
                denominator = signature->denominator;
            }
        }
    }
    if (bpm <= 1.0 || bpm > 1000.0) bpm = lastBpm_;
    lastBpm_ = bpm;
    coordinator().updateTransport(bpm, numerator, denominator, positioned);

    if (playing && coordinator().isCapturing()) {
        const double beatsPerSecond = bpm / 60.0;
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (!message.isNoteOn() && !message.isNoteOff()) continue;
            const double atBeat = blockBeat + std::max(0, metadata.samplePosition) / sampleRate_ * beatsPerSecond;
            CapturedMidiEvent captured;
            captured.beat = atBeat;
            captured.pitch = static_cast<std::uint8_t>(message.getNoteNumber());
            captured.velocity = static_cast<std::uint8_t>(std::clamp(juce::roundToInt(message.getFloatVelocity() * 127.0f), 0, 127));
            captured.channel = static_cast<std::uint8_t>(std::clamp(message.getChannel() - 1, 0, 15));
            captured.noteOn = message.isNoteOn();
            eventQueue_.push(captured);
        }
    }
    fallbackBeat_ = blockBeat + (numSamples / sampleRate_) * bpm / 60.0;
}

void HarmonicShadowProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    captureBlock<float>(midi, buffer.getNumSamples());
}

void HarmonicShadowProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    captureBlock<double>(midi, buffer.getNumSamples());
}

juce::AudioProcessorEditor* HarmonicShadowProcessor::createEditor() {
    return new HarmonicShadowEditor(*this);
}

void HarmonicShadowProcessor::setTrackLabel(const juce::String& label) {
    trackLabel_ = label.trim().substring(0, 80);
    if (trackLabel_.isEmpty()) trackLabel_ = "MIDI Part " + juce::String(trackId_);
    coordinator().setLabel(trackId_, trackLabel_.toStdString());
}

void HarmonicShadowProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree state("HarmonicShadowState");
    state.setProperty("trackLabel", trackLabel_, nullptr);
    state.setProperty("instanceId", static_cast<int64>(trackId_), nullptr);
    const auto snapshot = coordinator().collect();
    state.setProperty("sessionToken", snapshot.sessionToken, nullptr);
    juce::ValueTree settings("Settings");
    settings.setProperty("mood", static_cast<int>(snapshot.controls.mood), nullptr);
    settings.setProperty("darkness", snapshot.controls.darkness, nullptr);
    settings.setProperty("transformation", snapshot.controls.transformation, nullptr);
    settings.setProperty("density", snapshot.controls.density, nullptr);
    settings.setProperty("lowPitch", snapshot.controls.lowPitch, nullptr);
    settings.setProperty("highPitch", snapshot.controls.highPitch, nullptr);
    settings.setProperty("customDissonance", snapshot.controls.customDissonance, nullptr);
    settings.setProperty("commonTones", snapshot.controls.preserveCommonTones, nullptr);
    settings.setProperty("preserveBass", snapshot.controls.preserveBass, nullptr);
    settings.setProperty("changeRhythm", snapshot.controls.changeHarmonicRhythm, nullptr);
    settings.setProperty("scratchEnabled", snapshot.scratchEnabled, nullptr);
    settings.setProperty("scratchTonic", snapshot.scratchTonic, nullptr);
    settings.setProperty("scratchMode", static_cast<int>(snapshot.scratchMode), nullptr);
    settings.setProperty("scratchMeasures", snapshot.scratchMeasures, nullptr);
    juce::String curve;
    for (size_t i = 0; i < snapshot.controls.tensionCurve.size(); ++i) {
        if (i) curve += ",";
        curve += juce::String(snapshot.controls.tensionCurve[i], 4);
    }
    settings.setProperty("tensionCurve", curve, nullptr);
    state.addChild(settings, -1, nullptr);

    juce::ValueTree captured("CapturedSession");
    captured.setProperty("bpm", snapshot.session.bpm, nullptr);
    captured.setProperty("numerator", snapshot.session.numerator, nullptr);
    captured.setProperty("denominator", snapshot.session.denominator, nullptr);
    captured.setProperty("hostTimeline", snapshot.hostTimelineAvailable, nullptr);
    for (const auto& sourceTrack : snapshot.session.tracks) {
        juce::ValueTree track("Track");
        track.setProperty("id", static_cast<int64>(sourceTrack.id), nullptr);
        track.setProperty("name", sourceTrack.name, nullptr);
        for (const auto& sourceNote : sourceTrack.notes) {
            juce::ValueTree note("Note");
            note.setProperty("start", sourceNote.startBeat, nullptr);
            note.setProperty("end", sourceNote.endBeat, nullptr);
            note.setProperty("pitch", sourceNote.pitch, nullptr);
            note.setProperty("velocity", sourceNote.velocity, nullptr);
            note.setProperty("channel", sourceNote.channel, nullptr);
            track.addChild(note, -1, nullptr);
        }
        captured.addChild(track, -1, nullptr);
    }
    state.addChild(captured, -1, nullptr);
    juce::ValueTree manualChords("ManualChords");
    for (const auto& sourceChord : snapshot.manualChordOverrides) {
        juce::ValueTree chord("Chord");
        chord.setProperty("start", sourceChord.startBeat, nullptr);
        chord.setProperty("end", sourceChord.endBeat, nullptr);
        chord.setProperty("root", sourceChord.root, nullptr);
        chord.setProperty("quality", static_cast<int>(sourceChord.quality), nullptr);
        chord.setProperty("confidence", sourceChord.confidence, nullptr);
        chord.setProperty("measure", sourceChord.measure, nullptr);
        manualChords.addChild(chord, -1, nullptr);
    }
    state.addChild(manualChords, -1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void HarmonicShadowProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        const auto state = juce::ValueTree::fromXml(*xml);
        if (!state.hasType("HarmonicShadowState")) return;
        setTrackLabel(state.getProperty("trackLabel", "").toString());

        CaptureSnapshot snapshot;
        snapshot.sessionToken = state.getProperty("sessionToken", "").toString().toStdString();
        const auto savedInstanceId = static_cast<std::uint32_t>(std::max<int64>(0, state.getProperty("instanceId", trackId_)));
        const auto settings = state.getChildWithName("Settings");
        if (settings.isValid()) {
            snapshot.controls.mood = static_cast<Mood>(std::clamp(static_cast<int>(settings.getProperty("mood", 0)), 0, 7));
            snapshot.controls.darkness = std::clamp(static_cast<int>(settings.getProperty("darkness", 45)), 0, 100);
            snapshot.controls.transformation = std::clamp(static_cast<int>(settings.getProperty("transformation", 42)), 0, 100);
            snapshot.controls.density = std::clamp(static_cast<int>(settings.getProperty("density", 4)), 2, 5);
            snapshot.controls.lowPitch = std::clamp(static_cast<int>(settings.getProperty("lowPitch", 42)), 24, 95);
            snapshot.controls.highPitch = std::clamp(static_cast<int>(settings.getProperty("highPitch", 84)), snapshot.controls.lowPitch + 12, 108);
            snapshot.controls.customDissonance = std::clamp(static_cast<int>(settings.getProperty("customDissonance", 45)), 0, 100);
            snapshot.controls.preserveCommonTones = static_cast<bool>(settings.getProperty("commonTones", true));
            snapshot.controls.preserveBass = static_cast<bool>(settings.getProperty("preserveBass", true));
            snapshot.controls.changeHarmonicRhythm = static_cast<bool>(settings.getProperty("changeRhythm", false));
            snapshot.scratchEnabled = static_cast<bool>(settings.getProperty("scratchEnabled", false));
            snapshot.scratchTonic = std::clamp(static_cast<int>(settings.getProperty("scratchTonic", 0)), 0, 11);
            snapshot.scratchMode = static_cast<Mode>(std::clamp(static_cast<int>(settings.getProperty("scratchMode", 0)), 0, 6));
            snapshot.scratchMeasures = std::clamp(static_cast<int>(settings.getProperty("scratchMeasures", 8)), 2, 64);
            juce::StringArray points;
            points.addTokens(settings.getProperty("tensionCurve", "").toString(), ",", "");
            for (const auto& point : points) if (point.trim().isNotEmpty())
                snapshot.controls.tensionCurve.push_back(std::clamp(point.getDoubleValue(), 0.0, 1.0));
        }

        const auto captured = state.getChildWithName("CapturedSession");
        if (captured.isValid()) {
            snapshot.session.bpm = static_cast<double>(captured.getProperty("bpm", 120.0));
            snapshot.session.numerator = std::clamp(static_cast<int>(captured.getProperty("numerator", 4)), 1, 32);
            snapshot.session.denominator = std::clamp(static_cast<int>(captured.getProperty("denominator", 4)), 1, 32);
            snapshot.hostTimelineAvailable = static_cast<bool>(captured.getProperty("hostTimeline", true));
            for (int ti = 0; ti < captured.getNumChildren(); ++ti) {
                const auto savedTrack = captured.getChild(ti);
                if (!savedTrack.hasType("Track")) continue;
                TrackPart track;
                track.id = static_cast<std::uint32_t>(static_cast<int64>(savedTrack.getProperty("id", 0)));
                track.name = savedTrack.getProperty("name", "MIDI Part").toString().toStdString();
                for (int ni = 0; ni < savedTrack.getNumChildren(); ++ni) {
                    const auto savedNote = savedTrack.getChild(ni);
                    if (!savedNote.hasType("Note")) continue;
                    Note note;
                    note.startBeat = static_cast<double>(savedNote.getProperty("start", 0.0));
                    note.endBeat = static_cast<double>(savedNote.getProperty("end", note.startBeat + .25));
                    note.pitch = std::clamp(static_cast<int>(savedNote.getProperty("pitch", 60)), 0, 127);
                    note.velocity = std::clamp(static_cast<int>(savedNote.getProperty("velocity", 88)), 1, 127);
                    note.channel = std::clamp(static_cast<int>(savedNote.getProperty("channel", 0)), 0, 15);
                    track.notes.push_back(note);
                }
                snapshot.session.tracks.push_back(std::move(track));
            }
        }
        const auto manualChords = state.getChildWithName("ManualChords");
        if (manualChords.isValid()) for (int i = 0; i < manualChords.getNumChildren(); ++i) {
            const auto savedChord = manualChords.getChild(i);
            if (!savedChord.hasType("Chord")) continue;
            Chord chord;
            chord.startBeat = static_cast<double>(savedChord.getProperty("start", 0.0));
            chord.endBeat = static_cast<double>(savedChord.getProperty("end", 1.0));
            chord.root = std::clamp(static_cast<int>(savedChord.getProperty("root", 0)), 0, 11);
            chord.quality = static_cast<Quality>(std::clamp(static_cast<int>(savedChord.getProperty("quality", 0)), 0, 15));
            chord.confidence = static_cast<double>(savedChord.getProperty("confidence", 0.0));
            chord.measure = static_cast<int>(savedChord.getProperty("measure", 0));
            chord.label = chordName(chord.root, chord.quality);
            snapshot.manualChordOverrides.push_back(std::move(chord));
        }
        trackId_ = coordinator().restoreSnapshot(snapshot, trackId_, savedInstanceId);
    }
}

