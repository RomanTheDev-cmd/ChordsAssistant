#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <iterator>

using namespace harmonic_shadow;

namespace {
bool parseChordToken(juce::String token, int& root, Quality& quality) {
    token = token.trim();
    const std::array<std::pair<const char*, int>, 23> roots {{{"C#",1},{"Db",1},{"D#",3},{"Eb",3},{"F#",6},{"Gb",6},
        {"G#",8},{"Ab",8},{"A#",10},{"Bb",10},{"Cb",11},{"B#",0},{"C",0},{"D",2},{"E",4},{"F",5},{"G",7},{"A",9},{"B",11},
        {"C♯",1},{"D♭",1},{"E♭",3},{"F♯",6}}};
    juce::String suffix;
    bool found = false;
    for (const auto& candidate : roots) {
        const juce::String name(candidate.first);
        if (token.startsWithIgnoreCase(name)) { root = candidate.second; suffix = token.substring(name.length()); found = true; break; }
    }
    if (!found) return false;
    suffix = suffix.trim();
    if (suffix == "" || suffix == "m" || suffix == "min" || suffix == "dim" || suffix == "°" || suffix == "aug" || suffix == "+"
        || suffix == "sus2" || suffix == "sus4" || suffix == "5" || suffix == "7" || suffix == "maj7" || suffix == "m7"
        || suffix == "m(maj7)" || suffix == "ø7" || suffix == "°7" || suffix == "7b9" || suffix == "7♭9"
        || suffix == "add9" || suffix == "m(add9)") {
        auto normalized = suffix.replace("7♭9", "7b9");
        quality = parseQuality(normalized.toStdString());
        return true;
    }
    return false;
}

class VariationRow final : public juce::Component {
public:
    VariationRow(Variation variation, Session session)
        : variation_(std::move(variation)), session_(std::move(session)) {}

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(juce::Colour(0xff20232a));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(juce::Colour(0xff343943));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
        g.setColour(juce::Colour(0xffedf0f6));
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.drawText(juce::String(variation_.targetTrack) + "  ·  " + variation_.name,
                   14, 4, getWidth() - 180, 22, juce::Justification::centredLeft);
        juce::String progression;
        for (size_t i = 0; i < variation_.chords.size(); ++i) {
            if (i) progression += "   →   ";
            progression += juce::String(variation_.chords[i].label);
        }
        g.setColour(juce::Colour(0xffafb7c5));
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(progression, 14, 27, getWidth() - 220, 19, juce::Justification::centredLeft, 1);
        g.setColour(juce::Colour(0xff74d3ad));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("Score " + juce::String(variation_.score * 100.0, 0) + "   ·   drag to Logic",
                   getWidth() - 205, 12, 190, 25, juce::Justification::centredRight);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragOrigin_ = e.getPosition();
        dragged_ = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragged_ || e.getDistanceFromDragStart() < 6) return;
        dragged_ = true;
        auto bytes = writeMidiFile(variation_, session_);
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HarmonicShadow");
        dir.createDirectory();
        const auto safeTarget = juce::File::createLegalFileName(variation_.targetTrack);
        const auto safeName = juce::File::createLegalFileName(variation_.name);
        auto midiFile = dir.getChildFile(safeTarget + " - " + safeName + " - " + juce::Uuid().toString() + ".mid");
        if (!bytes.empty() && midiFile.replaceWithData(bytes.data(), bytes.size())) {
            juce::StringArray files;
            files.add(midiFile.getFullPathName());
            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
        }
    }

private:
    Variation variation_;
    Session session_;
    juce::Point<int> dragOrigin_;
    bool dragged_ = false;
};
}

