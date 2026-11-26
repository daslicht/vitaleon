// SPDX-License-Identifier: GPL-3.0-or-later
// Vitaleon by Andersson Audio. Experimental Vital-engine port, not official Vital.
// MIDI -> Vital voices / wavetable oscillators -> Vital filter / ADSR -> stereo.
// The base class retains the 0.1.x state schema for conversion. FullSynth.h adds the full engine,
// asset imports, modulation, effects and MPC graphics around the same DSP core.
#include "mpc_plugin.h"
#include "engine/sound_engine.h"
#include "engine/wave_frame.h"
#include "engine/wavetable.h"
#include "engine/synth_oscillator.h"
#include "engine/voice_handler.h"
#include <array>
#include <memory>

class VitaleonLegacy : public mpc::Plugin {
public:
    // 0.4.0 (user report): the fourteen legacy controls (Volume, Cutoff, Attack...)
    // were dead since the full graph took over, yet the host still offered them as
    // automation targets that did nothing. Only the two slots the full engine still
    // reads remain: Voices is the polyphony, Bench the benchmark arming flag.
    enum { Voices, Bench, Count };
    VitaleonLegacy() : mpc::Plugin(makeInfo()) {
        // At most eight voices (0.3.24, user request): more than that is beyond the MPC's
        // processor with a full sound. A project saved with more is restored at eight.
        addParam(mpc::intParam("voices", "Voices", 1, 8, 4));
        auto bench = mpc::boolParam("bench", "Bench", false);
        bench.automatable = false;
        addParam(bench);
        engine_ = std::make_unique<vital::SoundEngine>();
        engine_->setStatusUpdatesEnabled(false);
        auto controls = engine_->getControls();
        auto find = [&](const std::string& key) {
            auto found = controls.find(key);
            if (found == controls.end()) throw std::runtime_error("Missing engine control: " + key);
            return found->second;
        };
        polyphony_ = find("polyphony");
        find("oversampling")->set(0);
        // Andersson Audio, 2026-11-13: reuse voices at the polyphony limit.
        // Vital's default kKill mode can render a second full chord while the
        // old voices fade for 50 ms, causing an avoidable CPU spike on MPC.
        // kSteal retains Vital's envelope/filter/phase transition handling.
        find("voice_override")->set(vital::VoiceHandler::kSteal);
        find("filter_1_model")->set(vital::constants::kDigital);
        find("filter_1_style")->set(0);
        find("filter_2_on")->set(0); find("sample_on")->set(0);
        for (const char* fx : {"chorus_on", "compressor_on", "delay_on", "distortion_on", "eq_on", "flanger_on", "phaser_on", "reverb_on"}) find(fx)->set(0);
        const vital::PredefinedWaveFrames::Shape shapes[] = {
            vital::PredefinedWaveFrames::kSin, vital::PredefinedWaveFrames::kTriangle,
            vital::PredefinedWaveFrames::kSaw, vital::PredefinedWaveFrames::kSquare};
        for (int i = 0; i < 3; ++i) {
            auto prefix = std::string("osc_") + std::to_string(i + 1) + "_";
            find(prefix + "destination")->set(vital::constants::kFilter1);
            find(prefix + "level")->set(0.5f); find(prefix + "random_phase")->set(0.75f);
            find(prefix + "tune")->set(i == 0 ? 0 : (i == 1 ? -0.07f : 0.07f));
            auto* table = engine_->getWavetable(i);
            table->setNumFrames(4);
            for (int frame = 0; frame < 4; ++frame)
                table->loadWaveFrame(vital::PredefinedWaveFrames::getWaveFrame(shapes[frame]), frame);
        }
    }
    void prepare(double sampleRate, int) override {
        engine_->setSampleRate(int(sampleRate)); engine_->checkOversampling();
        engine_->noteOn(60, .1f, 0, 0); engine_->process(128); engine_->allSoundsOff();
    }
    void reset() override {
        engine_->allSoundsOff();
        engine_->process(128); // clear accumulated output from the previous voice set
        // Preserve the saved parameter; require Off -> On before restarting a saved benchmark.
        benchArmed_ = !getBool(Bench); benchmarkCount_ = 0;
    }
    void process(mpc::ProcessContext& ctx) override {
        if (!getBool(Bench)) benchArmed_ = true;
        const bool bench = getBool(Bench) && benchArmed_;
        const int desired = bench ? getInt(Voices) : 0;
        if (desired != benchmarkCount_) {
            engine_->allSoundsOff(); benchmarkCount_ = desired;
            for (int n = 0; n < desired; ++n) engine_->noteOn(48 + n, .65f, 0, 0);
        }
        int event = 0;
        for (int offset = 0; offset < ctx.numFrames;) {
            while (event < ctx.numMidiEvents && ctx.midi[event].sampleOffset <= offset) {
                if (!bench) handleMidi(ctx.midi[event], 0);
                ++event;
            }
            int frames = std::min(128, ctx.numFrames - offset);
            // Split at MIDI boundaries: avoid upstream voice envelopes changing samples
            // before a mid-block trigger. Events at one boundary retain their input order.
            if (!bench && event < ctx.numMidiEvents)
                frames = std::min(frames, ctx.midi[event].sampleOffset - offset);
            engine_->process(frames);
            const auto* out = engine_->output()->buffer;
            for (int i = 0; i < frames; ++i) {
                ctx.outputs[0][offset + i] = out[i][0];
                ctx.outputs[1][offset + i] = out[i][1];
            }
            offset += frames;
        }
    }
    int activeVoices() const { return engine_->getNumActiveVoices(); }
protected:
    // The full graph owns every sound control; only polyphony is still read here.
    std::unique_ptr<vital::SoundEngine> engine_;
    vital::Value *polyphony_;
    int benchmarkCount_ = 0;
    bool benchArmed_ = true;
    void handleMidi(const mpc::MidiEvent& e, int offset) {
        const int ch = e.channel();
        if (e.isNoteOn()) engine_->noteOn(e.note(), e.velocity() / 127.0f, offset, ch);
        else if (e.isNoteOff()) engine_->noteOff(e.note(), e.velocity() / 127.0f, offset, ch);
        else if (e.isPitchBend()) engine_->setPitchWheel(e.pitchBend() / 8192.0f, ch);
        else if (e.isController()) {
            if (e.controller() == 120) engine_->allSoundsOff();
            else if (e.controller() == 123) engine_->allNotesOffRange(offset, ch, ch);
            else if (e.controller() == 64) {
                if (e.controllerValue() >= 64) engine_->sustainOn(ch);
                else engine_->sustainOff(offset, ch);
            }
            else if (e.controller() == 1) engine_->setModWheel(e.controllerValue() / 127.0f, ch);
        }
        else if (e.isAftertouch()) engine_->setChannelAftertouch(ch, e.data[1] / 127.0f, offset);
    }
    static mpc::PluginInfo makeInfo() {
        mpc::PluginInfo info;
        info.name = "Vitaleon"; info.vendor = "Andersson Audio";
        info.uniqueId = MPC_FOURCC('A','l','7','q'); info.version = 319; info.latencySamples = 128; /* effect pipeline, see FullSynth.h */
        info.isInstrument = true; info.wantsMidi = true;
        info.numInputs = 0; info.numOutputs = 2;
        return info;
    }
};
#include "FullSynth.h"
#ifndef VITALEON_TEST
MPC_PLUGIN_ENTRY(Vitaleon)
#endif
