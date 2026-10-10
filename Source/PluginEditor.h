#pragma once

#include "PluginProcessor.h"

class HarmonicShadowEditor;

class HarmonicShadowEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit HarmonicShadowEditor(HarmonicShadowProcessor& processor);
    ~HarmonicShadowEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshSnapshot(bool forceAnalysis);
    void updateAnalysisText();
    void refreshTargets();
    void generate();
    void applyChordCorrections();
    void persistPreferences();
    harmonic_shadow::Controls readControls() const;
    void rebuildRows();

    HarmonicShadowProcessor& processor_;
    juce::Label title_, subtitle_, status_, context_, partLabel_, chordCorrectionLabel_;
    juce::TextButton captureButton_ {"Start capture"}, clearButton_ {"Clear"}, generateButton_ {"Generate suggestions"};
    juce::ComboBox targetCombo_, moodCombo_, tonicCombo_, modeCombo_;
    juce::TextEditor partName_, tensionCurve_;
    juce::TextEditor chordCorrection_;
    juce::TextButton applyCorrections_ {"Apply chords"};
    juce::ToggleButton scratchMode_ {"Generate from key (secondary mode)"};
    juce::ToggleButton preserveCommon_ {"Keep common tones"}, changeRhythm_ {"Change harmonic rhythm"}, preserveBass_ {"Keep bass rhythm"};
    juce::Slider darkness_, transformation_, density_, measures_;
    juce::Label darknessLabel_, transformationLabel_, densityLabel_, measuresLabel_;
    juce::Viewport suggestionsViewport_;
    juce::Component suggestionsContent_;
    juce::OwnedArray<juce::Component> suggestionRows_;
    class PianoRoll;
    std::unique_ptr<PianoRoll> pianoRoll_;
    harmonic_shadow::CaptureSnapshot snapshot_;
    harmonic_shadow::Analysis analysis_;
    std::vector<harmonic_shadow::Variation> variations_;
    std::vector<std::uint32_t> targetIds_;
    std::size_t lastEventCount_ = 0;
    bool haveAnalysis_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicShadowEditor)
};
