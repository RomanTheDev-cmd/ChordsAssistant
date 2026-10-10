#include "CaptureCoordinator.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace harmonic_shadow {
namespace {

std::string makeSessionToken() {
    static std::atomic<std::uint64_t> sequence{1};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream value;
    value << std::hex << ticks << '-' << sequence.fetch_add(1, std::memory_order_relaxed);
    return value.str();
}

}

CaptureCoordinator::CaptureCoordinator() : sessionToken_(makeSessionToken()) {}

CaptureCoordinator& CaptureCoordinator::instance() {
    static CaptureCoordinator coordinator;
    return coordinator;
}

std::uint32_t CaptureCoordinator::addInstance(MidiEventQueue<8192>* queue, const std::string& label) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto id = nextId_.fetch_add(1, std::memory_order_relaxed);
    while (parts_.count(id) != 0) id = nextId_.fetch_add(1, std::memory_order_relaxed);
    Part part; part.id = id; part.queue = queue; part.label = label.empty() ? "MIDI Part " + std::to_string(id) : label;
    parts_.emplace(id, std::move(part));
    return id;
}

void CaptureCoordinator::removeInstance(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = parts_.find(id);
    if (it == parts_.end()) return;
    drainLocked(it->second, true);
    // Keep the captured phrase if a track temporarily unloads its MIDI FX instance.
    it->second.queue = nullptr;
    const bool anyActive = std::any_of(parts_.begin(), parts_.end(), [](const auto& pair) {
        return pair.second.queue != nullptr;
    });
    if (!anyActive) {
        parts_.clear();
        unboundParts_.clear();
        sessionToken_ = makeSessionToken();
    }
}

void CaptureCoordinator::setLabel(std::uint32_t id, const std::string& label) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = parts_.find(id);
    if (it != parts_.end() && !label.empty()) it->second.label = label.substr(0, 80);
}

void CaptureCoordinator::startCapture() {
    capturing_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = parts_.begin(); it != parts_.end();) {
        auto& part = it->second;
        if (part.queue == nullptr) { it = parts_.erase(it); continue; }
        part.queue->discard(); part.notes.clear(); part.pending.clear(); ++it;
    }
    manualChordOverrides_.clear();
    hostTimeline_.store(true, std::memory_order_release);
    capturing_.store(true, std::memory_order_release);
}

void CaptureCoordinator::stopCapture() {
    capturing_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, part] : parts_) { (void) id; drainLocked(part, true); }
}

void CaptureCoordinator::clear() {
    capturing_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = parts_.begin(); it != parts_.end();) {
        auto& part = it->second;
        if (part.queue == nullptr) { it = parts_.erase(it); continue; }
        part.queue->discard(); part.notes.clear(); part.pending.clear(); ++it;
    }
    manualChordOverrides_.clear();
    unboundParts_.clear();
    hostTimeline_.store(true, std::memory_order_release);
}

void CaptureCoordinator::updateTransport(double bpm, int numerator, int denominator, bool hasHostPosition) {
    if (bpm > 1.0 && bpm < 1000.0) bpm_.store(bpm, std::memory_order_relaxed);
    if (numerator > 0 && numerator < 33) numerator_.store(numerator, std::memory_order_relaxed);
    if (denominator > 0 && denominator < 33) denominator_.store(denominator, std::memory_order_relaxed);
    if (!hasHostPosition) hostTimeline_.store(false, std::memory_order_release);
}

void CaptureCoordinator::drainLocked(Part& part, bool closeOpenNotes) {
    if (part.queue == nullptr) return;
    CapturedMidiEvent event;
    while (part.queue->pop(event)) {
        const PendingKey key {static_cast<int>(event.channel), static_cast<int>(event.pitch)};
        if (event.noteOn && event.velocity > 0) {
            part.pending[key].push_back({event.beat, static_cast<int>(event.velocity)});
        } else {
            auto found = part.pending.find(key);
            if (found == part.pending.end() || found->second.empty()) continue;
            const auto start = found->second.front(); found->second.pop_front();
            part.notes.push_back({start.start, std::max(start.start + 0.01, event.beat),
                                  static_cast<int>(event.pitch), start.velocity, static_cast<int>(event.channel)});
            if (found->second.empty()) part.pending.erase(found);
        }
    }
    if (closeOpenNotes) {
        for (const auto& [key, queue] : part.pending) for (const auto& pending : queue) {
            part.notes.push_back({pending.start, pending.start + 0.25, key.pitch,
                                  pending.velocity, key.channel});
        }
        part.pending.clear();
    }
    std::sort(part.notes.begin(), part.notes.end(), [](const Note& a, const Note& b) {
        return a.startBeat < b.startBeat || (a.startBeat == b.startBeat && a.pitch < b.pitch);
    });
}