class HarmonicShadowEditor::PianoRoll final : public juce::Component {
public:
    void setSession(const Session& session) { session_ = session; repaint(); }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff14161b));
        auto notes = std::vector<std::pair<std::uint32_t, const Note*>>{};
        std::unordered_map<std::uint32_t, juce::String> trackNames;
        for (const auto& track : session_.tracks) {
            trackNames[track.id] = track.name;
            for (const auto& note : track.notes) notes.emplace_back(track.id, &note);
        }
        auto area = getLocalBounds().reduced(8);
        if (notes.empty()) {
            g.setColour(juce::Colour(0xff77808e));
            g.drawFittedText("Play the selected regions while capture is active. Notes from enabled Harmonic Shadow tracks appear here.",
                             area, juce::Justification::centred, 2);
            return;
        }
        int low = 127, high = 0;
        double start = 1.0e12, end = 0.0;
        for (const auto& pair : notes) { const auto& n = *pair.second; low = std::min(low, n.pitch); high = std::max(high, n.pitch); start = std::min(start, n.startBeat); end = std::max(end, n.endBeat); }
        low = std::max(0, low - 2); high = std::min(127, high + 2);
        const int labelWidth = 42;
        auto plot = area.withTrimmedLeft(labelWidth).reduced(0, 3);
        const double span = std::max(1.0, end - start);
        const double rowHeight = static_cast<double>(plot.getHeight()) / std::max(1, high - low + 1);
        const double beatWidth = static_cast<double>(plot.getWidth()) / span;
        for (int pitch = low; pitch <= high; ++pitch) {
            const int y = plot.getBottom() - static_cast<int>((pitch - low + 1) * rowHeight);
            if (pitch % 12 == 0) {
                g.setColour(juce::Colour(0xff20242b));
                g.fillRect(plot.getX(), y, plot.getWidth(), static_cast<int>(std::ceil(rowHeight)));
            }
            g.setColour(juce::Colour(0xff4c5562));
            g.drawHorizontalLine(y, static_cast<float>(plot.getX()), static_cast<float>(plot.getRight()));
            if (pitch % 12 == 0) {
                g.setColour(juce::Colour(0xff99a2b0));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(juce::String(noteName(pitch % 12)), area.getX(), y - 6, labelWidth - 4, 13, juce::Justification::centredRight);
            }
        }
        for (double beat = std::ceil(start); beat <= end; beat += 1.0) {
            const auto x = plot.getX() + static_cast<int>((beat - start) * beatWidth);
            g.setColour(std::fmod(beat, session_.numerator * 4.0 / std::max(1, session_.denominator)) < .01
                            ? juce::Colour(0xff515b6a) : juce::Colour(0xff2c323c));
            g.drawVerticalLine(x, static_cast<float>(plot.getY()), static_cast<float>(plot.getBottom()));
        }
        const std::array<juce::Colour, 8> colors {{juce::Colour(0xff69c6f0),juce::Colour(0xffc18cf0),juce::Colour(0xffffb454),
            juce::Colour(0xff78d6a6),juce::Colour(0xfff0789c),juce::Colour(0xff88a3ff),juce::Colour(0xffe3d066),juce::Colour(0xff65d0c6)}};
        for (const auto& pair : notes) {
            const auto& n = *pair.second;
            auto trackIt = std::find_if(session_.tracks.begin(), session_.tracks.end(), [&](const auto& t) { return t.id == pair.first; });
            const auto colorIndex = trackIt == session_.tracks.end() ? 0u : static_cast<unsigned>(std::distance(session_.tracks.begin(), trackIt)) % colors.size();
            const int x = plot.getX() + static_cast<int>((n.startBeat - start) * beatWidth);
            const int y = plot.getBottom() - static_cast<int>((n.pitch - low + 1) * rowHeight);
            const int width = std::max(2, static_cast<int>((n.endBeat - n.startBeat) * beatWidth));
            g.setColour(colors[colorIndex]);
            g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y + 1), static_cast<float>(width),
                                   static_cast<float>(std::max(2.0, rowHeight - 2)), 2.0f);
        }
    }
private:
    Session session_;
};

