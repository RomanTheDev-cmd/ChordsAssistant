#include "../Source/HarmonicEngine.h"
#include "../Source/CaptureCoordinator.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

using namespace harmonic_shadow;

static void chord(TrackPart& track, double start, double duration, std::initializer_list<int> pitches) {
    for (int pitch : pitches) track.notes.push_back({start, start + duration, pitch, 88, 0});
}

static Session tonalExample() {
    Session s; s.bpm=96; s.numerator=4; s.denominator=4;
    TrackPart part{1,"Pads",{}};
    chord(part,0,4,{48,52,55}); chord(part,4,4,{43,47,50});
    chord(part,8,4,{45,48,52}); chord(part,12,4,{41,45,48});
    s.tracks.push_back(part); return s;
}

static void verifyFiveVariants(const Session& s, std::uint32_t target) {
    auto a=analyzeSession(s);
    assert(!a.chords.empty());
    Controls c; c.mood=Mood::Dark; c.darkness=55; c.transformation=45;
    auto variants=generateVariations(s,a,target,c);
    assert(variants.size()==5);
    for (const auto& v:variants) { assert(!v.notes.empty()); assert(v.score>=0.0 && v.score<=1.0); }
    auto bytes=writeMidiFile(variants.front(),s);
    assert(bytes.size()>40 && bytes[0]=='M' && bytes[1]=='T' && bytes[2]=='h' && bytes[3]=='d');
    assert(bytes[8]==0 && bytes[9]==1); // Type 1, conductor plus generated part.
    assert(bytes[10]==0 && bytes[11]==2); // Conductor and MIDI part.
    assert(bytes[12]==1 && bytes[13]==0xe0); // 480 PPQ.

    auto read32=[&](size_t p) { return (static_cast<std::uint32_t>(bytes[p])<<24) |
        (static_cast<std::uint32_t>(bytes[p+1])<<16) | (static_cast<std::uint32_t>(bytes[p+2])<<8) | bytes[p+3]; };
    auto readVlq=[&](size_t& p, size_t end) {
        std::uint32_t value=0; int count=0;
        while (p<end && count++<4) { const auto byte=bytes[p++]; value=(value<<7)|(byte&0x7f); if (!(byte&0x80)) return value; }
        assert(false && "Invalid MIDI variable-length quantity"); return std::uint32_t{0};
    };
    size_t offset=14; int tracks=0, noteOns=0, noteOffs=0; bool tempo=false, meter=false;
    while (offset+8<=bytes.size()) {
        assert(std::string(bytes.begin()+static_cast<std::ptrdiff_t>(offset), bytes.begin()+static_cast<std::ptrdiff_t>(offset+4))=="MTrk");
        const auto length=read32(offset+4); size_t p=offset+8; const size_t end=p+length;
        assert(end<=bytes.size()); ++tracks;
        while (p<end) {
            (void)readVlq(p,end); assert(p<end);
            const auto status=bytes[p++];
            if (status==0xff) {
                assert(p<end); const auto type=bytes[p++]; const auto dataLength=readVlq(p,end);
                if (type==0x51 && dataLength==3) tempo=true;
                if (type==0x58 && dataLength==4) meter=true;
                p+=dataLength; assert(p<=end);
                if (type==0x2f) break;
            } else {
                assert(status>=0x80 && status<0xf0);
                const auto dataCount=((status&0xf0)==0xc0 || (status&0xf0)==0xd0) ? 1u : 2u;
                assert(p+dataCount<=end);
                if ((status&0xf0)==0x90 && bytes[p+1]>0) ++noteOns;
                if ((status&0xf0)==0x80 || ((status&0xf0)==0x90 && bytes[p+1]==0)) ++noteOffs;
                p+=dataCount;
            }
        }
        offset=end;
    }
    assert(tracks==2 && tempo && meter);
    assert(noteOns==static_cast<int>(variants.front().notes.size()));
    assert(noteOffs==noteOns);
}

