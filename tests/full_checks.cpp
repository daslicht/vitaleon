// SPDX-License-Identifier: GPL-3.0-or-later
#define VITALEON_TEST 1
#include "../src/Vitaleon.cpp"
#include "../src/engine/synth_lfo.h"
#include <iostream>
#include <sstream>
#include <new>
MPC_PLUGIN_ENTRY(Vitaleon)
static thread_local bool audit=false;
static thread_local size_t allocations=0;
static std::atomic<unsigned> displayUpdates{0};
static std::atomic<size_t> selectedCache{0},loadedCache{0};
static std::atomic<bool> displayOnAudio{false};
static std::atomic<int> automatedTarget{-1};
static std::atomic<unsigned> readoutUpdates{0};
static std::atomic<unsigned> automateCalls{0}; // every audioMasterAutomate, whatever the parameter
static std::atomic<int> cachedProgramCount{0};
static std::atomic<bool> validReentrantNames{true};
static AEffect displayEffect{};
static VstIntPtr displayHost(AEffect* effect,VstInt32 opcode,VstInt32 index,VstIntPtr,void*,float){
 if(opcode==audioMasterAutomate){automateCalls.fetch_add(1);if(static_cast<mpc::Plugin*>(effect->object)->param(index).automatable)automatedTarget.store(index);else readoutUpdates.fetch_add(1);}
 if(opcode==audioMasterUpdateDisplay){
  if(audit)displayOnAudio.store(true);
  auto* plugin=static_cast<mpc::Plugin*>(effect->object);
  if(effect->dispatcher){for(int i=0;i<effect->numPrograms;++i){char name[256]{};auto ok=effect->dispatcher(effect,effGetProgramNameIndexed,i,0,name,0);if(!ok||!name[0])validReentrantNames.store(false);}cachedProgramCount.store(effect->numPrograms);}
  for(int p=0;p<plugin->numParams();++p){
   if(plugin->param(p).id=="library_selected_0")selectedCache.store(std::hash<std::string>{}(plugin->displayText(p)));
   // Reentrant string queries acquire the synth state mutex: the notification
   // must happen outside it, not from setLoadStatus while loading.
   if(plugin->param(p).id=="library_loaded_0")loadedCache.store(std::hash<std::string>{}(plugin->displayText(p)));
  }
  displayUpdates.fetch_add(1);
 }
 return opcode==audioMasterVersion?2400:opcode==audioMasterUpdateDisplay?1:0;
}
void* operator new(size_t n){if(audit)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
int main(int argc,char** argv){
 if(argc>1&&std::string(argv[1])=="--native-bank"){
  AEffect* e=VSTPluginMain(displayHost);auto* p=static_cast<Vitaleon*>(e->object);
  auto d=[&](int op,int ix=0,VstIntPtr val=0,void* ptr=nullptr,float opt=0){return e->dispatcher(e,op,ix,val,ptr,opt);};
  d(effOpen);d(effSetSampleRate,0,0,nullptr,44100);d(effSetBlockSize,0,512);d(effMainsChanged,0,1);
  float l[512]{},r[512]{};float* out[]={l,r};int loaded=0,rejected=0,failed=0;std::set<std::string> names;
  int status=-1;for(int i=0;i<p->numParams();++i)if(p->param(i).id=="load_status")status=i;
  std::cout<<"native_program_count="<<e->numPrograms<<std::endl;
  for(int i=0;i<e->numPrograms;++i){char name[256]{};d(effGetProgramNameIndexed,i,0,name);std::string text=name;if(!names.insert(text).second||text.size()>24)++failed;try{nlohmann::json(text).dump();}catch(...){++failed;}
   d(effSetProgram,0,i);for(int n=0;n<30000&&p->busy();++n){e->processReplacing(e,nullptr,out,128);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
   if(p->busy()){++failed;std::cout<<"FAIL timeout "<<text<<std::endl;continue;}
   if(p->getInt(status)==2){++rejected;std::cout<<"REJECTED "<<text<<" : "<<p->displayText(status)<<std::endl;continue;}
   VstMidiEvent note{};note.type=kVstMidiType;note.byteSize=sizeof(note);note.midiData[0]=char(0x90);note.midiData[1]=60;note.midiData[2]=90;VstEvents events{};events.numEvents=1;events.events[0]=reinterpret_cast<VstEvent*>(&note);d(effProcessEvents,0,0,&events);
   double energy=0;bool finite=true;for(int n=0;n<80;++n){e->processReplacing(e,nullptr,out,128);for(int j=0;j<128;++j){finite&=std::isfinite(l[j])&&std::isfinite(r[j]);energy+=l[j]*l[j]+r[j]*r[j];}}
   if(!finite)++failed;else ++loaded;std::cout<<(finite?"LOADED ":"FAIL audio ")<<i<<" "<<text<<" energy="<<energy<<std::endl;
  }
  std::cout<<"loaded="<<loaded<<" rejected="<<rejected<<" failed="<<failed<<std::endl;d(effMainsChanged,0,0);d(effClose);return failed||!loaded?1:0;
 }
 int failures=0;auto check=[&](bool ok,const std::string& name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<std::endl;if(!ok)++failures;};
 if(argc>1&&std::string(argv[1])=="--no-content"){
  // A plugin without any installed sound (a build from source): one "Init" program.
  AEffect* e=VSTPluginMain(displayHost);auto* p=static_cast<Vitaleon*>(e->object);
  auto d=[&](int op,int ix=0,VstIntPtr val=0,void* ptr=nullptr,float opt=0){return e->dispatcher(e,op,ix,val,ptr,opt);};
  d(effOpen);d(effSetSampleRate,0,0,nullptr,44100);d(effSetBlockSize,0,512);d(effMainsChanged,0,1);
  float l[512]{},r[512]{};float* out[]={l,r};char name[256]{};d(effGetProgramNameIndexed,0,0,name);
  auto wait=[&]{for(int n=0;n<10000&&p->busy();++n){e->processReplacing(e,nullptr,out,128);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
  auto energy=[&]{VstMidiEvent note{};note.type=kVstMidiType;note.byteSize=sizeof(note);note.midiData[0]=char(0x90);note.midiData[1]=60;note.midiData[2]=100;VstEvents events{};events.numEvents=1;events.events[0]=reinterpret_cast<VstEvent*>(&note);d(effProcessEvents,0,0,&events);
   double sum=0;for(int n=0;n<80;++n){e->processReplacing(e,nullptr,out,128);for(int j=0;j<128;++j)sum+=l[j]*l[j]+r[j]*r[j];}return sum;};
  wait();check(e->numPrograms==1&&std::string(name)=="Init"&&d(effGetProgram)==0,"plugin without sounds offers exactly one Init program");
  // The Init state of every parameter, for tools/generate_surface.py (tools/init-values.json).
  if(argc>2){nlohmann::json values=nlohmann::json::array();for(int i=0;i<p->numParams();++i)values.push_back({{"index",i},{"name",p->param(i).name},{"value",p->getNormalised(i)},{"display",p->displayText(i)}});std::ofstream file(argv[2]);file<<values.dump(1);}
  check(energy()>1e-6,"plugin without sounds plays its default sound");
  int cutoff=p->controlIndex("filter_1_cutoff");float before=p->get(cutoff);p->set(cutoff,before==20?30.0f:20.0f);d(effSetProgram,0,0);wait();
  int loaded=-1;for(int i=0;i<p->numParams();++i)if(p->param(i).id=="library_loaded_0")loaded=i;
  check(p->get(cutoff)==before&&p->displayText(loaded)=="Init"&&energy()>1e-6,"selecting Init resets the controls and still plays");
  d(effMainsChanged,0,0);d(effClose);return failures?1:0;
 }
 {
  vital::Wavetable oldPath(257),prepared(257),target(257);
  WavetableCreator a(&oldPath);a.initPredefinedWaves();auto json=a.stateToJson();a.jsonToState(json);a.render();
  WavetableCreator b(&prepared);b.jsonToState(json);
  int version=target.getVersion();target.adoptPrepared(prepared);
  auto* x=oldPath.getAllData();auto* y=target.getAllData();int frames=x->num_frames;
  bool identical=frames==y->num_frames&&x->frequency_ratio==y->frequency_ratio&&x->sample_rate==y->sample_rate;
  identical&=std::memcmp(x->wave_data.get(),y->wave_data.get(),frames*sizeof(float)*vital::Wavetable::kWaveformSize)==0;
  for(int f=0;f<frames;++f){size_t bytes=sizeof(vital::poly_float)*vital::Wavetable::kPolyFrequencySize;
   identical&=std::memcmp(x->frequency_amplitudes[f],y->frequency_amplitudes[f],bytes)==0;
   identical&=std::memcmp(x->normalized_frequencies[f],y->normalized_frequencies[f],bytes)==0;
   identical&=std::memcmp(x->phases[f],y->phases[f],bytes)==0;
  }
  check(identical,"prepared transfer preserves every waveform and spectral data byte");
  check(target.getVersion()!=version&&target.getName()==oldPath.getName()&&target.isShepardTable()==oldPath.isShepardTable(),"prepared transfer invalidates oscillator cache and preserves metadata");
 }
 for(bool control:{false,true}){
  LineGenerator curve;curve.setNumPoints(4);curve.setPoint(0,{0,1});curve.setPoint(1,{.25f,.3f});curve.setPoint(2,{.6f,.8f});curve.setPoint(3,{1,0});curve.render();
  vital::SynthLfo lfo(&curve);lfo.setSampleRate(1000);lfo.setControlRate(control);
  std::array<vital::Output,vital::SynthLfo::kNumInputs> in;for(int i=0;i<int(in.size());++i)lfo.plug(&in[i],i);
  in[vital::SynthLfo::kSyncType].buffer[0]=vital::SynthLfo::kPointCycle;in[vital::SynthLfo::kPhase].buffer[0]=.83f;
  auto trigger=[&](vital::SynthLfo& target){in[vital::SynthLfo::kNoteTrigger].trigger_value=vital::poly_float(vital::kVoiceOn,vital::kVoiceOn,0,0);target.process(1);in[vital::SynthLfo::kNoteTrigger].clearTrigger();return vital::utils::decodePhaseAndVoice(target.output(vital::SynthLfo::kOscPhase)->buffer[0]).first[0];};
  bool ok=std::abs(trigger(lfo))<1e-5;
  std::unique_ptr<vital::SynthLfo> clone(static_cast<vital::SynthLfo*>(lfo.clone()));
  ok&=std::abs(trigger(*clone)-.25f)<1e-5&&std::abs(trigger(lfo)-.6f)<1e-5;
  in[vital::SynthLfo::kFrequency].buffer[0]=8;for(int i=0;i<8;++i)lfo.process(32);
  float end=vital::utils::decodePhaseAndVoice(lfo.output(vital::SynthLfo::kOscPhase)->buffer[0]).first[0];ok&=std::abs(end-1)<1e-4;
  check(ok,control?"Point Cycle control-rate segments, shared cursor and endpoint":"Point Cycle audio-rate segments, shared cursor and endpoint");
  lfo.resetPointCycle();curve.setPoint(2,{.25f,.8f});curve.render();in[vital::SynthLfo::kFrequency].buffer[0]=0;
  ok=std::abs(trigger(lfo))<1e-5&&std::abs(trigger(lfo)-.25f)<1e-5&&std::abs(trigger(lfo))<1e-5;
  check(ok,"Point Cycle skips duplicate-time segments");
 }
 // The engine checks start from the Init state; the start sound is checked through the VST entry point below.
 auto synth=std::make_unique<Vitaleon>();synth->testNoStartSound();synth->prepare(44100,512);
 displayEffect.object=synth.get();synth->effect_=&displayEffect;synth->host_=displayHost;
 auto id=[&](const std::string& name){for(int i=0;i<synth->numParams();++i)if(synth->param(i).id==name)return i;throw std::runtime_error("Missing parameter "+name);};
 float left[512]{},right[512]{};float* outputs[]={left,right};mpc::ProcessContext ctx;ctx.outputs=outputs;ctx.numOutputs=2;ctx.numFrames=128;
 bool finite=true;auto render=[&]{mpc::ScopedDenormalsOff denormals;audit=true;synth->process(ctx);audit=false;for(int i=0;i<ctx.numFrames;++i)finite&=std::isfinite(left[i])&&std::isfinite(right[i]);};
 auto settle=[&]{ctx.numMidiEvents=0;for(int i=0;i<10000&&synth->busy();++i){render();std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(!synth->busy(),"asset service settled");};
 std::array<mpc::MidiEvent,8> notes;for(int i=0;i<8;++i)notes[i]=mpc::MidiEvent::noteOn(0,48+i,90);ctx.midi=notes.data();ctx.numMidiEvents=8;render();ctx.numMidiEvents=0;
 float peak=0;for(int b=0;b<100;++b){render();for(float x:left)peak=std::max(peak,std::abs(x));}check(peak>1e-5f,"full graph produces sound");
 synth->set(id("edit_env"),3);synth->set(id("edit_env_attack"),.7f);check(std::abs(synth->get(synth->controlIndex("env_3_attack"))-.7f)<1e-5,"envelope proxy edits selected envelope");
 synth->set(synth->controlIndex("env_3_sustain"),.123f);check(std::abs(synth->get(id("edit_env_sustain"))-.123f)<1e-5,"canonical edits refresh selected proxy");
 float previousAttack=synth->getNormalised(synth->controlIndex("env_1_attack"));
 synth->setNormalised(id("home_3"),.5f);
 check(std::abs(synth->getNormalised(synth->controlIndex("env_1_attack"))-.5f)<1e-5&&std::abs(synth->get(synth->controlIndex("env_3_attack"))-.7f)<1e-5,"overview attack edits amplitude envelope, not editor selection");
 synth->setNormalised(id("home_3"),previousAttack);
 synth->setNormalised(id("home_5"),1);check(synth->getInt(synth->controlIndex("osc_1_unison_voices"))==16,"overview unison reaches full-engine range");synth->setNormalised(id("home_5"),0);
 auto choice=[&](int p,const std::string& value){const auto& choices=synth->param(p).choices;for(size_t i=0;i<choices.size();++i)if(choices[i]==value)return float(i);throw std::runtime_error("Choice missing: "+value);};
 // Deterministically place an audio-block refresh between the host's atomic
 // parameter store and its parameterChanged callback. Rereading a proxy here
 // loses the gesture; the callback's supplied plain value is authoritative.
 auto interleavedEdit=[&](int proxy,float normalized){
  synth->publishNormalised(proxy,normalized);render();
  synth->parameterChanged(proxy,synth->param(proxy).toPlain(normalized));
 };
 interleavedEdit(id("home_1"),.82f);
 check(std::abs(synth->getNormalised(synth->controlIndex("filter_1_cutoff"))-.82f)<1e-5,"HOME cutoff survives audio refresh during host gesture");
 interleavedEdit(id("edit_env_attack"),.61f);
 check(std::abs(synth->getNormalised(synth->controlIndex("env_3_attack"))-.61f)<1e-5,"envelope gesture survives audio refresh");
 synth->set(id("advanced_parameter"),0);interleavedEdit(id("advanced_value"),.37f);
 check(std::abs(synth->getNormalised(synth->controlIndex(synth->param(id("advanced_parameter")).choices[0]))-.37f)<1e-5,"advanced gesture survives audio refresh");
 synth->set(id("route_1_source"),choice(id("route_1_source"),"lfo_1"));synth->set(id("route_1_destination"),choice(id("route_1_destination"),"osc_1_wave_frame"));
 synth->set(synth->controlIndex("modulation_1_amount"),.8f);settle();
 float min=1,max=0;for(int b=0;b<700;++b){render();float p=synth->get(id("display_position_0"));min=std::min(min,p);max=std::max(max,p);}check(max-min>.1f,"wavetable display follows actual LFO modulation");std::cout<<"frame_display_min="<<min<<" max="<<max<<std::endl;
 if(argc>2){nlohmann::json values=nlohmann::json::array();for(int i=0;i<synth->numParams();++i)values.push_back({{"id",synth->param(i).id},{"name",synth->param(i).name},{"index",i},{"value",synth->getNormalised(i)},{"display",synth->displayText(i)}});std::ofstream file(argv[2]);file<<values.dump(2);}
 for(const char* fx:{"chorus_on","compressor_on","delay_on","distortion_on","eq_on","filter_fx_on","flanger_on","phaser_on","reverb_on"}){synth->set(synth->controlIndex(fx),1);for(int b=0;b<12;++b)render();synth->set(synth->controlIndex(fx),0);}
 check(finite,"all nine effects render finite audio");check(allocations==0,"no C++ allocations in callback (full graph, modulation, effects)");std::cout<<"callback_allocations="<<allocations<<std::endl;
 synth->set(id("effect_slot"),1);synth->set(id("effect_type"),8);int order[9];vital::utils::decodeFloatToOrder(order,synth->get(synth->controlIndex("effect_chain_order")),9);check(order[0]==8,"FX reorder controls update engine order");
 auto saved=synth->internalSaveState();float sustain=synth->get(synth->controlIndex("env_3_sustain"));synth->set(synth->controlIndex("env_3_sustain"),.9f);check(synth->internalLoadState(saved.data(),saved.size()),"state parser accepts saved chunk");settle();check(std::abs(synth->get(synth->controlIndex("env_3_sustain"))-sustain)<1e-6,"canonical state round trip");
 auto savedAssets=synth->testAssets();check(!synth->internalLoadState(saved.data(),saved.size()-10),"truncated chunk rejected");check(synth->get(synth->controlIndex("env_3_sustain"))==sustain,"invalid chunk leaves parameters unchanged");
 check(savedAssets.count("sample")&&savedAssets["sample"].count("samples"),"default sample embedded in project state");
 for(int turn=0;turn<6;++turn){synth->testInit();if(turn%2)synth->set(synth->controlIndex("osc_2_on"),1);settle();check(synth->internalLoadState(saved.data(),saved.size()),"project restores across sound changes");settle();check(synth->testAssets()==savedAssets,"embedded tables, sample and curves restored exactly");}
 nlohmann::json invalid={{"synth_version","1.0.6"},{"settings",savedAssets}};invalid["settings"]["env_3_sustain"]=.99f;invalid["settings"]["lfos"][0]["num_points"]=101;
 bool rejected=false;try{synth->testImport(invalid);}catch(const std::exception&){rejected=true;}
 check(rejected&&synth->get(synth->controlIndex("env_3_sustain"))==sustain,"invalid preset rejected before changing sound parameters");
 auto beforeLateFailure=synth->testAssets();auto lateFailure=invalid;
 lateFailure["settings"]=beforeLateFailure;lateFailure["settings"]["sample"]["samples"]="broken";
 lateFailure["settings"]["wavetables"][0]["name"]="Prepared but must not commit";
 rejected=false;try{synth->testImport(lateFailure);}catch(...){rejected=true;}
 check(rejected&&synth->testAssets()==beforeLateFailure&&synth->get(synth->controlIndex("env_3_sustain"))==sustain,"sample failure after table preparation preserves previous sound and all assets");
 nlohmann::json ramps={{"synth_version","1.6.4"},{"settings",{{"filter_1_cutoff",63},{"modulation_1_ramp_up",-10},{"modulation_64_ramp_down",-10}}}};
 synth->testImport(ramps);check(synth->get(synth->controlIndex("filter_1_cutoff"))==63,"1.6 minimum ramp fields import with documented compatibility bypass");
 for(const auto& field:{"modulation_1_ramp_up","modulation_64_ramp_down","modulation_65_ramp_up"}){auto bad=ramps;bad["settings"][field]=-9;bool rejected=false;try{synth->testImport(bad);}catch(...){rejected=true;}check(!rejected&&synth->get(synth->controlIndex("filter_1_cutoff"))==63&&synth->testImportNotes().size()==1,"configured or unknown ramp loads and is reported as left out");}
 auto modRamp=ramps;modRamp["settings"]["modulations"]=nlohmann::json::array({{{"source","lfo_1"},{"destination","modulation_1_ramp_up"}}});rejected=false;try{synth->testImport(modRamp);}catch(...){rejected=true;}check(!rejected&&synth->testImportNotes().size()==1&&synth->testImportNotes()[0].find("Mod target skipped")==0&&synth->getInt(id("route_1_destination"))==0,"route to an unknown target is skipped and reported, not connected elsewhere");
 {nlohmann::json modern={{"synth_version","1.5.5"},{"settings",{{"filter_1_cutoff",64},{"osc_1_spectral_morph_type",14},{"osc_2_spectral_morph_type",3},{"osc_1_spectral_morph_phase",.25},{"pitch_env_type",0}}}};
  bool threw=false;try{synth->testImport(modern);}catch(...){threw=true;}
  check(!threw&&synth->get(synth->controlIndex("filter_1_cutoff"))==64,"preset with newer spectral mode and unknown setting loads");
  check(synth->getInt(synth->controlIndex("osc_1_spectral_morph_type"))==0&&synth->getInt(synth->controlIndex("osc_2_spectral_morph_type"))==3,"unavailable spectral mode is switched off, supported modes are kept");
  check(synth->testImportNotes().size()==3,"every omitted feature is reported once");
  auto broken=modern;broken["settings"]["filter_1_model"]=99;threw=false;try{synth->testImport(broken);}catch(...){threw=true;}
  check(threw&&synth->get(synth->controlIndex("filter_1_cutoff"))==64,"out-of-range engine value is still rejected before sound mutation");
  synth->testImport(ramps);check(synth->testImportNotes().empty(),"clean import clears earlier notes");}
 // Effects mixed to exactly 0 behind a macro are not processed; anything else keeps running.
 {auto settleFx=[&]{for(int i=0;i<40;++i){render();std::this_thread::sleep_for(std::chrono::milliseconds(2));}};
  nlohmann::json gated={{"synth_version","1.0.6"},{"settings",{{"reverb_on",1},{"reverb_dry_wet",0},{"delay_on",1},{"delay_dry_wet",0},{"chorus_on",1},{"chorus_dry_wet",0.3},{"macro_control_1",0},
   {"modulations",nlohmann::json::array({{{"source","macro_control_1"},{"destination","reverb_dry_wet"}},{{"source","lfo_1"},{"destination","delay_dry_wet"}}})}}}};
  synth->testImport(gated);settleFx();
  check(synth->getBool(synth->controlIndex("reverb_on"))&&!synth->testEngineSwitch("reverb_on"),"reverb at mix 0 behind a closed macro is skipped while its switch stays on");
  check(synth->testEngineSwitch("delay_on"),"effect whose mix is modulated by an LFO keeps running");
  check(synth->testEngineSwitch("chorus_on"),"effect with a non-zero mix keeps running");
  synth->set(synth->controlIndex("macro_control_1"),.4f);settleFx();
  check(synth->testEngineSwitch("reverb_on"),"opening the macro starts the effect again");
  synth->set(synth->controlIndex("macro_control_1"),0);settleFx();synth->set(synth->controlIndex("reverb_dry_wet"),.2f);settleFx();
  check(synth->testEngineSwitch("reverb_on"),"raising the mix itself starts the effect again");
  synth->set(synth->controlIndex("reverb_on"),0);settleFx();check(!synth->testEngineSwitch("reverb_on"),"switching the effect off still works");
  synth->testImport(ramps);settleFx();}
 nlohmann::json stack={{"synth_version","1.5.5"},{"settings",savedAssets}};stack["settings"]["osc_1_spectral_morph_phase"]=.5;
 for(int choice:{5,6,8,11,12}){stack["settings"]["osc_1_stack_style"]=choice;synth->testImport(stack);int expected=choice==5?11:choice>=6&&choice<=11?choice-1:choice;check(synth->get(synth->controlIndex("osc_1_stack_style"))==expected,"1.5 stack IDs translated without changing intervals");}
 stack["synth_version"]="1.0.6";stack["settings"].erase("osc_1_spectral_morph_phase");stack["settings"]["osc_1_stack_style"]=8;synth->testImport(stack);check(synth->get(synth->controlIndex("osc_1_stack_style"))==8,"1.0 stack IDs preserved");
 auto captureTable=[&](bool square){
  LineGenerator line;if(square)line.initSquare();else line.initSin();vital::Wavetable table(257);WavetableCreator creator(&table);creator.jsonToState(line.stateToJson());
  nlohmann::json sound={{"synth_version","1.0.6"},{"settings",savedAssets}};sound["settings"]["wavetables"][0]=creator.stateToJson();sound["settings"]["osc_1_random_phase"]=0;sound["settings"]["env_1_attack"]=0;sound["settings"]["env_1_sustain"]=1;sound["settings"]["polyphony"]=1;
  synth->testImport(sound);synth->reset();auto note=mpc::MidiEvent::noteOn(0,60,100);ctx.midi=&note;ctx.numMidiEvents=1;render();ctx.numMidiEvents=0;
  for(int i=0;i<40;++i)render();std::vector<float> capture;for(int i=0;i<8;++i){render();capture.insert(capture.end(),left,left+ctx.numFrames);}ctx.midi=notes.data();return capture;
 };
 auto sine=captureTable(false),square=captureTable(true),sineAgain=captureTable(false);double changed=0,restored=0;
 for(size_t i=0;i<sine.size();++i){changed+=std::abs(sine[i]-square[i]);restored+=std::abs(sine[i]-sineAgain[i]);}
 check(changed/sine.size()>.005&&restored/sine.size()<1e-5,"same-frame-count wavetable replacement changes audio and restores exactly");
 std::cout<<"wavetable_changed_mean="<<changed/sine.size()<<" restored_mean="<<restored/sine.size()<<std::endl;
 if(argc>1&&argv[1][0]){int accepted=0,rejected=0;for(const auto& e:std::filesystem::recursive_directory_iterator(argv[1]))if(e.path().extension()==".vital"){
   try{std::ifstream in(e.path());nlohmann::json preset;in>>preset;synth->testImport(preset);++accepted;ctx.numMidiEvents=8;render();ctx.numMidiEvents=0;for(int b=0;b<8;++b)render();std::cout<<"IMPORTED "<<e.path().string()<<std::endl;}
   catch(const std::exception& ex){++rejected;std::cout<<"REJECTED "<<e.path().string()<<" : "<<ex.what()<<std::endl;}}
   std::cout<<"presets_imported="<<accepted<<" presets_rejected="<<rejected<<std::endl;check(accepted>0,"real local presets imported");}
 check(finite,"finite audio after imported presets");
 if(argc>3){
  int selector=id("preset_select");auto chooseFile=[&](const std::string& name){for(int n=1;n<=int(synth->param(selector).max);++n){std::string label;synth->formatValue(selector,float(n),label);if(label==name){synth->set(selector,float(n));synth->set(id("library_load"),synth->getBool(id("library_load"))?0:1);settle();return true;}}return false;};
  auto tap=[&](const std::string& name){int p=id(name);synth->set(p,synth->getBool(p)?0:1);};
  synth->testInit();settle();
  check(synth->getBool(Vitaleon::FullMode)&&synth->displayText(id("library_loaded_0"))=="Init","Init sound uses the single full engine");
  synth->setNormalised(id("home_1"),.5f);synth->setNormalised(id("home_5"),1);
  check(std::abs(synth->getNormalised(synth->controlIndex("filter_1_cutoff"))-.5f)<1e-5&&synth->getInt(synth->controlIndex("osc_1_unison_voices"))==16,"overview controls always use full-engine parameters and ranges");
  tap("library_row_0");
  check(!synth->busy()&&synth->getBool(Vitaleon::FullMode),"touch browsing does not stop audio or switch engine");
  check(synth->displayText(id("library_selected_0"))=="01 Import Test","touch row selects actual indexed filename");
  check(chooseFile("03 Unsupported")&&synth->getBool(Vitaleon::FullMode),"unsupported preset preserves current engine");
  check(chooseFile("01 Import Test")&&synth->get(synth->controlIndex("filter_1_cutoff"))==60,"Content folder preset loads through worker");
  check(synth->getBool(Vitaleon::FullMode),"valid import remains on the single engine");
  check(synth->displayText(id("library_loaded_0"))=="01 Import Test","loaded label identifies actual successful import");
  check(chooseFile("02 Bank Test")&&synth->get(synth->controlIndex("filter_1_cutoff"))==77,"compressed bank preset loads through worker");
  check(chooseFile("03 Unsupported")&&synth->get(synth->controlIndex("filter_1_cutoff"))==77&&synth->getInt(id("load_status"))==2,"unsupported file preserves previous sound controls and reports error");
  check(synth->displayText(id("library_loaded_0"))=="02 Bank Test","failed load retains previous loaded-name label");
  std::string message;for(int i=0;i<4;++i){auto part=synth->displayText(id("display_load_text_"+std::to_string(i)));check(part.size()<=31,"status segment fits MPC host string limit");message+=part;}
  check(message==synth->displayText(id("load_status"))&&message.find("filter_1_model")!=std::string::npos,"MPC status segments retain full unsupported-feature reason");
  tap("library_load");settle();check(synth->getInt(id("load_status"))==2,"same failed preset can be retried without changing selection");
  tap("library_next");check(synth->getInt(id("library_page"))==1&&!synth->busy(),"touch page navigation is read-only");
  /* Release makes no write with persistent command bits. */ check(synth->getInt(id("library_page"))==1,"button release needs no second parameter write");
  tap("library_bank_next");check(synth->getInt(id("library_page"))==0&&synth->displayText(id("library_bank_text_0"))=="Imported Bank","bank navigation resets list to first page");
  tap("library_row_0");check(synth->displayText(id("library_selected_0"))=="02 Bank Test","filtered bank row maps to correct preset");
  tap("library_load");settle();check(synth->get(synth->controlIndex("filter_1_cutoff"))==77,"LOAD applies preset selected by touch row");
  synth->set(synth->controlIndex("filter_1_cutoff"),23);tap("library_load");settle();check(synth->get(synth->controlIndex("filter_1_cutoff"))==77,"LOAD reloads same preset after edits");
  auto beforeBlank=synth->getInt(selector);tap("library_row_5");check(synth->getInt(selector)==beforeBlank,"empty row cannot select or load another sound");
  if(argc>2){nlohmann::json values=nlohmann::json::array();for(int i=0;i<synth->numParams();++i)values.push_back({{"id",synth->param(i).id},{"name",synth->param(i).name},{"index",i},{"value",synth->getNormalised(i)},{"display",synth->displayText(i)}});std::ofstream file(std::string(argv[2])+"-library.json");file<<values.dump(2);}
  auto loadedState=synth->internalSaveState();synth->testInit();settle();check(synth->internalLoadState(loadedState.data(),loadedState.size()),"imported state accepted");settle();check(synth->displayText(id("library_loaded_0"))=="02 Bank Test"&&synth->get(synth->controlIndex("filter_1_cutoff"))==77,"loaded filename and sound survive project restore");
 }
 if(argc>4){
  nlohmann::json skin;std::ifstream input(argv[4]);input>>skin;
  bool browserCommands=false;
  for(const auto& tab:skin["pageData"]["tabs"])
   for(const auto& entry:tab["componentDefinition"]["componentsData"]){
    std::string name=entry["componentData"]["name"];
    browserCommands|=name=="SELECT"||name=="LOAD";
   }
  check(!browserCommands,"failed custom SELECT/LOAD browser is absent from the shipped skin");
 }
 // Verify audible filter action, rather than checking only the displayed value.
 // Both HOME and FILTER 1 use the same parameter handles.
 {
  synth->testInit();settle();
  synth->set(id("home_filter_on"),1);
  synth->set(synth->controlIndex("osc_1_wave_frame"),170);synth->set(synth->controlIndex("env_1_sustain"),1);
  auto energy=[&](float cutoff,bool enabled){
   synth->set(id("home_filter_on"),enabled?1:0);synth->setNormalised(id("home_1"),cutoff);
   synth->reset();auto note=mpc::MidiEvent::noteOn(0,60,100);ctx.midi=&note;ctx.numMidiEvents=1;render();ctx.numMidiEvents=0;
   for(int b=0;b<150;++b)render();double e=0;for(int b=0;b<60;++b){render();for(int n=0;n<ctx.numFrames;++n)e+=left[n]*left[n]+right[n]*right[n];}
   ctx.midi=notes.data();return e;
  };
  double low=energy(.08f,true),high=energy(.9f,true),bypass=energy(.08f,false);
  std::cout<<"filter_audio_program=Init low="<<low<<" high="<<high<<" bypass="<<bypass<<std::endl;
  check(high>low*4&&high>1e-8&&bypass>low*4,"Init HOME/FILTER 1 cutoff changes rendered audio; bypass is explicit");
 }
 auto legacy=std::make_unique<VitaleonLegacy>();legacy->set(Vitaleon::Cutoff,812.5f);legacy->set(Vitaleon::Unison,8);auto oldState=legacy->internalSaveState();
 check(synth->internalLoadState(oldState.data(),oldState.size()),"0.1.4 state accepted");settle();
 check(synth->getBool(Vitaleon::FullMode)&&synth->getInt(synth->controlIndex("osc_1_unison_voices"))==8&&std::abs(vital::utils::midiNoteToFrequency(synth->get(synth->controlIndex("filter_1_cutoff")))-812.5f)<.1f,"old project cutoff and unison migrate into the single full engine");
 check(synth->displayText(id("library_loaded_0"))=="Converted project","old project restore cannot retain an unrelated preset label");
 // Freeze telemetry for byte-exact serialization: settle() processes audio,
 // and its scheduling can otherwise change only the live waveform readouts.
 synth->set(id("visuals"),0);
 auto converted=synth->internalSaveState();auto convertedAssets=synth->testAssets();synth->testInit();settle();
 check(synth->internalLoadState(converted.data(),converted.size()),"converted project reload accepted");settle();
 auto convertedAgain=synth->internalSaveState();
 if(convertedAgain!=converted&&argc>2){std::ofstream a(std::string(argv[2])+".state-before",std::ios::binary),b(std::string(argv[2])+".state-after",std::ios::binary);a.write(reinterpret_cast<const char*>(converted.data()),converted.size());b.write(reinterpret_cast<const char*>(convertedAgain.data()),convertedAgain.size());}
 check(synth->testAssets()==convertedAssets&&convertedAgain==converted,"converted waveforms and state round-trip exactly");
 // 0.2.x saved many more than sixteen parameters while its old mode was active.
 synth->publishNormalised(Vitaleon::FullMode,0);auto oldExpanded=synth->internalSaveState();
 check(synth->internalLoadState(oldExpanded.data(),oldExpanded.size()),"expanded old-mode project accepted");settle();
 check(synth->getBool(Vitaleon::FullMode)&&std::abs(vital::utils::midiNoteToFrequency(synth->get(synth->controlIndex("filter_1_cutoff")))-812.5f)<.1f,"0.2.x old-mode flag triggers conversion rather than an inactive UI");
 synth->set(Vitaleon::FullMode,0);check(synth->getBool(Vitaleon::FullMode),"retired mode parameter cannot reactivate a second engine");
 synth->testInit();settle();check(synth->getBool(Vitaleon::FullMode),"Init after a converted project uses the one engine");
 // Exercise the exported entry point and real VST dispatcher used by the
 // host's PRESET dropdown, independently of the retired custom browser.
 if(argc>3){
  AEffect* effect=VSTPluginMain(displayHost);
  auto* native=static_cast<Vitaleon*>(effect->object);
  auto dispatch=[&](int opcode,int index=0,VstIntPtr value=0,void* ptr=nullptr,float opt=0){return effect->dispatcher(effect,opcode,index,value,ptr,opt);};
  dispatch(effOpen);dispatch(effSetSampleRate,0,0,nullptr,44100);dispatch(effSetBlockSize,0,512);dispatch(effMainsChanged,0,1);
  auto waitNative=[&]{for(int i=0;i<10000&&native->busy();++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(!native->busy(),"native program service settled");};
  int loose=-1,bank=-1,secondBank=-1,unsupported=-1;std::set<std::string> names;bool validNames=true;
  for(int p=0;p<effect->numPrograms;++p){char name[256]{};validNames&=dispatch(effGetProgramNameIndexed,p,0,name)==1;std::string text=name;validNames&=!text.empty()&&text.size()<=24&&names.insert(text).second;nlohmann::json utf8=text;try{utf8.dump();}catch(...){validNames=false;}if(text=="01 Import Test")loose=p;if(text=="02 Bank Test")bank=p;if(text=="03 Unsupported")unsupported=p;if(text=="02 Bank Test #2")secondBank=p;}
  check(effect->numPrograms==11&&loose==0&&bank>0&&secondBank>0&&unsupported>0,"VST entry point exposes exactly the eleven sounds from two banks and loose files, no built-in programs");
  check(validNames&&dispatch(effGetProgram)==0,"native program enumeration has unique valid names and keeps the first sound current");
  auto select=[&](int p){dispatch(effSetProgram,0,p);waitNative();};
  int cutoff=native->controlIndex("filter_1_cutoff");
  {waitNative();char started[256]{};dispatch(effGetProgramName,0,0,started);
   check(native->get(cutoff)==60&&dispatch(effGetProgram)==0&&std::string(started)=="01 Import Test"&&native->displayText(id("library_loaded_0"))=="01 Import Test","a new instance starts with the first installed sound loaded");
   check(native->displayText(id("library_page_text"))=="11 installed sounds","library names the number of installed sounds");}
  select(bank);char current[256]{};dispatch(effGetProgramName,0,0,current);
  check(native->get(cutoff)==77&&dispatch(effGetProgram)==bank&&std::string(current)=="02 Bank Test","native PRESET selection loads compressed bank and reports current program");
  select(secondBank);check(native->get(cutoff)==88&&native->displayText(id("library_bank_text_0"))=="Second Bank","second bank with duplicate sound name selects correct archive");
  select(bank);check(native->get(cutoff)==77&&native->displayText(id("library_bank_text_0"))=="Imported Bank","switching back restores first bank without stale sound data");
  auto bankAssets=native->testAssets();
  native->set(cutoff,23);select(bank);check(native->get(cutoff)==77,"native selection reloads same bank sound after edits");
  select(loose);check(native->get(cutoff)==60,"native PRESET selection loads loose Vital file");
  select(unsupported);check(native->get(cutoff)==60&&native->displayText(id("library_loaded_0"))=="01 Import Test"&&native->getInt(id("load_status"))==2,"unsupported native program preserves last successful sound and reports failure");
  dispatch(effSetProgram,0,bank);dispatch(effSetProgram,0,loose);dispatch(effSetProgram,0,bank);waitNative();check(native->get(cutoff)==77,"rapid native program changes load the final selection");
  native->set(cutoff,42);bankAssets=native->testAssets();void* chunk=nullptr;auto bytes=dispatch(effGetChunk,0,0,&chunk);std::vector<uint8_t> edited(static_cast<uint8_t*>(chunk),static_cast<uint8_t*>(chunk)+bytes);
  dispatch(effSetProgram,0,loose);dispatch(effSetChunk,0,edited.size(),edited.data());waitNative();
  if(std::abs(native->get(cutoff)-42)>=1e-4||native->testAssets()!=bankAssets||native->displayText(id("library_loaded_0"))!="02 Bank Test")std::cout<<"RESTORE DETAIL cutoff="<<native->get(cutoff)<<" name="<<native->displayText(id("library_loaded_0"))<<" status="<<native->displayText(id("load_status"))<<" diff="<<nlohmann::json::diff(bankAssets,native->testAssets()).dump()<<std::endl;
  check(std::abs(native->get(cutoff)-42)<1e-4&&native->testAssets()==bankAssets&&native->displayText(id("library_loaded_0"))=="02 Bank Test","project chunk overrides pending native selection and restores edited bank sound");
  native->testInit();waitNative();check(native->get(cutoff)!=42&&native->displayText(id("library_loaded_0"))=="Init","Init replaces imported sound");select(bank);check(native->get(cutoff)==77,"native bank loads again after Init");
  bool audioFinite=true;for(int i=0;i<40;++i){effect->processReplacing(effect,nullptr,outputs,128);for(int n=0;n<128;++n)audioFinite&=std::isfinite(left[n])&&std::isfinite(right[n]);}check(audioFinite,"native VST processReplacing stays finite across program changes");
  check(displayUpdates.load()>0&&!displayOnAudio.load(),"host display invalidation is reentrant and stays off the audio callback");
  // Drive the real browser command parameters and actual exported program table.
  auto command=[&](int n){int p=id("storage_command_"+std::to_string(n));effect->setParameter(effect,p,native->getNormalised(p)>.5f?0:1);waitNative();};
  auto openItem=[&](const std::string& text){
    // An opened bank is shown like a folder (0.3.19): leave it before looking for the next item.
    {auto place=native->displayText(id("storage_path_0"));auto ends=[&](const char* tail){std::string t=tail;return place.size()>=t.size()&&place.compare(place.size()-t.size(),t.size(),t)==0;};
     if(ends(".vitalbank")||ends(".zip"))command(1);}
    for(int page=0;page<10;++page){for(int row=0;row<6;++row){auto name=native->displayText(id("storage_row_"+std::to_string(row*2)))+native->displayText(id("storage_row_"+std::to_string(row*2+1)));if(name.find(text)!=std::string::npos){command(6+row);return true;}}command(4);}return false;
  };
  command(0);check(openItem("Plugin Content")&&openItem("Storage Tests"),"storage browser navigates drive and directory through UI command parameters");
  float unchanged=native->get(cutoff);
  check(openItem("A small"),"browser opens external bank outside Content/Banks");
  check(native->get(cutoff)==unchanged&&effect->numPrograms>=1&&effect->numPrograms<=2,"bank indexing publishes names without changing sounding parameters");
  char externalName[256]{};dispatch(effGetProgramNameIndexed,0,0,externalName);
  check(std::string(externalName)=="Same name","selected bank sounds appear in native PRESET program names");
  select(0);check(native->get(cutoff)==31,"native PRESET loads selected external bank");
  command(3);check(openItem("B large"),"browser selects larger bank");
  check(effect->numPrograms==4&&native->get(cutoff)==31,"larger bank refresh preserves current sound in a valid retained slot");
  for(int n=0;n<1000&&cachedProgramCount.load()!=effect->numPrograms;++n)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  check(cachedProgramCount.load()==4&&validReentrantNames.load(),"host refresh callback reentrantly reads the new program count and all names");
  select(2);check(native->get(cutoff)==73,"larger bank last sound reaches correct archive entry");
  command(3);check(openItem("C same size"),"browser selects smaller bank");
  check(effect->numPrograms==2&&dispatch(effGetProgramNameIndexed,2,0,externalName)==0,"smaller bank removes stale names and rejects removed program indices");
  select(0);check(native->get(cutoff)==81,"same preset filename in different bank loads correct sound");
  void* bankChunk=nullptr;auto bankBytes=dispatch(effGetChunk,0,0,&bankChunk);std::vector<uint8_t> savedBank((uint8_t*)bankChunk,(uint8_t*)bankChunk+bankBytes);
  command(3);openItem("D empty");check(native->get(cutoff)==81&&effect->numPrograms==2,"empty bank preserves current catalog and sound");
  command(3);openItem("E broken");check(native->get(cutoff)==81&&effect->numPrograms==2,"malformed bank preserves current catalog and sound");
  command(3);openItem("A small");select(0);check(native->get(cutoff)==31,"external bank A/B/A switching is stable");
  dispatch(effSetChunk,0,savedBank.size(),savedBank.data());waitNative();
  check(native->get(cutoff)==81&&native->displayText(id("active_bank_0"))=="C same size"&&dispatch(effGetProgram)==0,"project restore recovers bank identity and current native slot");
  auto removed=std::filesystem::path(argv[3])/"Content/Storage Tests/C same size.vitalbank";
  auto heldPath=removed;heldPath+=".test-unavailable";std::filesystem::rename(removed,heldPath);
  dispatch(effSetChunk,0,savedBank.size(),savedBank.data());waitNative();
  check(native->get(cutoff)==81&&native->displayText(id("library_loaded_0"))=="Same name","embedded project sound restores when original bank drive is unavailable");
  {AEffect* fresh=VSTPluginMain(displayHost);auto* instance=static_cast<Vitaleon*>(fresh->object);fresh->dispatcher(fresh,effOpen,0,0,nullptr,0);fresh->dispatcher(fresh,effSetSampleRate,0,0,nullptr,44100);fresh->dispatcher(fresh,effMainsChanged,0,1,nullptr,0);fresh->dispatcher(fresh,effSetChunk,0,savedBank.size(),savedBank.data(),0);
   for(int n=0;n<10000&&instance->busy();++n){fresh->processReplacing(fresh,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
   char retainedName[256]{};fresh->dispatcher(fresh,effGetProgramName,0,0,retainedName,0);
   check(!instance->busy()&&instance->get(cutoff)==81&&std::string(retainedName)=="[Current sound]","fresh instance restores missing-bank project into a valid current-sound slot");fresh->dispatcher(fresh,effClose,0,0,nullptr,0);}
  std::filesystem::rename(heldPath,removed);
  command(5);check(effect->numPrograms==12&&native->get(cutoff)==81,"Installed banks returns original catalog while retaining external sound");
  command(0);check(!native->displayText(id("storage_row_0")).empty(),"Drives rescans storage roots");
  openItem("Plugin Content");openItem("Storage Tests");openItem("Folder 0");bool emptyRows=true;for(int row=0;row<6;++row)emptyRows&=native->getNormalised(id("storage_available_"+std::to_string(row)))==0;
  check(emptyRows,"empty folder hides all six OPEN buttons and keeps navigation available");command(0);
  // 0.3.19 library: a tapped button goes back to off, a bank opens like a folder,
  // its sounds are LOAD rows, UP leaves the bank, removable drives come first.
  {void* before=nullptr;auto beforeBytes=dispatch(effGetChunk,0,0,&before);std::vector<uint8_t> kept((uint8_t*)before,(uint8_t*)before+beforeBytes);
   check(native->getNormalised(id("storage_command_0"))==0,"a tapped library button returns to off");
   check(native->displayText(id("storage_row_0"))=="DRIVE"&&native->getNormalised(id("storage_available_0"))==0.5f,"the drive list shows kind and an OPEN row");
   openItem("Plugin Content");openItem("Storage Tests");
   // The folder has more than six entries: look through the pages.
   auto findRow=[&](const std::string& name){for(int page=0;page<10;++page){for(int row=0;row<6;++row)if(native->displayText(id("storage_row_"+std::to_string(row*2+1)))==name)return row;command(4);}return -1;};
   int bankAt=findRow("B large");bool bankRow=bankAt>=0&&native->displayText(id("storage_row_"+std::to_string(bankAt*2)))=="BANK"&&native->getNormalised(id("storage_available_"+std::to_string(bankAt)))==0.5f;
   check(bankRow,"a bank is listed by name without extension and offers OPEN");
   openItem("B large");
   check(native->displayText(id("storage_row_0"))=="SOUND"&&native->displayText(id("storage_row_1"))=="Same name"&&native->displayText(id("storage_row_3"))=="Sound 1"&&native->getNormalised(id("storage_available_1"))==1.0f&&native->getNormalised(id("storage_available_3"))==0,"an opened bank lists its sounds as LOAD rows");
   command(7);check(native->get(cutoff)==72&&native->testAssets().value("preset_name",std::string())=="Sound 1","LOAD on a bank row loads that sound");
   command(1);bool back=findRow("B large")>=0;
   check(back,"UP leaves the bank and shows its folder again");
   check(vitaleon_storage::drivePriority("vfat")<vitaleon_storage::drivePriority("ext4")&&vitaleon_storage::drivePriority("exfat")==1,"removable file systems are listed before internal ones");
   {std::string path,type;check(vitaleon_storage::parseMount("98 61 8:17 / /media/USB\\040STICK rw,noexec - vfat /dev/sdb1 rw,fmask=0022",path,type)&&path=="/media/USB STICK"&&type=="vfat","mount lines give the path and the file system");}
   command(5);command(0);dispatch(effSetChunk,0,kept.size(),kept.data());waitNative();}
  // Every rendered module/section must remap Q-Links to its actual parameters.
  auto spec=nlohmann::json::parse(surfaceMapJson);bool mapped=true;int sectionCount=0;nlohmann::json snapshots;int expectedSections=0;for(const auto& pageSpec:spec)expectedSections+=int(pageSpec["sections"].size());
  for(int page=0;page<int(spec.size());++page){native->set(id("ui_page"),float(page));
    for(int sec=0;sec<int(spec[page]["sections"].size());++sec){native->set(id("ui_section"),float(sec));waitNative();++sectionCount;
      int slot=0;for(const auto& value:spec[page]["sections"][sec]["items"]){std::string name=value;int target=-1;for(int p=0;p<native->numParams();++p)if(native->param(p).name==name)target=p;if(target<0){std::replace(name.begin(),name.end(),'_',' ');for(int p=0;p<native->numParams();++p)if(native->param(p).name==name)target=p;}mapped&=target>=0&&std::abs(native->getNormalised(id("ui_knob_"+std::to_string(slot)))-native->getNormalised(target))<1e-6;++slot;}
      if(argc>2){auto values=nlohmann::json::array();for(int p=0;p<native->numParams();++p)values.push_back({{"index",p},{"name",native->param(p).name},{"value",native->getNormalised(p)},{"display",native->displayText(p)}});snapshots[std::to_string(page)+"-"+std::to_string(sec)]=std::move(values);}
    }
  }
  check(mapped&&sectionCount==expectedSections,"all "+std::to_string(int(spec.size()))+" module views and "+std::to_string(expectedSections)+" sections map Q-Links to visible controls");
  if(argc>2){std::ofstream f(std::string(argv[2])+"-surface.json");f<<snapshots.dump();}
  native->set(id("ui_page"),8);native->set(id("ui_section"),0);native->setNormalised(id("ui_knob_0"),.37f);check(std::abs(native->getNormalised(cutoff)-.37f)<1e-6,"filter cutoff Q-Link writes the actual full-engine filter");
  native->set(id("ui_page"),15);native->setNormalised(id("ui_knob_0"),.4f);check(std::abs(native->getNormalised(native->controlIndex("env_6_attack"))-.4f)<1e-6,"Envelope 6 view edits Envelope 6 through Q-Link");
  native->set(id("ui_page"),23);native->setNormalised(id("ui_knob_0"),.6f);check(std::abs(native->getNormalised(native->controlIndex("lfo_8_frequency"))-.6f)<1e-6,"LFO 8 view edits LFO 8 through Q-Link");
  // The sound parameter is announced once the control rests (0.3.23), not while it moves.
  for(int i=0;i<1000&&!native->testSlotsAtRest();++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  check(native->testSlotsAtRest()&&automatedTarget.load()==native->controlIndex("lfo_8_frequency"),"Q-Link automation reports stable LFO 8 parameter rather than navigation proxy");
  // The MOD tab hosts two groups on one Q-Link bank: a matrix knob write must
  // stay on the matrix page instead of pulling the LFO page back.
  native->set(id("ui_gpage_6"),0);native->set(id("ui_section"),0);
  const int modPage=native->getInt(id("ui_page"));
  check(native->getInt(id("ui_group"))==6&&modPage==34,"the OTHER group page shows the matrix");
  native->setNormalised(id("ui_g4_slot_0"),.8f);
  check(native->getInt(id("ui_page"))==modPage&&native->getInt(id("ui_group"))==6,"a MOD bank write keeps the matrix page");
  native->set(id("ui_gpage_0"),1);const int homePage=native->getInt(id("ui_page"));
  native->setNormalised(id("ui_g0_slot_0"),.5f);
  check(native->getInt(id("ui_page"))==homePage,"a single-group tab keeps its own page");
  // 0.3.22: knobs and faders on screen are bound to their Q-Link slot, so the host moves
  // the control it wrote. 0.3.23: while a control moves, the plugin calls the host not
  // once (0.3.21 redrew the page on every change and all controls stuttered; 0.3.22 sent
  // audioMasterAutomate on every change and the controls felt sluggish on the device).
  {native->set(id("visuals"),0);native->set(id("ui_page"),0);native->set(id("ui_section"),0);
   auto idle=[&](int blocks){for(int i=0;i<blocks;++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}};
   auto rest=[&]{for(int i=0;i<1000&&!native->testSlotsAtRest();++i)idle(1);idle(20);};
   auto named=[&](const std::string& name){for(int p=0;p<native->numParams();++p)if(native->param(p).name==name)return p;return -1;};
   int knob=id("ui_knob_0"),sound=id("home_0"),volume=native->controlIndex("volume");effect->setParameter(effect,knob,.5f);rest();
   unsigned shown=displayUpdates.load(),calls=automateCalls.load();float turned=.8f;automatedTarget.store(-1);
   for(int step=1;step<=20;++step){effect->setParameter(effect,knob,.5f+step*.015f);idle(2);}
   check(displayUpdates.load()==shown&&automateCalls.load()==calls,"a moving control makes no call into the host");
   check(std::abs(native->getNormalised(sound)-turned)<1e-5&&std::abs(effect->getParameter(effect,knob)-turned)<1e-5,"a Q-Link turn reaches the sound and the slot keeps the value the host wrote");
   rest();
   check(automateCalls.load()==calls+1&&automatedTarget.load()==volume&&displayUpdates.load()==shown,"a rested control announces its sound parameter once, without a page redraw");
   check(native->getInt(id("ui_bank"))==0,"the first four Q-Links are group 1");
   // Another knob group: the frame parameter is announced at once, the page redrawn at rest.
   unsigned framed=readoutUpdates.load();shown=displayUpdates.load();
   for(int step=0;step<10;++step){effect->setParameter(effect,id("ui_knob_5"),.3f+step*.01f);idle(2);}
   check(native->getInt(id("ui_bank"))==1&&readoutUpdates.load()==framed+1&&displayUpdates.load()==shown,"a knob of the second group moves the Q-Link frame without a page redraw while it turns");
   rest();check(displayUpdates.load()==shown+1,"the page is redrawn once after the knob group changed and the control rests");
   effect->setParameter(effect,knob,turned);rest();check(native->getInt(id("ui_bank"))==0,"a knob of the first group moves the frame back");
   // Small Q-Link increments on a stepped control add up instead of snapping back to the step.
   {int voices=named("Voices"),slot=id("ui_knob_1");native->set(voices,4);idle(10);float start=effect->getParameter(effect,slot);int count=native->getInt(voices);
    for(int step=1;step<=24;++step){effect->setParameter(effect,slot,effect->getParameter(effect,slot)+.004f);idle(3);}
    check(std::abs(effect->getParameter(effect,slot)-(start+.096f))<1e-4&&native->getInt(voices)==count+1,"small Q-Link increments on a stepped control add up");rest();
    // 0.3.24: the voice control ends at eight, also for a project saved with more.
    effect->setParameter(effect,voices,1.0f);check(native->getInt(voices)==8&&native->param(voices).max==8,"the voice control ends at eight voices");
    {native->set(voices,3);rest();auto saved=native->internalSaveState();std::vector<uint8_t> chunk(saved.begin(),saved.end());
     const std::string key=std::string(1,char(6))+"voices";auto at=std::search(chunk.begin(),chunk.end(),key.begin(),key.end());bool found=at!=chunk.end();
     if(found){size_t pos=size_t(at-chunk.begin())+key.size();float plain=24.0f,norm=23.0f/31.0f;std::memcpy(&chunk[pos],&plain,4);std::memcpy(&chunk[pos+4],&norm,4);}
     bool accepted=found&&native->internalLoadState(chunk.data(),chunk.size());for(int i=0;i<3000&&native->busy();++i)idle(1);rest();
     check(found&&accepted&&native->getInt(voices)==8,"a project saved with 24 voices is restored at eight");}}
   // A list on screen is bound to its own parameter: the host hears of its step at once.
   native->set(id("ui_page"),16);native->set(id("ui_section"),0);idle(20);int list=named("LFO sync");
   bool stepped=list>=0&&native->param(list).kind!=mpc::Param::Kind::Float;float old=stepped?native->getNormalised(list):0;automatedTarget.store(-1);
   effect->setParameter(effect,id("ui_knob_1"),old>.5f?0.0f:1.0f);for(int i=0;i<100&&automatedTarget.load()<0;++i)idle(1);
   check(stepped&&native->getNormalised(list)!=old&&automatedTarget.load()==list,"a list turned by Q-Link is announced to the host for redrawing");
   rest();native->set(id("ui_page"),0);native->set(id("visuals"),1);}
  // Single .vital files on any drive: tap = load, folder becomes the PRESET list.
  command(0);openItem("Plugin Content");openItem("Storage Tests");openItem("Loose Sounds");
  check(openItem("Second loose"),"browser lists single .vital files");
  check(native->get(cutoff)==53&&native->displayText(id("library_loaded_0"))=="Second loose","tapping a single .vital file loads that sound");
  check(native->displayText(id("load_status")).find("Loaded with changes")==0,"status names what a tolerant import left out");
  check(native->displayText(id("active_bank_0"))=="Loose Sounds"&&dispatch(effGetProgramNameIndexed,0,0,externalName)==1&&std::string(externalName)=="First loose","folder of loose sounds becomes the native PRESET list");
  select(0);check(native->get(cutoff)==52&&native->displayText(id("load_status"))=="Loaded: First loose","other sounds of that folder load from PRESET");
  {void* chunk2=nullptr;auto n2=dispatch(effGetChunk,0,0,&chunk2);std::vector<uint8_t> saved((uint8_t*)chunk2,(uint8_t*)chunk2+n2);command(5);
   dispatch(effSetChunk,0,saved.size(),saved.data());waitNative();
   check(native->get(cutoff)==52&&native->displayText(id("active_bank_0"))=="Loose Sounds","project restore recovers a folder-based sound list");}
  // Matrix: PICK on a row -> group page -> entry page -> back on the row's matrix page.
  {int matrix=-1,groups=-1,targets=-1;for(int page=0;page<int(spec.size());++page){if(!spec[page]["chart"].is_string())continue;std::string chart=spec[page]["chart"];if(chart=="mxtable")matrix=page;if(chart=="pickgroup")groups=page;if(chart=="picktarget")targets=page;}
   check(matrix>=0&&groups>=0&&targets>=0&&spec[groups]["hidden"]==true&&spec[targets]["hidden"]==true,"matrix table and hidden picker views exist");
   native->set(id("ui_page"),float(matrix));waitNative();native->set(id("ui_pick"),10);waitNative();
   check(native->getInt(id("ui_page"))==groups&&native->getInt(id("ui_section"))==0&&native->getInt(id("edit_route"))==10&&native->getInt(id("ui_pick"))==0,"PICK on a matrix row selects that route, opens the group page and re-arms itself");
   native->set(id("ui_section"),3);waitNative();
   check(native->getInt(id("ui_page"))==targets&&native->getInt(id("ui_section"))==3,"a group button opens that group's entries");
   int proxy=id("edit_route_1"),real=id("route_10_destination");native->set(proxy,57);waitNative();
   check(native->getInt(real)==57&&native->getInt(id("ui_page"))==matrix&&native->getInt(id("ui_section"))==1,"choosing an entry sets that route's target and returns to its matrix page");
   native->set(id("ui_pick"),10);waitNative();check(native->getInt(id("ui_page"))==groups,"the same row can be picked again");native->set(proxy,0);waitNative();
   check(native->getInt(real)==0&&native->getInt(id("ui_page"))==matrix,"NO TARGET clears the route and returns to the matrix");
   native->set(id("ui_group"),0);waitNative();native->set(id("ui_group"),6);waitNative();
   check(native->getInt(id("ui_page"))==matrix,"the OTHER group remembers the matrix, never a hidden picker view");
   native->set(id("ui_page"),0);waitNative();}
  // Live graphs (0.3.18): a changed picture asks the host for ONE display update;
  // readouts are never sent as single parameter notifications (they delayed the
  // MPC's audio callback), and a picture that does not change asks for nothing.
  {native->set(id("ui_page"),4);waitNative();for(int i=0;i<120;++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
   unsigned single=readoutUpdates.load(),shown=displayUpdates.load();int frame=native->controlIndex("osc_1_wave_frame");native->set(frame,native->get(frame)<100?170.0f:20.0f);
   for(int i=0;i<2000&&displayUpdates.load()==shown;++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
   check(displayUpdates.load()>shown,"a changed oscillator graph asks the host for a display update");
   for(int i=0;i<150;++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
   unsigned still=displayUpdates.load();
   for(int i=0;i<200;++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
   check(displayUpdates.load()==still,"a graph that does not change asks for nothing");
   check(readoutUpdates.load()==single,"graph readouts are never sent as single parameter notifications");
   // Columns 97-128 live in the second readout block; the frame is moved to a second shape so both blocks must follow.
   auto column=[&](int c){return native->getNormalised(c<96?id("display_wave_0_0")+c:id("display_wave_bank_0_0")+c-96);};
   std::array<float,128> first{};for(int c=0;c<128;++c)first[size_t(c)]=column(c);
   native->set(frame,60.0f);for(int i=0;i<150;++i){effect->processReplacing(effect,nullptr,outputs,128);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
   int low=0,high=0;for(int c=0;c<128;++c)if(column(c)!=first[size_t(c)])(c<96?low:high)++;
   check(low>8&&high>2,"the oscillator graph is written in 128 columns across both readout blocks");
   check(!displayOnAudio.load(),"display updates never run on the audio callback");}
  check(native->testSplitActive()&&effect->initialDelay==128,"effect chain runs on the worker thread and the 128-sample latency is reported");
  // CPU guard: an overloaded engine steals voices instead of dropping out, and recovers.
  {struct Ev6{int32_t n;intptr_t reserved;VstEvent* events[6];};VstMidiEvent notes[6]{};Ev6 chord{};
   for(int i=0;i<6;++i){notes[i].type=kVstMidiType;notes[i].byteSize=sizeof(VstMidiEvent);notes[i].midiData[0]=char(0x90);notes[i].midiData[1]=char(48+i*3);notes[i].midiData[2]=100;chord.events[i]=reinterpret_cast<VstEvent*>(&notes[i]);}chord.n=6;
   native->set(Vitaleon::Voices,3);for(int i=0;i<4;++i)effect->processReplacing(effect,nullptr,outputs,128);
   native->set(Vitaleon::Voices,8);dispatch(effProcessEvents,0,0,&chord);for(int i=0;i<20;++i)effect->processReplacing(effect,nullptr,outputs,128);
   check(native->testDynamicVoices()==8,"CPU guard leaves the voice limit alone at normal load; a raised voice count applies at once");
   native->testLoad_.store(.95f);for(int i=0;i<400;++i)effect->processReplacing(effect,nullptr,outputs,128);
   check(native->testDynamicVoices()==2,"sustained overload steps the voice limit down to one pair");
   bool guardFinite=true;for(int n=0;n<128;++n)guardFinite&=std::isfinite(left[n])&&std::isfinite(right[n]);check(guardFinite,"audio stays finite while voices are stolen");
   native->testLoad_.store(.2f);for(int i=0;i<int(44100*10/128);++i)effect->processReplacing(effect,nullptr,outputs,128);
   check(native->testDynamicVoices()==8,"the voice limit recovers after calm seconds");
   native->testLoad_.store(-1.0f);for(int i=0;i<6;++i)notes[i].midiData[0]=char(0x80);dispatch(effProcessEvents,0,0,&chord);
   {Ev6 off{};VstMidiEvent cc{};cc.type=kVstMidiType;cc.byteSize=sizeof(VstMidiEvent);cc.midiData[0]=char(0xb0);cc.midiData[1]=120;off.events[0]=reinterpret_cast<VstEvent*>(&cc);off.n=1;dispatch(effProcessEvents,0,0,&off);}
   for(int i=0;i<30;++i)effect->processReplacing(effect,nullptr,outputs,128);}
  // Idle sleep: silence parks the engine, the next note wakes it in the same callback.
  {struct Ev{int32_t n;intptr_t reserved;VstEvent* events[1];};VstMidiEvent m{};m.type=kVstMidiType;m.byteSize=sizeof(VstMidiEvent);m.midiData[1]=60;m.midiData[2]=100;Ev ev{};ev.events[0]=reinterpret_cast<VstEvent*>(&m);ev.n=1;
   m.midiData[0]=char(0xb0);m.midiData[1]=120;dispatch(effProcessEvents,0,0,&ev);effect->processReplacing(effect,nullptr,outputs,128);
   check(!native->testAsleep(),"engine is awake right after a MIDI event");
   for(int i=0;i<2400;++i)effect->processReplacing(effect,nullptr,outputs,128);
   bool silent=true;for(int n=0;n<128;++n)silent&=left[n]==0&&right[n]==0;
   check(native->testAsleep()&&silent,"engine sleeps after sustained silence with no active voice");
   m.midiData[0]=char(0x90);m.midiData[1]=60;dispatch(effProcessEvents,0,0,&ev);double energy=0;
   for(int i=0;i<20;++i){effect->processReplacing(effect,nullptr,outputs,128);if(i==0)check(!native->testAsleep(),"note-on wakes the engine in the same callback");for(int n=0;n<128;++n)energy+=double(left[n])*left[n];}
   check(energy>1e-6,"woken engine renders the note");
   m.midiData[0]=char(0x80);dispatch(effProcessEvents,0,0,&ev);}
  dispatch(effMainsChanged,0,0);dispatch(effClose);
 }
 return failures?1:0;
}