HarmonicShadowEditor::HarmonicShadowEditor(HarmonicShadowProcessor& processorInstance)
    : AudioProcessorEditor(&processorInstance), processor_(processorInstance), pianoRoll_(std::make_unique<PianoRoll>()) {
    setSize(1180, 780);
    setResizable(true, true);
    setResizeLimits(960, 660, 1800, 1200);

    title_.setText("HARMONIC SHADOW", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, juce::Colour(0xffeef1f6));
    subtitle_.setText("Phrase-level MIDI harmony capture for Logic Pro", juce::dontSendNotification);
    subtitle_.setColour(juce::Label::textColourId, juce::Colour(0xff9ba5b4));
    status_.setColour(juce::Label::textColourId, juce::Colour(0xff83d4b0));
    status_.setJustificationType(juce::Justification::centredLeft);
    context_.setColour(juce::Label::textColourId, juce::Colour(0xffd5dbe5));
    context_.setJustificationType(juce::Justification::topLeft);
    context_.setMinimumHorizontalScale(0.75f);
    partLabel_.setText("This instance label", juce::dontSendNotification);
    partLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffa9b1bf));
    partName_.setText(processor_.trackLabel(), juce::dontSendNotification);
    partName_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20232a));
    partName_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffedf0f6));
    partName_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3c434e));
    partName_.onReturnKey = [this] { processor_.setTrackLabel(partName_.getText()); refreshSnapshot(false); };
    partName_.onFocusLost = [this] { processor_.setTrackLabel(partName_.getText()); refreshSnapshot(false); };

    for (const auto& mood : {"Dark", "Melancholic", "Tension", "Horror", "Epic", "Mystery", "Release", "Custom"}) moodCombo_.addItem(mood, moodCombo_.getNumItems() + 1);
    moodCombo_.setSelectedId(1);
    for (int i = 0; i < 12; ++i) tonicCombo_.addItem(noteName(i), i + 1);
    for (const auto& mode : {"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian"}) modeCombo_.addItem(mode, modeCombo_.getNumItems() + 1);
    tonicCombo_.setSelectedId(1); modeCombo_.setSelectedId(1);
    for (auto* slider : {&darkness_, &transformation_, &density_, &measures_, &lowPitch_, &highPitch_}) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
        slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff7780c8));
        slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffd3d8ff));
        slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe7eaf0));
        slider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff20232a));
    }
    darkness_.setRange(0, 100, 1); darkness_.setValue(45);
    transformation_.setRange(0, 100, 1); transformation_.setValue(42);
    density_.setRange(2, 5, 1); density_.setValue(4);
    measures_.setRange(2, 32, 1); measures_.setValue(8);
    lowPitch_.setRange(24, 95, 1); lowPitch_.setValue(42);
    highPitch_.setRange(36, 108, 1); highPitch_.setValue(84);
    darknessLabel_.setText("Darkness", juce::dontSendNotification);
    transformationLabel_.setText("Transformation", juce::dontSendNotification);
    densityLabel_.setText("Chord density", juce::dontSendNotification);
    measuresLabel_.setText("Bars", juce::dontSendNotification);
    lowPitchLabel_.setText("Low note", juce::dontSendNotification);
    highPitchLabel_.setText("High note", juce::dontSendNotification);
    for (auto* label : {&darknessLabel_, &transformationLabel_, &densityLabel_, &measuresLabel_, &lowPitchLabel_, &highPitchLabel_})
        label->setColour(juce::Label::textColourId, juce::Colour(0xffabb4c2));

    tensionCurve_.setText("0.20, 0.35, 0.50, 0.75, 0.95, 0.45, 0.20", false);
    tensionCurve_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20232a));
    tensionCurve_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffedf0f6));
    tensionCurve_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3c434e));
    tensionCurve_.setTooltip("Tension per phrase point from 0 to 1, comma-separated. Example: 0.2, 0.5, 0.9, 0.3");
    chordCorrectionLabel_.setText("Chord correction", juce::dontSendNotification);
    chordCorrectionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffaab3c1));
    chordCorrection_.setTooltip("Edit the detected chord sequence as comma-separated chord names; keep the same number of events.");
    chordCorrection_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff20232a));
    chordCorrection_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffedf0f6));
    chordCorrection_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3c434e));
    applyCorrections_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343943));
    applyCorrections_.onClick = [this] { applyChordCorrections(); };
    scratchMode_.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffb9c2cf));
    preserveCommon_.setToggleState(true, juce::dontSendNotification);
    for (auto* toggle : {&scratchMode_, &preserveCommon_, &changeRhythm_, &preserveBass_}) toggle->setColour(juce::ToggleButton::textColourId, juce::Colour(0xffb9c2cf));

    captureButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4654a4));
    generateButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4654a4));
    clearButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343943));
    captureButton_.onClick = [this] {
        if (processor_.coordinator().isCapturing()) {
            processor_.coordinator().stopCapture();
            refreshSnapshot(true);
        } else {
            processor_.coordinator().startCapture();
            variations_.clear(); rebuildRows(); haveAnalysis_ = false; lastEventCount_ = 0;
            refreshSnapshot(false);
        }
    };
    clearButton_.onClick = [this] {
        processor_.coordinator().clear(); variations_.clear(); haveAnalysis_ = false; lastEventCount_ = 0;
        refreshSnapshot(true); rebuildRows();
    };
    generateButton_.onClick = [this] { generate(); };

    const auto persist = [this] { persistPreferences(); };
    moodCombo_.onChange = persist;
    tonicCombo_.onChange = persist;
    modeCombo_.onChange = persist;
    tensionCurve_.onTextChange = persist;
    scratchMode_.onClick = persist;
    preserveCommon_.onClick = persist;
    changeRhythm_.onClick = persist;
    preserveBass_.onClick = persist;
    for (auto* slider : {&darkness_, &transformation_, &density_, &measures_, &lowPitch_, &highPitch_}) slider->onValueChange = persist;
    lowPitch_.onValueChange = [this, persist] {
        if (highPitch_.getValue() < lowPitch_.getValue() + 12)
            highPitch_.setValue(lowPitch_.getValue() + 12, juce::dontSendNotification);
        persist();
    };
    highPitch_.onValueChange = [this, persist] {
        if (lowPitch_.getValue() > highPitch_.getValue() - 12)
            lowPitch_.setValue(highPitch_.getValue() - 12, juce::dontSendNotification);
        persist();
    };

    const auto saved = processor_.coordinator().collect();
    moodCombo_.setSelectedId(static_cast<int>(saved.controls.mood) + 1, juce::dontSendNotification);
    darkness_.setValue(saved.controls.darkness, juce::dontSendNotification);
    transformation_.setValue(saved.controls.transformation, juce::dontSendNotification);
    density_.setValue(saved.controls.density, juce::dontSendNotification);
    lowPitch_.setValue(saved.controls.lowPitch, juce::dontSendNotification);
    highPitch_.setValue(saved.controls.highPitch, juce::dontSendNotification);
    preserveCommon_.setToggleState(saved.controls.preserveCommonTones, juce::dontSendNotification);
    preserveBass_.setToggleState(saved.controls.preserveBass, juce::dontSendNotification);
    changeRhythm_.setToggleState(saved.controls.changeHarmonicRhythm, juce::dontSendNotification);
    scratchMode_.setToggleState(saved.scratchEnabled, juce::dontSendNotification);
    tonicCombo_.setSelectedId(saved.scratchTonic + 1, juce::dontSendNotification);
    modeCombo_.setSelectedId(static_cast<int>(saved.scratchMode) + 1, juce::dontSendNotification);
    measures_.setValue(saved.scratchMeasures, juce::dontSendNotification);
    if (!saved.controls.tensionCurve.empty()) {
        juce::String savedCurve;
        for (size_t i=0; i<saved.controls.tensionCurve.size(); ++i) {
            if (i) savedCurve += ", ";
            savedCurve += juce::String(saved.controls.tensionCurve[i], 3);
        }
        tensionCurve_.setText(savedCurve, false);
    }

    suggestionsViewport_.setViewedComponent(&suggestionsContent_, false);
    suggestionsViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(title_); addAndMakeVisible(subtitle_); addAndMakeVisible(status_); addAndMakeVisible(context_);
    addAndMakeVisible(partLabel_); addAndMakeVisible(partName_); addAndMakeVisible(captureButton_); addAndMakeVisible(clearButton_);
    addAndMakeVisible(targetCombo_); addAndMakeVisible(moodCombo_); addAndMakeVisible(tonicCombo_); addAndMakeVisible(modeCombo_);
    addAndMakeVisible(darkness_); addAndMakeVisible(transformation_); addAndMakeVisible(density_); addAndMakeVisible(measures_);
    addAndMakeVisible(lowPitch_); addAndMakeVisible(highPitch_);
    addAndMakeVisible(darknessLabel_); addAndMakeVisible(transformationLabel_); addAndMakeVisible(densityLabel_); addAndMakeVisible(measuresLabel_);
    addAndMakeVisible(lowPitchLabel_); addAndMakeVisible(highPitchLabel_);
    addAndMakeVisible(tensionCurve_); addAndMakeVisible(scratchMode_); addAndMakeVisible(preserveCommon_); addAndMakeVisible(changeRhythm_); addAndMakeVisible(preserveBass_);
    addAndMakeVisible(chordCorrectionLabel_); addAndMakeVisible(chordCorrection_); addAndMakeVisible(applyCorrections_);
    addAndMakeVisible(generateButton_); addAndMakeVisible(suggestionsViewport_); addAndMakeVisible(*pianoRoll_);
    startTimerHz(8);
    refreshSnapshot(true);
}

