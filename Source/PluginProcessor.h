#pragma once

#include "CaptureCoordinator.h"
#include <JuceHeader.h>

class HarmonicShadowProcessor final : public juce::AudioProcessor {
public:
    HarmonicShadowProcessor();
    ~HarmonicShadowProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    std::uint32_t trackId() const noexcept { return trackId_; }
    juce::String trackLabel() const { return trackLabel_; }
    void setTrackLabel(const juce::String& label);
    harmonic_shadow::CaptureCoordinator& coordinator() const { return harmonic_shadow::CaptureCoordinator::instance(); }

private:
    template <typename SampleType>
    void captureBlock(juce::MidiBuffer& midi, int numSamples);

    harmonic_shadow::MidiEventQueue<8192> eventQueue_;
    std::uint32_t trackId_ = 0;
    juce::String trackLabel_;
    double sampleRate_ = 44100.0;
    double fallbackBeat_ = 0.0;
    double lastBpm_ = 120.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicShadowProcessor)
};
