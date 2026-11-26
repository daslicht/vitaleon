#define VITALEON_TEST 1
#include "../src/Vitaleon.cpp"
#include <cstdio>
#include <chrono>
int main(){
    auto s=std::make_unique<Vitaleon>();s->set(Vitaleon::Voices,8);s->set(s->controlIndex("osc_1_unison_voices"),8);s->set(s->controlIndex("osc_2_on"),1);s->set(s->controlIndex("osc_3_on"),1);
    s->prepare(44100,128);s->reset();float l[128]{},r[128]{};float* out[]={l,r};
    mpc::MidiEvent notes[8];mpc::ProcessContext ctx;ctx.outputs=out;ctx.numOutputs=2;ctx.numFrames=128;ctx.sampleRate=44100;ctx.midi=notes;
    auto render=[&](){mpc::ScopedDenormalsOff denormals;s->process(ctx);};
    for(int chord=0;chord<4;++chord){
        for(int i=0;i<8;++i)notes[i]=mpc::MidiEvent::noteOn(0,48+i*3+chord,83);
        ctx.numMidiEvents=8;render();ctx.numMidiEvents=0;
        std::printf("chord=%d active_after_attack=%d\n",chord,s->activeVoices());
        double sum=0,max=0;int maxvoices=s->activeVoices();
        for(int b=0;b<20;++b){auto t=std::chrono::steady_clock::now();render();double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count();sum+=us;max=std::max(max,us);maxvoices=std::max(maxvoices,s->activeVoices());}
        std::printf("next_58ms_mean_us=%.3f max_us=%.3f max_voices=%d\n",sum/20,max,maxvoices);
        for(int i=0;i<8;++i)notes[i]=mpc::MidiEvent::noteOff(0,48+i*3+chord,0);
        ctx.numMidiEvents=8;render();ctx.numMidiEvents=0;render();
    }
}