HarmonicShadowEditor::~HarmonicShadowEditor() { stopTimer(); }

void HarmonicShadowEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff101216));
    g.setColour(juce::Colour(0xff1b1e24));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 12.0f);
    g.setColour(juce::Colour(0xff303640));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 12.0f, 1.0f);
    g.setColour(juce::Colour(0xffaab3c1));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("TENSION CURVE", 490, 130, 120, 15, juce::Justification::centredLeft);
    g.drawText("CAPTURED MIDI", 20, 284, 130, 18, juce::Justification::centredLeft);
    g.drawText("HARMONIC CONTEXT", 20, getHeight() - 248, 160, 18, juce::Justification::centredLeft);
    g.drawText("DRAG A SUGGESTION INTO LOGIC", 20, getHeight() - 128, 230, 18, juce::Justification::centredLeft);
}

void HarmonicShadowEditor::resized() {
    auto b = getLocalBounds().reduced(20);
    title_.setBounds(b.removeFromTop(32));
    subtitle_.setBounds(20, 51, 560, 22);
    captureButton_.setBounds(getWidth() - 180, 19, 118, 34);
    clearButton_.setBounds(getWidth() - 56, 19, 42, 34);

    moodCombo_.setBounds(20, 82, 150, 28);
    darknessLabel_.setBounds(184, 73, 112, 18); darkness_.setBounds(184, 91, 190, 27);
    transformationLabel_.setBounds(382, 73, 130, 18); transformation_.setBounds(382, 91, 200, 27);
    densityLabel_.setBounds(590, 73, 110, 18); density_.setBounds(590, 91, 170, 27);
    targetCombo_.setBounds(770, 82, std::max(150, getWidth() - 790), 28);
    partLabel_.setBounds(20, 119, 120, 20); partName_.setBounds(142, 116, 180, 26);
    scratchMode_.setBounds(338, 116, 245, 26);
    tonicCombo_.setBounds(590, 116, 70, 26); modeCombo_.setBounds(667, 116, 118, 26);
    measuresLabel_.setBounds(795, 119, 44, 20); measures_.setBounds(838, 116, 137, 26);
    preserveCommon_.setBounds(20, 149, 145, 24); changeRhythm_.setBounds(170, 149, 165, 24);
    preserveBass_.setBounds(340, 149, 145, 24);
    const int rowRight = getWidth() - 20;
    const int generateWidth = std::clamp(rowRight - 820, 130, 180);
    const int generateX = rowRight - generateWidth;
    tensionCurve_.setBounds(490, 150, std::max(110, generateX - 500), 24);
    generateButton_.setBounds(generateX, 148, generateWidth, 29);
    lowPitchLabel_.setBounds(20, 184, 68, 22); lowPitch_.setBounds(88, 181, 170, 27);
    highPitchLabel_.setBounds(272, 184, 70, 22); highPitch_.setBounds(340, 181, 170, 27);
    status_.setBounds(20, 213, getWidth() - 40, 28);
    chordCorrectionLabel_.setBounds(20, 247, 110, 25);
    chordCorrection_.setBounds(132, 247, std::max(240, getWidth() - 282), 25);
    applyCorrections_.setBounds(getWidth() - 140, 246, 120, 27);
    pianoRoll_->setBounds(20, 305, getWidth() - 40, 175);
    context_.setBounds(20, getHeight() - 222, getWidth() - 40, 82);
    suggestionsViewport_.setBounds(20, getHeight() - 102, getWidth() - 40, 88);
    repaint();
}