CaptureSnapshot CaptureCoordinator::collect() {
    std::lock_guard<std::mutex> lock(mutex_);
    CaptureSnapshot result;
    result.capturing = capturing_.load(std::memory_order_acquire);
    result.sessionToken = sessionToken_;
    result.hostTimelineAvailable = hostTimeline_.load(std::memory_order_acquire);
    result.session.bpm = bpm_.load(std::memory_order_relaxed);
    result.session.numerator = numerator_.load(std::memory_order_relaxed);
    result.session.denominator = denominator_.load(std::memory_order_relaxed);
    result.controls = controls_;
    result.manualChordOverrides = manualChordOverrides_;
    result.scratchTonic = scratchTonic_;
    result.scratchMode = scratchMode_;
    result.scratchMeasures = scratchMeasures_;
    result.scratchEnabled = scratchEnabled_;
    for (auto& [id, part] : parts_) {
        drainLocked(part, false);
        TrackPart track; track.id = id; track.name = part.label; track.notes = part.notes;
        result.eventCount += part.notes.size();
        if (part.queue) result.droppedEvents += part.queue->dropped();
        result.session.tracks.push_back(std::move(track));
    }
    return result;
}

void CaptureCoordinator::setPreferences(const Controls& controls, int tonic, Mode mode, int measures, bool scratchEnabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    controls_ = controls;
    scratchTonic_ = (tonic % 12 + 12) % 12;
    scratchMode_ = mode;
    scratchMeasures_ = std::clamp(measures, 2, 64);
    scratchEnabled_ = scratchEnabled;
}

void CaptureCoordinator::setChordOverrides(const std::vector<Chord>& chords) {
    std::lock_guard<std::mutex> lock(mutex_);
    manualChordOverrides_ = chords;
}

std::uint32_t CaptureCoordinator::restoreSnapshot(const CaptureSnapshot& snapshot, std::uint32_t runtimeId,
                                                   std::uint32_t savedInstanceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string restoredToken = snapshot.sessionToken.empty() ? makeSessionToken() : snapshot.sessionToken;
    if (restoredToken != sessionToken_) {
        unboundParts_.clear();
        for (const auto& [id, part] : parts_) if (part.queue != nullptr) {
            Part pending; pending.id = id; pending.label = part.label; pending.queue = part.queue;
            unboundParts_.emplace(id, std::move(pending));
        }
        parts_.clear();
        for (const auto& saved : snapshot.session.tracks) {
            Part part; part.id = saved.id; part.label = saved.name; part.notes = saved.notes;
            parts_.emplace(part.id, std::move(part));
        }
        sessionToken_ = restoredToken;
    }
    controls_ = snapshot.controls;
    manualChordOverrides_ = snapshot.manualChordOverrides;
    scratchTonic_ = snapshot.scratchTonic;
    scratchMode_ = snapshot.scratchMode;
    scratchMeasures_ = snapshot.scratchMeasures;
    scratchEnabled_ = snapshot.scratchEnabled;
    bpm_.store(snapshot.session.bpm, std::memory_order_relaxed);
    numerator_.store(snapshot.session.numerator, std::memory_order_relaxed);
    denominator_.store(snapshot.session.denominator, std::memory_order_relaxed);
    hostTimeline_.store(snapshot.hostTimelineAvailable, std::memory_order_relaxed);
    Part active;
    bool hasActive = false;
    auto pending = unboundParts_.find(runtimeId);
    if (pending != unboundParts_.end()) {
        active = std::move(pending->second);
        unboundParts_.erase(pending);
        hasActive = active.queue != nullptr;
    } else {
        auto current = parts_.find(runtimeId);
        if (current != parts_.end() && current->second.queue != nullptr) {
            if (runtimeId == savedInstanceId) {
                return runtimeId;
            }
            active = std::move(current->second);
            parts_.erase(current);
            hasActive = active.queue != nullptr;
        }
    }

    auto destination = parts_.find(savedInstanceId);
    if (destination != parts_.end() && destination->second.queue != nullptr &&
        (!hasActive || destination->second.queue != active.queue)) {
        // A copied plug-in instance can carry the same saved ID. Give the copy its own part.
        std::uint32_t newId = nextId_.fetch_add(1, std::memory_order_relaxed);
        while (parts_.count(newId) != 0 || unboundParts_.count(newId) != 0)
            newId = nextId_.fetch_add(1, std::memory_order_relaxed);
        active.id = newId;
        active.notes.clear();
        if (active.label.empty()) active.label = "MIDI Part " + std::to_string(newId);
        parts_[newId] = std::move(active);
        return newId;
    }

    if (destination == parts_.end()) {
        Part part;
        part.id = savedInstanceId;
        part.label = active.label.empty() ? "MIDI Part " + std::to_string(savedInstanceId) : active.label;
        destination = parts_.emplace(savedInstanceId, std::move(part)).first;
    }
    if (hasActive) {
        destination->second.queue = active.queue;
        if (destination->second.label.empty()) destination->second.label = active.label;
        if (destination->second.notes.empty()) destination->second.notes = std::move(active.notes);
    }

    std::uint32_t highest = 0;
    for (const auto& [id, part] : parts_) { (void) part; highest = std::max(highest, id); }
    auto next = nextId_.load(std::memory_order_relaxed);
    while (next <= highest && !nextId_.compare_exchange_weak(next, highest + 1, std::memory_order_relaxed)) {}
    return savedInstanceId;
}

} // namespace harmonic_shadow
