#pragma once

#include "HarmonicEngine.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace harmonic_shadow {

struct CapturedMidiEvent {
    double beat = 0.0;
    std::uint8_t pitch = 0;
    std::uint8_t velocity = 0;
    std::uint8_t channel = 0;
    bool noteOn = false;
};

template <std::size_t Capacity>
class MidiEventQueue {
public:
    bool push(const CapturedMidiEvent& event) noexcept {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        if (write - read >= Capacity) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; }
        events_[write % Capacity] = event;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool pop(CapturedMidiEvent& event) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        event = events_[read % Capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
    void discard() noexcept { CapturedMidiEvent event; while (pop(event)) {} }
private:
    std::array<CapturedMidiEvent, Capacity> events_{};
    std::atomic<std::uint64_t> write_{0}, read_{0}, dropped_{0};
};

struct CaptureSnapshot {
    Session session;
    std::string sessionToken;
    std::vector<Chord> manualChordOverrides;
    Controls controls;
    int scratchTonic = 0;
    Mode scratchMode = Mode::Ionian;
    int scratchMeasures = 8;
    bool scratchEnabled = false;
    bool capturing = false;
    bool hostTimelineAvailable = true;
    std::size_t eventCount = 0;
    std::uint64_t droppedEvents = 0;
};

class CaptureCoordinator {
public:
    static CaptureCoordinator& instance();
    std::uint32_t addInstance(MidiEventQueue<8192>* queue, const std::string& label);
    void removeInstance(std::uint32_t id);
    void setLabel(std::uint32_t id, const std::string& label);
    void startCapture();
    void stopCapture();
    bool isCapturing() const noexcept { return capturing_.load(std::memory_order_acquire); }
    void clear();
    void updateTransport(double bpm, int numerator, int denominator, bool hasHostPosition);
    CaptureSnapshot collect();
    void setPreferences(const Controls& controls, int tonic, Mode mode, int measures, bool scratchEnabled);
    void setChordOverrides(const std::vector<Chord>& chords);
    std::uint32_t restoreSnapshot(const CaptureSnapshot& snapshot, std::uint32_t runtimeId,
                                  std::uint32_t savedInstanceId);

private:
    CaptureCoordinator();
    struct PendingKey {
        int channel = 0, pitch = 0;
        bool operator==(const PendingKey& other) const noexcept { return channel == other.channel && pitch == other.pitch; }
    };
    struct PendingHash {
        std::size_t operator()(const PendingKey& key) const noexcept { return static_cast<std::size_t>((key.channel << 8) | key.pitch); }
    };
    struct PendingNote { double start = 0; int velocity = 88; };
    struct Part {
        std::uint32_t id = 0;
        std::string label;
        MidiEventQueue<8192>* queue = nullptr;
        std::vector<Note> notes;
        std::unordered_map<PendingKey, std::deque<PendingNote>, PendingHash> pending;
    };
    void drainLocked(Part& part, bool closeOpenNotes);
    mutable std::mutex mutex_;
    std::unordered_map<std::uint32_t, Part> parts_;
    std::unordered_map<std::uint32_t, Part> unboundParts_;
    std::atomic<bool> capturing_{false};
    std::atomic<std::uint32_t> nextId_{1};
    std::atomic<double> bpm_{120.0};
    std::atomic<int> numerator_{4}, denominator_{4};
    std::atomic<bool> hostTimeline_{true};
    Controls controls_;
    int scratchTonic_ = 0;
    Mode scratchMode_ = Mode::Ionian;
    int scratchMeasures_ = 8;
    bool scratchEnabled_ = false;
    std::vector<Chord> manualChordOverrides_;
    std::string sessionToken_;
};

} // namespace harmonic_shadow