Controls HarmonicShadowEditor::readControls() const {
    Controls c;
    switch (moodCombo_.getSelectedItemIndex()) {
        case 1: c.mood = Mood::Melancholic; break; case 2: c.mood = Mood::Tension; break;
        case 3: c.mood = Mood::Horror; break; case 4: c.mood = Mood::Epic; break;
        case 5: c.mood = Mood::Mystery; break; case 6: c.mood = Mood::Release; break;
        case 7: c.mood = Mood::Custom; break; default: c.mood = Mood::Dark; break;
    }
    c.darkness = juce::roundToInt(darkness_.getValue());
    c.transformation = juce::roundToInt(transformation_.getValue());
    c.density = juce::roundToInt(density_.getValue());
    c.customDissonance = c.darkness;
    c.preserveCommonTones = preserveCommon_.getToggleState();
    c.preserveBass = preserveBass_.getToggleState();
    c.changeHarmonicRhythm = changeRhythm_.getToggleState();
    c.lowPitch = juce::roundToInt(lowPitch_.getValue());
    c.highPitch = juce::roundToInt(highPitch_.getValue());
    juce::StringArray values; values.addTokens(tensionCurve_.getText(), ",", "");
    for (const auto& value : values) if (value.trim().isNotEmpty()) c.tensionCurve.push_back(std::clamp(value.getDoubleValue(), 0.0, 1.0));
    return c;
}

