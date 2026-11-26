// SPDX-License-Identifier: GPL-3.0-or-later
#define VITALEON_TEST 1
#include "../src/Vitaleon.cpp"
#include "../src/engine/fourier_transform.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

static thread_local bool audit_allocations = false;
static thread_local size_t callback_allocations = 0;
void* operator new(size_t n) {
    if (audit_allocations) ++callback_allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

static int failures = 0;
void check(bool pass, const char* name) {
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
    if (!pass) ++failures;
}
int main(int argc, char**) {
    // Verify positive-spectrum FFT layout, including DC/Nyquist and a half-spectrum inverse.
    for (int bits : {6, 11}) {
        int n = 1 << bits;
        vital::FourierTransform fft(bits);
        std::vector<float> data(2*n), original(n);
        for (int i=0;i<n;++i) original[i]=data[i]=.1f+.4f*std::sin(6.28318530718f*3*i/n)+(i%2 ? -.2f:.2f);
        fft.transformRealForward(data.data());
        check(std::abs(data[0]-.1f*n)<.002f && std::abs(data[n]-.2f*n)<.002f, "FFT DC and Nyquist");
        std::fill(data.begin()+n+2,data.end(),0);
        fft.transformRealInverse(data.data());
        float error=0;
        for(int i=0;i<n;++i) error=std::max(error,std::abs(data[i]-original[i]));
        check(error<.00001f,"FFT normalized half-spectrum round trip");
    }
    auto synth=std::make_unique<Vitaleon>();
    synth->prepare(44100,512); synth->reset();
    float left[512]={},right[512]={}; float* channels[]={left,right};
    mpc::ProcessContext ctx; ctx.outputs=channels; ctx.numOutputs=2; ctx.numFrames=128; ctx.sampleRate=44100;
    // Benchmark regression checks exercise the one full graph.
    for(int i=0;i<10000&&synth->busy();++i){synth->process(ctx);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(!synth->busy(),"engine ready for the benchmark");
    auto render=[&]() {
        mpc::ScopedDenormalsOff denormals;
        audit_allocations=true;
        synth->process(ctx);
        audit_allocations=false;
        bool finite=true;
        for(int i=0;i<ctx.numFrames;++i) finite &= std::isfinite(left[i]) && std::isfinite(right[i]);
        if(!finite) ++failures;
    };
    // Exact MIDI offset and note release with a quiet engine.
    auto note=mpc::MidiEvent::noteOn(0,60,100,63);
    ctx.midi=&note;ctx.numMidiEvents=1;render();
    float before=0,after=0;
    for(int i=0;i<63;++i) before=std::max(before,std::abs(left[i]));
    ctx.numMidiEvents=0;
    for(int j=0;j<100;++j) {render(); for(int i=0;i<128;++i) after=std::max(after,std::abs(left[i]));}
    check(before<1e-8f && after>1e-5f,"sample-offset note-on produces audio only after event");
    std::printf("event_before_peak=%g event_after_peak=%g\n",before,after);
    note=mpc::MidiEvent::noteOff(0,60,11);ctx.numMidiEvents=1;render();ctx.numMidiEvents=0;
    for(int i=0;i<200;++i) render();
    check(synth->activeVoices()==0,"note-off releases the voice");
    // Channel-specific all notes off must leave another channel running.
    mpc::MidiEvent notes[]={mpc::MidiEvent::noteOn(0,60,90),mpc::MidiEvent::noteOn(1,67,90)};
    ctx.midi=notes;ctx.numMidiEvents=2;render();ctx.numMidiEvents=0;
    note=mpc::MidiEvent::cc(0,123,0);ctx.midi=&note;ctx.numMidiEvents=1;render();ctx.numMidiEvents=0;
    for(int i=0;i<200;++i) render();
    check(synth->activeVoices()==1,"CC123 affects only its MIDI channel");
    synth->reset();
    if(argc>1) {std::printf("Quick failures: %d\n",failures);return failures?1:0;}
    // Regression: Vital's default Kill policy briefly rendered 16 voices for
    // a second eight-note chord, even with the polyphony setting at eight.
    // Exercise repeated overlap, held sustain, and supported host block sizes.
    bool bounded=true;
    synth->set(synth->controlIndex("osc_1_unison_voices"),8);
    for(const char* osc:{"osc_1_on","osc_2_on","osc_3_on"})synth->set(synth->controlIndex(osc),1);
    for(int block:{32,128,512}) for(int limit:{1,2,4,6,8,16,32}) {
        synth->reset();synth->set(Vitaleon::Voices,float(limit));ctx.numFrames=block;
        std::array<mpc::MidiEvent,32> chord{};
        for(int turn=0;turn<4;++turn) {
            for(int n=0;n<limit;++n) chord[n]=mpc::MidiEvent::noteOn(0,36+n+(turn%2)*12,83);
            ctx.midi=chord.data();ctx.numMidiEvents=limit;render();ctx.numMidiEvents=0;
            bounded &= synth->activeVoices()==limit;
            for(int b=0;b<8;++b) {render();bounded &= synth->activeVoices()<=limit;}
            auto sustain=mpc::MidiEvent::cc(0,64,turn%2 ? 127:0);
            ctx.midi=&sustain;ctx.numMidiEvents=1;render();
            for(int n=0;n<limit;++n) chord[n]=mpc::MidiEvent::noteOff(0,36+n+(turn%2)*12,0);
            ctx.midi=chord.data();ctx.numMidiEvents=limit;render();ctx.numMidiEvents=0;
            // No wait for the long releases: the next chord must steal/reuse.
        }
    }
    check(bounded,"overlapping chords stay within configured polyphony (1..32, blocks 32/128/512)");
    note=mpc::MidiEvent::cc(0,64,0);ctx.midi=&note;ctx.numMidiEvents=1;render();ctx.numMidiEvents=0;
    synth->reset();
    std::printf("BENCHMARK: desktop timings only; NOT MPC voice-capacity measurements.\n");
    std::printf("profile,block,voices,active,unison,oscillators,mean_audio_budget_pct,p99_audio_budget_pct,peak\n");
    for(int profile=0;profile<4;++profile) {
        synth->set(synth->controlIndex("osc_1_on"),1);
        synth->set(synth->controlIndex("osc_2_on"),profile>=2?1:0);
        synth->set(synth->controlIndex("osc_3_on"),profile>=2?1:0);
        synth->set(synth->controlIndex("osc_1_unison_voices"),profile==0?1:4);
        synth->set(synth->controlIndex("osc_1_spectral_morph_type"),profile==3?1:0);
        synth->set(synth->controlIndex("osc_1_spectral_morph_amount"),.65f);
        for(int voices : {1,2,4,8,12,16,24,32}) {
            synth->set(Vitaleon::Voices,float(voices)); synth->set(Vitaleon::Bench,1);
            ctx.numFrames=128;
            for(int i=0;i<20;++i) render();
            check(synth->activeVoices()==voices,"benchmark active-voice count matches requested count");
            std::array<double,300> times{}; double sum=0;float peak=0;
            for(auto& time:times) {
                auto t=std::chrono::steady_clock::now();render();
                time=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();sum+=time;
                for(int i=0;i<128;++i) peak=std::max(peak,std::abs(left[i]));
            }
            std::sort(times.begin(),times.end());
            std::printf("%d,128,%d,%d,%d,%d,%.3f,%.3f,%.6f\n",profile,voices,synth->activeVoices(),synth->getInt(synth->controlIndex("osc_1_unison_voices")),synth->getInt(synth->controlIndex("osc_1_on"))+synth->getInt(synth->controlIndex("osc_2_on"))+synth->getInt(synth->controlIndex("osc_3_on")),sum/300*44100/128*100,times[296]*44100/128*100,peak);
            check(peak>1e-6f && peak<1,"benchmark output audible and unclipped at test gain");
        }
    }
    // Non-power-of-two and host blocks bigger than Vital's 128-frame ceiling.
    for(int block:{1,17,32,64,96,128,192,256,512}) {ctx.numFrames=block;for(int i=0;i<8;++i)render();}
    check(callback_allocations==0,"zero C++ heap allocations in audited audio callbacks");
    std::printf("callback_allocations=%zu\n",callback_allocations);
    synth->reset();render();
    check(synth->activeVoices()==0,"reset stops benchmark and all voices, preserving saved parameters");
    std::printf("Failures: %d\n",failures);
    return failures?1:0;
}