int main() {
    // 1. Simple major-key progression.
    auto tonal=tonalExample();
    const auto tonalAnalysis=analyzeSession(tonal);
    assert(tonalAnalysis.tonic==0 && tonalAnalysis.mode==Mode::Ionian);
    assert(tonalAnalysis.chords.size()==4);
    assert(tonalAnalysis.chords.front().function=="I");
    verifyFiveVariants(tonal,1);

    // 2. Dorian/modal phrase, with characteristic natural sixth.
    Session modal; modal.bpm=112;
    TrackPart modalPart{2,"Dorian Pad",{}};
    chord(modalPart,0,4,{50,53,57}); chord(modalPart,4,4,{55,59,62});
    chord(modalPart,8,4,{48,52,55}); chord(modalPart,12,4,{50,53,57});
    modal.tracks.push_back(modalPart);
    verifyFiveVariants(modal,2);
    assert(analyzeSession(modal).mode==Mode::Dorian);

    // 3. Chromatic harmony with non-diatonic roots and extended sonorities.
    Session chromatic; chromatic.bpm=80;
    TrackPart chroma{3,"Chromatic",{}};
    chord(chroma,0,4,{48,51,55,59}); chord(chroma,4,4,{49,53,56,60});
    chord(chroma,8,4,{54,58,61,64}); chord(chroma,12,4,{53,56,60,63});
    chromatic.tracks.push_back(chroma);
    verifyFiveVariants(chromatic,3);

    // 4. Separate bass and harmony tracks. The bass-target output remains a bass line,
    // while the analysis still uses the combined arrangement.
    Session separate; separate.bpm=100;
    TrackPart pads{4,"Pads",{}}, bass{5,"Bass",{}};
    chord(pads,0,4,{60,64,67}); chord(pads,4,4,{59,62,67});
    chord(pads,8,4,{57,60,64}); chord(pads,12,4,{55,59,62});
    bass.notes={{0,4,36,96,0},{4,8,43,96,0},{8,12,33,96,0},{12,16,31,96,0}};
    separate.tracks={pads,bass};
    auto analysis=analyzeSession(separate);
    assert(analysis.trackNames.size()==2);
    assert(analysis.perTrackChords.at(4).size()>=3);
    assert(analysis.perTrackChords.at(5).size()>=3);
    auto bassVariants=generateVariations(separate,analysis,5,Controls{});
    assert(bassVariants.size()==5);
    for (const auto& v:bassVariants) {
        assert(v.notes.size()==bass.notes.size());
        for(const auto& n:v.notes) assert(n.pitch<55);
    }
    verifyFiveVariants(separate,4);
    Controls changedRhythm; changedRhythm.changeHarmonicRhythm=true;
    auto rhythmic=generateVariations(tonal,analyzeSession(tonal),1,changedRhythm);
    assert(rhythmic.size()==5 && rhythmic.front().chords.size()>=7);

    // Chord symbols retain bass inversions when the detected bass is a chord tone.
    Session inversion; TrackPart inverted{6,"Inverted pad",{}};
    chord(inverted,0,4,{52,55,60}); chord(inverted,4,4,{55,59,62});
    inversion.tracks.push_back(inverted);
    const auto inversionAnalysis=analyzeSession(inversion);
    assert(inversionAnalysis.chords.front().label=="C/E");

    // Horror mode is intentionally the darkest setting; release trends lower.
    Controls horror; horror.mood=Mood::Horror; horror.darkness=95; horror.transformation=75;
    Controls release=horror; release.mood=Mood::Release; release.darkness=25;
    const auto h=generateVariations(tonal,analyzeSession(tonal),1,horror);
    const auto r=generateVariations(tonal,analyzeSession(tonal),1,release);
    double hMean=0,rMean=0; for(const auto& v:h)hMean+=v.meanTension; for(const auto& v:r)rMean+=v.meanTension;
    assert(hMean>=rMean);

    // The shared collector keeps parts separate while preserving their absolute PPQ alignment.
    auto& coordinator=CaptureCoordinator::instance();
    MidiEventQueue<8192> padsQueue, bassQueue;
    const auto padsId=coordinator.addInstance(&padsQueue,"Pads");
    const auto bassId=coordinator.addInstance(&bassQueue,"Bass");
    coordinator.startCapture();
    coordinator.updateTransport(96,3,4,true);
    padsQueue.push({2.0,60,88,0,true}); padsQueue.push({3.0,60,0,0,false});
    bassQueue.push({2.0,36,96,0,true}); bassQueue.push({4.0,36,0,0,false});
    coordinator.collect();
    coordinator.stopCapture();
    const auto captured=coordinator.collect();
    assert(captured.session.numerator==3 && captured.session.denominator==4);
    assert(captured.session.tracks.size()>=2);
    const auto padsCaptured=std::find_if(captured.session.tracks.begin(),captured.session.tracks.end(),[padsId](const auto& t){return t.id==padsId;});
    const auto bassCaptured=std::find_if(captured.session.tracks.begin(),captured.session.tracks.end(),[bassId](const auto& t){return t.id==bassId;});
    assert(padsCaptured!=captured.session.tracks.end() && padsCaptured->notes.size()==1);
    assert(bassCaptured!=captured.session.tracks.end() && bassCaptured->notes.size()==1);
    assert(std::abs(padsCaptured->notes.front().startBeat-bassCaptured->notes.front().startBeat)<1e-9);
    coordinator.removeInstance(padsId); coordinator.removeInstance(bassId);
    coordinator.clear();

    // A saved Logic plugin state restores all shared tracks and rebinds each live FX instance.
    MidiEventQueue<8192> restoredPadsQueue, restoredBassQueue;
    const auto runtimePads=coordinator.addInstance(&restoredPadsQueue,"Runtime pads");
    const auto runtimeBass=coordinator.addInstance(&restoredBassQueue,"Runtime bass");
    CaptureSnapshot saved;
    saved.sessionToken="harmonic-shadow-persistence-fixture";
    saved.session.bpm=88; saved.session.numerator=5; saved.session.denominator=4;
    saved.session.tracks={{101,"Pads",{{0,2,60,84,0},{2,4,64,80,0}}},
                          {202,"Bass",{{0,4,36,96,0}}}};
    const auto restoredPads=coordinator.restoreSnapshot(saved,runtimePads,101);
    const auto restoredBass=coordinator.restoreSnapshot(saved,runtimeBass,202);
    assert(restoredPads==101 && restoredBass==202);
    const auto restored=coordinator.collect();
    assert(restored.session.bpm==88 && restored.session.numerator==5);
    assert(restored.session.tracks.size()==2);
    const auto restoredPadsPart=std::find_if(restored.session.tracks.begin(),restored.session.tracks.end(),[](const auto& t){return t.id==101;});
    const auto restoredBassPart=std::find_if(restored.session.tracks.begin(),restored.session.tracks.end(),[](const auto& t){return t.id==202;});
    assert(restoredPadsPart!=restored.session.tracks.end() && restoredPadsPart->notes.size()==2);
    assert(restoredBassPart!=restored.session.tracks.end() && restoredBassPart->notes.front().pitch==36);
    coordinator.removeInstance(restoredPads); coordinator.removeInstance(restoredBass);

    std::cout << "Harmonic engine examples passed: tonal, modal, chromatic, split bass; five variations, MIDI Type 1, inversions and saved-session restore.\n";
    return 0;
}