void HarmonicShadowEditor::refreshSnapshot(bool forceAnalysis) {
    snapshot_ = processor_.coordinator().collect();
    moodCombo_.setSelectedId(static_cast<int>(snapshot_.controls.mood) + 1, juce::dontSendNotification);
    darkness_.setValue(snapshot_.controls.darkness, juce::dontSendNotification);
    transformation_.setValue(snapshot_.controls.transformation, juce::dontSendNotification);
    density_.setValue(snapshot_.controls.density, juce::dontSendNotification);
    lowPitch_.setValue(snapshot_.controls.lowPitch, juce::dontSendNotification);
    highPitch_.setValue(snapshot_.controls.highPitch, juce::dontSendNotification);
    preserveCommon_.setToggleState(snapshot_.controls.preserveCommonTones, juce::dontSendNotification);
    preserveBass_.setToggleState(snapshot_.controls.preserveBass, juce::dontSendNotification);
    changeRhythm_.setToggleState(snapshot_.controls.changeHarmonicRhythm, juce::dontSendNotification);
    scratchMode_.setToggleState(snapshot_.scratchEnabled, juce::dontSendNotification);
    tonicCombo_.setSelectedId(snapshot_.scratchTonic + 1, juce::dontSendNotification);
    modeCombo_.setSelectedId(static_cast<int>(snapshot_.scratchMode) + 1, juce::dontSendNotification);
    measures_.setValue(snapshot_.scratchMeasures, juce::dontSendNotification);
    if (!snapshot_.controls.tensionCurve.empty()) {
        juce::String curve;
        for (size_t i = 0; i < snapshot_.controls.tensionCurve.size(); ++i) {
            if (i) curve += ", ";
            curve += juce::String(snapshot_.controls.tensionCurve[i], 3);
        }
        if (!tensionCurve_.hasKeyboardFocus(true) && curve != tensionCurve_.getText()) tensionCurve_.setText(curve, false);
    }
    const auto count = snapshot_.eventCount;
    const bool capturing = snapshot_.capturing;
    captureButton_.setButtonText(capturing ? "Stop & analyze" : "Start capture");
    if (capturing) {
        status_.setText("Capturing phrase… play the passage from its start; active Harmonic Shadow instances are combined.", juce::dontSendNotification);
        if (count > lastEventCount_) { lastEventCount_ = count; }
    } else if (!variations_.empty()) {
        // Preserve the generated result while the timer refreshes shared capture state.
    } else if (!snapshot_.hostTimelineAvailable) {
        status_.setText("Host beat position unavailable. Captured notes may not align across tracks in this host.", juce::dontSendNotification);
    } else {
        status_.setText(juce::String(static_cast<int>(count)) + " notes captured across " +
            juce::String(static_cast<int>(snapshot_.session.tracks.size())) + " MIDI FX instances.", juce::dontSendNotification);
    }
    pianoRoll_->setSession(snapshot_.session);
    refreshTargets();
    if (!capturing && (forceAnalysis || count != lastEventCount_)) {
        lastEventCount_ = count;
        if (count > 0) {
            analysis_ = analyzeSession(snapshot_.session);
            if (snapshot_.manualChordOverrides.size() == analysis_.chords.size()) {
                for (size_t i=0; i<analysis_.chords.size(); ++i) {
                    analysis_.chords[i].root = snapshot_.manualChordOverrides[i].root;
                    analysis_.chords[i].quality = snapshot_.manualChordOverrides[i].quality;
                    analysis_.chords[i].label = chordName(analysis_.chords[i].root, analysis_.chords[i].quality);
                }
            }
            haveAnalysis_ = true;
        } else haveAnalysis_ = false;
        updateAnalysisText();
    }
}

void HarmonicShadowEditor::refreshTargets() {
    const auto selected = targetCombo_.getSelectedItemIndex();
    const auto selectedId = selected > 0 && selected - 1 < static_cast<int>(targetIds_.size())
        ? targetIds_[static_cast<size_t>(selected - 1)] : 0u;
    targetCombo_.clear(juce::dontSendNotification);
    targetCombo_.addItem("All captured parts", 1);
    targetIds_.clear();
    int restoredSelection = 0;
    for (const auto& track : snapshot_.session.tracks) {
        targetIds_.push_back(track.id);
        const auto noteCount = track.notes.size();
        targetCombo_.addItem(juce::String(track.name) + " (" + juce::String(static_cast<int>(noteCount)) + ")",
                             static_cast<int>(targetIds_.size()) + 1);
        if (track.id == selectedId) restoredSelection = static_cast<int>(targetIds_.size());
    }
    targetCombo_.setSelectedItemIndex(restoredSelection, juce::dontSendNotification);
    const auto ownTrack = std::find_if(snapshot_.session.tracks.begin(), snapshot_.session.tracks.end(),
        [this](const TrackPart& track) { return track.id == processor_.trackId(); });
    if (ownTrack != snapshot_.session.tracks.end()) partName_.setText(ownTrack->name, juce::dontSendNotification);
}

void HarmonicShadowEditor::updateAnalysisText() {
    if (!haveAnalysis_) {
        context_.setText("No phrase analyzed yet. Insert this MIDI FX on each part you want included, start capture, then play the phrase through Logic.", juce::dontSendNotification);
        return;
    }
    juce::String text = juce::String("Estimated center: ") + juce::String(noteName(analysis_.tonic)) + " " + juce::String(modeName(analysis_.mode))
        + "   ·   confidence " + juce::String(analysis_.keyConfidence * 100.0, 0) + "%\nChord path: ";
    for (size_t i = 0; i < analysis_.chords.size(); ++i) {
        if (i) text += "  →  ";
        text += juce::String(analysis_.chords[i].label);
        if (!analysis_.chords[i].function.empty())
            text += " [" + juce::String(analysis_.chords[i].function) + "]";
    }
    juce::String editable;
    for (size_t i = 0; i < analysis_.chords.size(); ++i) {
        if (i) editable += ", ";
        editable += juce::String(analysis_.chords[i].label);
    }
    chordCorrection_.setText(editable, false);
    text += "\nParts: ";
    for (size_t i = 0; i < snapshot_.session.tracks.size(); ++i) {
        if (i) text += "   ·   ";
        const auto& track = snapshot_.session.tracks[i];
        text += juce::String(track.name) + " " + juce::String(static_cast<int>(track.notes.size())) + " notes";
    }
    context_.setText(text, juce::dontSendNotification);
}

void HarmonicShadowEditor::applyChordCorrections() {
    if (!haveAnalysis_ || analysis_.chords.empty()) {
        status_.setText("Capture a phrase before correcting its chord analysis.", juce::dontSendNotification);
        return;
    }
    juce::StringArray tokens; tokens.addTokens(chordCorrection_.getText(), ",", "");
    if (tokens.size() != static_cast<int>(analysis_.chords.size())) {
        status_.setText("Enter one chord per detected event, separated by commas.", juce::dontSendNotification);
        return;
    }
    std::vector<std::pair<int, Quality>> parsed;
    for (auto token : tokens) {
        int root = 0; Quality quality = Quality::Major;
        if (!parseChordToken(token, root, quality)) {
            status_.setText("Could not read '" + token.trim() + "'. Use names such as Cm7, F#7b9 or Bb.", juce::dontSendNotification);
            return;
        }
        parsed.emplace_back(root, quality);
    }
    for (size_t i = 0; i < parsed.size(); ++i) {
        analysis_.chords[i].root = parsed[i].first;
        analysis_.chords[i].quality = parsed[i].second;
        analysis_.chords[i].label = chordName(parsed[i].first, parsed[i].second);
    }
    processor_.coordinator().setChordOverrides(analysis_.chords);
    snapshot_.manualChordOverrides = analysis_.chords;
    updateAnalysisText();
    status_.setText("Chord corrections applied to the shared harmonic context.", juce::dontSendNotification);
}

void HarmonicShadowEditor::generate() {
    refreshSnapshot(true);
    const auto controls = readControls();
    variations_.clear();
    if (scratchMode_.getToggleState()) {
        const int tonic = std::max(0, tonicCombo_.getSelectedItemIndex());
        const auto modeIndex = std::clamp(modeCombo_.getSelectedItemIndex(), 0, 6);
        const auto mode = static_cast<Mode>(modeIndex);
        auto result = generateFromScratch(tonic, mode, juce::roundToInt(measures_.getValue()), snapshot_.session, controls);
        analysis_ = result.context; haveAnalysis_ = true;
        for (auto& variation : result.suggestions) { variation.targetTrack = "Scratch"; variations_.push_back(std::move(variation)); }
        updateAnalysisText();
    } else {
        if (!haveAnalysis_) {
            status_.setText("Capture MIDI first, or choose Generate from key for a new phrase.", juce::dontSendNotification);
            return;
        }
        const int selected = targetCombo_.getSelectedItemIndex();
        if (selected <= 0) {
            for (const auto& track : snapshot_.session.tracks) {
                if (track.notes.empty()) continue;
                auto items = generateVariations(snapshot_.session, analysis_, track.id, controls);
                variations_.insert(variations_.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
            }
        } else {
            const auto index = static_cast<size_t>(selected - 1);
            if (index < targetIds_.size()) variations_ = generateVariations(snapshot_.session, analysis_, targetIds_[index], controls);
        }
        if (variations_.empty()) {
            status_.setText("No notes in the selected part. Capture it during Logic playback first.", juce::dontSendNotification);
            return;
        }
    }
    status_.setText("Generated " + juce::String(static_cast<int>(variations_.size())) +
                    " phrase-aware suggestions. Drag any row into the Logic arrangement.", juce::dontSendNotification);
    rebuildRows();
}

void HarmonicShadowEditor::rebuildRows() {
    suggestionRows_.clear(true);
    int y = 4;
    for (const auto& variation : variations_) {
        auto* row = new VariationRow(variation, snapshot_.session);
        row->setBounds(4, y, std::max(500, getWidth() - 64), 54);
        suggestionsContent_.addAndMakeVisible(row);
        suggestionRows_.add(row);
        y += 58;
    }
    suggestionsContent_.setSize(std::max(500, getWidth() - 64), std::max(90, y + 4));
}

void HarmonicShadowEditor::timerCallback() {
    refreshSnapshot(false);
}

void HarmonicShadowEditor::persistPreferences() {
    const int tonic = std::max(0, tonicCombo_.getSelectedId() - 1);
    const auto mode = static_cast<Mode>(std::clamp(modeCombo_.getSelectedItemIndex(), 0, 6));
    processor_.coordinator().setPreferences(readControls(), tonic, mode,
                                             juce::roundToInt(measures_.getValue()), scratchMode_.getToggleState());
}
