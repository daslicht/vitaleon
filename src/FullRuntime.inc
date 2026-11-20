// SPDX-License-Identifier: GPL-3.0-or-later
inline Vitaleon::Vitaleon(){
    for(auto& key:held_)key.store(0);
    rpnMsb_.fill(127);rpnLsb_.fill(127);memberBendRange_.fill(48);
    createControls();defaultTables();syncFullControls(true,true);captureAssets();editLfo();refreshProxies();
    refreshLibrary();publishLoadedName();
    if(loadMessage_=="Ready")loadMessage_=assets_[0].size()>1?"Choose sound in MPC PRESET.":"No user sounds. Add banks to Content/Banks, then reopen plugin.";
    setLoadStatus(loadMessage_.find("Bank skipped:")==0?2:0);
    browserDisplayDirty_.store(false); // initial UI obtains its values on open
    startSound_.store(!filePrograms_.empty());
    ready_=true;service_=std::thread([this]{serviceLoop();});
    try{fxThread_=std::thread([this]{fxLoop();});}catch(...){/* stay single-threaded */}
}
inline Vitaleon::~Vitaleon(){quit_.store(true);if(service_.joinable())service_.join();
    fxWaitIdle();fxQuit_.store(true);fxSignal_.post();if(fxThread_.joinable())fxThread_.join();}
inline void Vitaleon::fxLoop(){
    for(;;){
        fxSignal_.wait();if(fxQuit_.load())return;
        const int frames=fxPending_.load(std::memory_order_acquire);if(frames<=0)continue;
        {mpc::ScopedDenormalsOff denormals;const auto start=std::chrono::steady_clock::now();
         engine_->processStageB(frames);const auto* out=engine_->output()->buffer;
         for(int i=0;i<frames;++i){const int at=(fxWrite_+i)%kFxRing;fxRing_[0][size_t(at)]=out[i][0];fxRing_[1][size_t(at)]=out[i][1];}
         fxWrite_=(fxWrite_+frames)%kFxRing;
         fxLoad_.store(float(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()*preparedRate_/frames),std::memory_order_relaxed);}
        fxPending_.store(0,std::memory_order_release);
    }
}
inline void Vitaleon::fxWaitIdle(){
    // Normally already idle. Spin briefly, then yield: the worker runs on another core.
    for(int spins=0;fxPending_.load(std::memory_order_acquire)!=0;++spins)if(spins>4000)std::this_thread::yield();
}
inline void Vitaleon::fxReset(){for(auto& channel:fxRing_)channel.fill(0.0f);fxRead_=0;fxWrite_=kFxLatency;fxLoad_.store(0);}
inline void Vitaleon::prepare(double rate,int){
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    preparedRate_=rate;engine_->setSampleRate(int(rate));
    syncFullControls(true,true);
    fxWaitIdle();
    engine_->checkOversampling();warmGraph();lastTimeline_=-1;
    fxReset();fxSplit_.store(fxWanted_.load()&&fxThread_.joinable()&&engine_->canSplit());
    // A new instance starts with the first installed sound, the one the PRESET
    // list shows as current. A project or program that arrived first wins.
    if(startSound_.exchange(false))programSelected(factoryProgramCount_);
}
inline void Vitaleon::syncFullControls(bool force,bool structural){
    for(auto& b:bindings_){if(b.structural&&!structural)continue;float value=getNormalised(b.index);
        if(force||value!=b.last){b.value->set(get(b.index));b.last=value;}}
    int count=std::min(getInt(Voices),dynamicVoices_);polyphony_->set(float(count));engine_->setVoiceLimit(count);
}
inline void Vitaleon::warmGraph(){
    // Build per-voice graph clones outside the realtime callback, including feedback.
    engine_->allSoundsOff();engine_->setVoiceLimit(32);polyphony_->set(32);
    for(int n=0;n<32;++n)engine_->noteOn(36+n,.01f,0,0);
    engine_->process(128);engine_->allSoundsOff();engine_->process(128);
    engine_->resetLfoCycles();
    engine_->setVoiceLimit(getInt(Voices));polyphony_->set(float(getInt(Voices)));
}
inline void Vitaleon::clearRoutes(){
    for(int i=0;i<64;++i){auto& r=routes_[i];if(r.connected){engine_->disconnectModulation(r.change);r.connected=false;}
        auto* c=engine_->getModulationBank().atIndex(i);c->source_name.clear();c->destination_name.clear();}
}
inline void Vitaleon::connectRoutes(){
    fxPlanDirty_.store(true);
    clearRoutes();
    engine_->disableUnnecessaryModSources();
    for(int i=0;i<64;++i){auto& r=routes_[i];int s=getInt(r.source),d=getInt(r.destination);if(s==0||d==0)continue;
        auto* c=engine_->getModulationBank().atIndex(i);c->source_name=sources_.at(s);c->destination_name=destinations_.at(d);
        auto& change=r.change;change={};change.source=sourceOutputs_.at(s);
        change.mono_destination=engine_->getMonoModulationDestination(c->destination_name);change.poly_destination=engine_->getPolyModulationDestination(c->destination_name);
        change.mono_modulation_switch=engine_->getMonoModulationSwitch(c->destination_name);change.poly_modulation_switch=engine_->getPolyModulationSwitch(c->destination_name);
        change.destination_scale=vital::Parameters::getParameterRange(c->destination_name);change.modulation_processor=c->modulation_processor.get();
        if(change.poly_destination&&change.poly_destination->router()==change.modulation_processor)throw std::runtime_error("A route cannot modulate itself");
        engine_->connectModulation(change);r.connected=true;
    }
}
inline void Vitaleon::serviceLoop(){
    while(!quit_.load()){
        if(browserPending_.load())serviceBrowser();
        if(browserDisplayDirty_.exchange(false)){
            // No state/lifecycle lock may be held: hosts may synchronously query
            // parameter strings or save state during these callbacks.
            // This is a display invalidation, not an automation gesture. In
            // particular, never echo command parameters back as automation.
            requestDisplayUpdate();
        }
        if(requested_.load()==completed_.load()){
            if(inspectCurve_.load()){
                std::lock_guard<std::mutex> life(lifecycleMutex_);std::lock_guard<std::mutex> state(stateMutex_);
                // Immutable graph/curve reads may run alongside audio reads.
                // Keep the flag set until publication finishes, for save/test synchronization.
                refreshCurveControls();refreshProxies();curveSelectionChanged_.store(false);inspectCurve_.store(false);
            }
            if(fxPlanDirty_.exchange(false))planEffectBypass();
            notifyVisuals();flushSlotNotifications();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;
        }
        std::unique_lock<std::mutex> lifecycle(lifecycleMutex_);
        parkEngine();
        if(quit_.load())break;
        unsigned revision=requested_.load();
        bool rebuild=graphRebuild_.exchange(false),succeeded=true;
        try {serviceChanges(rebuild);}
        catch(const std::exception& e){succeeded=false;std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_=std::string("Load failed: ")+e.what();setLoadStatus(2);}
        completed_.store(revision);if(rebuild&&succeeded)replayNotes_.store(true);gate_.store(3);
    }
}
// The audio thread writes the graph readouts into read-only parameters and
// counts changed pictures. This service thread, outside every lock, asks the
// host for one display update per changed picture (at most 20 a second).
// Without any refresh the MPC shows a static picture (user report, 0.3.x).
inline void Vitaleon::notifyVisuals(){
#ifdef VITALEON_GFXDIAG
    gfxReport();
#endif
    if(uiPage_<0||!getBool(visualEnabled_))return;
    auto now=std::chrono::steady_clock::now();
    if(now-lastVisual_<std::chrono::milliseconds(visualIntervalMs_.load(std::memory_order_relaxed)))return;
    const unsigned version=visualVersion_.load(std::memory_order_relaxed);if(version==visualShown_)return;
    visualShown_=version;lastVisual_=now;
#ifdef VITALEON_GFXDIAG
    requestDisplayUpdate();
    {long us=long(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-now).count());int m=gfxMode_.load();
     if(m>=0){auto& g=gfx_[size_t(m)];g.calls.fetch_add(1);g.callSumUs.fetch_add(us);if(us>g.callMaxUs.load())g.callMaxUs.store(us);}}
#else
    requestDisplayUpdate();
#endif
#ifdef VITALEON_DIAGNOSTIC
    diagReport();
#endif
}
#ifdef VITALEON_GFXDIAG
inline void Vitaleon::gfxStep(int frames){
    const auto now=std::chrono::steady_clock::now();const int page=uiPage_>=0?getInt(uiPage_):0;
    const auto& view=surfaceViews_[size_t(mpc::clamp(page,0,int(surfaceViews_.size())-1))];
    const bool hold=page!=0;
    if(hold&&!gfxHeld_){gfxHeld_=true;asleep_=false;silentFrames_=0;for(int note:{48,55,60})engine_->noteOn(note,.8f,0,0);}
    else if(!hold&&gfxHeld_){gfxHeld_=false;engine_->allSoundsOff();}
    int mode=-1;
    if(hold&&view.group==1){gfxFrames_+=frames;mode=int((gfxFrames_/long(preparedRate_*4))%4);}
    else if(hold&&page!=2)mode=4;
    static const int intervals[5]={100,66,50,33,66};
    if(mode!=gfxMode_.load()){gfxMode_.store(mode);gfxSince_=0;visualIntervalMs_.store(intervals[size_t(std::max(0,mode))]);}
    gfxSince_+=frames;
    // The previous block's figures are booked here; the first 0.5 s after a switch is skipped.
    if(mode>=0&&gfxSince_>long(preparedRate_*.5)&&gfxLastStart_!=std::chrono::steady_clock::time_point{}){
        auto& g=gfx_[size_t(mode)];long gap=long(std::chrono::duration<double,std::micro>(now-gfxLastStart_).count());
        long nominal=long(frames*1e6/preparedRate_);g.blocks.fetch_add(1);g.frames.store(frames);
        if(gap>g.maxGapUs.load())g.maxGapUs.store(gap);if(gap>nominal*3/2)g.late.fetch_add(1);
    }
    gfxLastStart_=now;
}
inline void Vitaleon::gfxReport(){
    if(uiPage_<0||getInt(uiPage_)!=2)return;
    auto now=std::chrono::steady_clock::now();if(now-gfxLastReport_<std::chrono::milliseconds(2500))return;gfxLastReport_=now;
    static const char* names[5]={"A 10 Hz","B 15 Hz","C 20 Hz","D 30 Hz","E other page"};
    const int m=gfxLine_++%5;auto& g=gfx_[size_t(m)];long blocks=std::max(1L,g.blocks.load());
    double seconds=blocks*double(std::max(1L,g.frames.load()))/preparedRate_;
    std::string text=std::string(names[m])+": blk "+std::to_string(g.sumUs.load()/blocks)+"/"+std::to_string(g.maxUs.load())+" gap "+std::to_string(g.maxGapUs.load())+" late "+std::to_string(g.late.load())+" wait "+std::to_string(g.maxWaitUs.load())
        +" n "+std::to_string(int(g.calls.load()/seconds))+"/s call "+std::to_string(g.calls.load()?g.callSumUs.load()/g.calls.load():0)+"/"+std::to_string(g.callMaxUs.load())+" t"+std::to_string(int(seconds));
    {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_=text;setLoadStatus(0);}
}
#endif
#ifdef VITALEON_DIAGNOSTIC
// CPU DIAGNOSTIC (never in a release): while HOME > ADVANCED is open the plugin
// holds one note itself and times the engine on the real device in eight
// configurations of 1.8 s each, switching parts off cumulatively:
//  a as loaded  b -reverb  c -eq  d all effects off  e -filters
//  f -osc 2/3 and sample  g -osc 1  h no note (idle engine, effects off)
inline void Vitaleon::diagApply(int config){
    for(auto& b:bindings_)b.value->set(get(b.index));
    auto off=[&](const char* key){for(auto& b:bindings_)if(b.key==key)b.value->set(0);};
    if(config>=1)off("reverb_on");
    if(config>=2)off("eq_on");
    if(config>=3)for(const char* key:{"chorus_on","compressor_on","delay_on","distortion_on","flanger_on","phaser_on","filter_fx_on"})off(key);
    if(config>=4){off("filter_1_on");off("filter_2_on");}
    if(config>=5){off("osc_2_on");off("osc_3_on");off("sample_on");}
    if(config>=6)off("osc_1_on");
    if(config==7)engine_->allSoundsOff();
    else if(config==0||config<0){}
    if(config==0)engine_->noteOn(60,.8f,0,0);
}
inline void Vitaleon::diagStep(int frames){
    bool wanted=uiPage_>=0&&getInt(uiPage_)==2;
    if(wanted&&diagConfig_<0){diagConfig_=0;diagFrames_=0;diagSum_=0;diagCount_=0;asleep_=false;silentFrames_=0;diagApply(0);}
    else if(!wanted&&diagConfig_>=0){diagConfig_=-1;for(auto& b:bindings_)b.value->set(get(b.index));engine_->allSoundsOff();}
    if(diagConfig_<0)return;
    diagFrames_+=frames;
    if(diagFrames_>long(preparedRate_*1.8)){
        if(diagCount_)diagResult_[size_t(diagConfig_)].store(int(diagSum_/diagCount_));
        diagBlock_.store(frames);diagVersion_.fetch_add(1);
        diagConfig_=(diagConfig_+1)%8;diagFrames_=0;diagSum_=0;diagCount_=0;diagApply(diagConfig_);
    }
}
inline void Vitaleon::diagReport(){
    int version=diagVersion_.load();if(version==diagShown_)return;diagShown_=version;
    int block=diagBlock_.load();double budget=block*1e6/preparedRate_;
    std::string text="b"+std::to_string(block);const char* names="abcdefgh";
    for(int i=0;i<8;++i){int us=diagResult_[size_t(i)].load();text+=" ";text+=names[i];text+=std::to_string(us)+"="+std::to_string(int(us*100/budget+.5))+"%";}
    {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_=text;setLoadStatus(0);}
}
#endif
// Decide, off the audio thread, which effects may be skipped while their mix is
// 0. Vital runs every enabled effect even when its dry/wet is 0; in the local
// library 84 of 128 enabled reverbs and 81 of 91 enabled delays sit at mix 0
// behind a macro. An effect is eligible only when EVERY route to its mix comes
// from a macro, is unipolar and maps 0 to 0. The audio thread then treats the
// effect as off while the mix parameter and those macros are 0. Vital's own
// off/on handling runs (the effect is reset when it goes off), so re-opening the
// macro starts the effect empty instead of pre-fed: the one audible difference.
inline void Vitaleon::planEffectBypass(){
    static const char* names[8]={"reverb","delay","chorus","flanger","phaser","distortion","compressor","filter_fx"};
    static const char* mixes[8]={"reverb_dry_wet","delay_dry_wet","chorus_dry_wet","flanger_dry_wet","phaser_dry_wet","distortion_mix","compressor_mix","filter_fx_mix"};
    for(int e=0;e<8;++e){auto& f=fxBypass_[size_t(e)];
        if(f.onParam<0){int on=controlIndex(std::string(names[e])+"_on"),mix=controlIndex(mixes[e]);if(on<0||mix<0)continue;
            for(auto& b:bindings_)if(b.index==on)f.onValue=b.value;if(!f.onValue)continue;f.mixParam=mix;f.onParam=on;}
        bool eligible=true;unsigned mask=0;
        for(int i=0;i<64&&eligible;++i){int s=getInt(routes_[size_t(i)].source),d=getInt(routes_[size_t(i)].destination);if(!s||!d)continue;
            if(destinations_.at(size_t(d))!=mixes[e])continue;const std::string& source=sources_.at(size_t(s));
            if(source.rfind("macro_control_",0)!=0){eligible=false;break;}
            int bipolar=controlIndex("modulation_"+std::to_string(i+1)+"_bipolar");
            float atZero=engine_->getModulationBank().atIndex(i)->modulation_processor->lineMapGenerator()->valueAtPhase(0.0f);
            if((bipolar>=0&&getBool(bipolar))||std::abs(atZero)>1e-6f)eligible=false;
            mask|=1u<<unsigned(mpc::clamp(source.back()-'1',0,3));
        }
        f.macros.store(mask);f.eligible.store(eligible);
    }
    for(int k=0;k<4;++k)macroParam_[size_t(k)]=controlIndex("macro_control_"+std::to_string(k+1));
}
inline void Vitaleon::cpuGuard(float ratio,int frames){
    cpuLoad_=.7f*cpuLoad_+.3f*ratio;guardHold_=std::max(0L,guardHold_-frames);
    // A new voice count (a loaded sound, the Voices control) applies at once; only
    // a limit the guard itself lowered recovers slowly.
    const int user=getInt(Voices);if(user!=guardUser_){guardUser_=user;dynamicVoices_=user;}
    const int active=engine_->getNumActiveVoices();
    if(cpuLoad_>.8f){
        calmFrames_=0;
        if(guardHold_==0&&active>2){dynamicVoices_=std::max(2,std::min(dynamicVoices_,active)-2);guardHold_=long(preparedRate_*.25);}
    }else if(cpuLoad_<.55f){
        calmFrames_+=frames;
        if(calmFrames_>long(preparedRate_*3)&&dynamicVoices_<user){dynamicVoices_=std::min(user,dynamicVoices_+2);calmFrames_=0;}
    }else calmFrames_=0;
}
inline void Vitaleon::parkEngine(){
    // Non-audio callers hold lifecycleMutex_. A running callback hands DSP over.
    while(!quit_.load()){
        int state=gate_.load();if(state==2)break;
        if(state==0||state==3){if(gate_.compare_exchange_strong(state,2))break;}
        else if(state==4)gate_.compare_exchange_strong(state,1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    fxWaitIdle(); // the effect worker may still be finishing the last chunk
}
inline void Vitaleon::beginStateTransaction(bool writing){lifecycleMutex_.lock();parkEngine();if(writing){startSound_.store(false);pendingPreset_.store(-1);externalPresetPending_=false;programReset_.store(false);migrateOldTables_.store(false);}else {refreshLibrary();publishLoadedName();refreshHome();} }
inline void Vitaleon::endStateTransaction(bool writing){
    // Restore remains parked until the worker has rebuilt routes and assets.
    if(writing)requestService();else gate_.store(3);
    lifecycleMutex_.unlock();
}
inline void Vitaleon::serviceChanges(bool rebuild){
    std::lock_guard<std::mutex> lock(stateMutex_);
    if(!rebuild){loadSelections();editLfo();captureAssets();refreshProxies();fxPlanDirty_.store(true);return;}
    int requestedPreset=pendingPreset_.exchange(-1);
    bool loaded=false;
    if(externalPresetPending_){
        externalPresetPending_=false;setLoadStatus(1);loadPreset(readAsset(pendingAsset_));selectedAsset_=pendingAsset_;loadedName_=pendingAsset_.name;savedAssets_["preset_name"]=loadedName_;loaded=true;
    }
    if(requestedPreset>=0){
        if(requestedPreset==0){loadMessage_="Select a preset in the list first.";setLoadStatus(2);return;}
        setLoadStatus(1);
        loadPreset(readAsset(assets_[0].at(requestedPreset)));
        lastPreset_=requestedPreset;loadedName_=assets_[0][requestedPreset].name;
        selectedAsset_=assets_[0][requestedPreset];
        savedAssets_["preset_name"]=loadedName_;loaded=true;
    }
    engine_->allSoundsOff();publish(FullMode,1);
    {
        if(programReset_.exchange(false)){
            curveEdits_.store(0);
            if(migrateOldTables_.exchange(false))migrationTables();else defaultTables();
            engine_->getSample()->init();savedAssets_.erase("sample");
            for(int i=0;i<8;++i)engine_->getLfoSource(i)->initTriangle();
            for(int i=0;i<64;++i)engine_->getModulationBank().atIndex(i)->modulation_processor->lineMapGenerator()->initLinear();
            curveSelectionChanged_.store(true);
        }
        if(!stagedState_.is_null()){restoreAssets(stagedState_);stagedState_=Json();lastPreset_=getInt(presetSelect_);lastSample_=getInt(sampleSelect_);lastShape_=getInt(lfoShapeSelect_);for(int i=0;i<3;++i)lastTable_[i]=getInt(tableSelect_[i]);}
        loadSelections();editLfo();syncFullControls(true,true);connectRoutes();captureAssets();
    }
    engine_->checkOversampling();warmGraph();refreshProxies();
    if(loaded){publishLoadedName();
        if(importNotes_.empty())loadMessage_="Loaded: "+loadedName_;
        else{loadMessage_="Loaded with changes: "+loadedName_+". "+importNotes_.front();if(importNotes_.size()>1)loadMessage_+=" (+"+std::to_string(importNotes_.size()-1)+" more)";}
        setLoadStatus(0);}
}
inline void Vitaleon::process(mpc::ProcessContext& ctx){
    // Capture held keys even while a non-audio asset transaction temporarily parks DSP.
    auto recordKeys=[&]{for(int i=0;i<ctx.numMidiEvents;++i){const auto& e=ctx.midi[i];if(e.isNoteOn())held_[e.channel()*128+e.note()].store(e.velocity());else if(e.isNoteOff())held_[e.channel()*128+e.note()].store(0);
        else if(e.isController()&&(e.controller()==120||e.controller()==123))for(int n=0;n<128;++n)held_[e.channel()*128+n].store(0);}};
    int expected=3;gate_.compare_exchange_strong(expected,0);expected=0;
    if(!gate_.compare_exchange_strong(expected,4)){recordKeys();for(int ch=0;ch<ctx.numOutputs;++ch)std::fill_n(ctx.outputs[ch],ctx.numFrames,0);return;}
    fxWaitIdle(); // from here on the engine belongs to this thread until a chunk is handed over
#ifdef __linux__
    if(!fxPriority_&&fxThread_.joinable()){fxPriority_=true;sched_param parameter{};int policy=0;
        if(pthread_getschedparam(pthread_self(),&policy,&parameter)==0)pthread_setschedparam(fxThread_.native_handle(),policy,&parameter);}
#endif
    if(resetPending_.exchange(false)){asleep_=false;silentFrames_=0;engine_->allSoundsOff();engine_->process(128);for(auto& key:held_)key.store(0);replayNotes_.store(false);controlsSynced_=false;benchmarkCount_=0;benchArmed_=resetBenchArmed_.load();}
    if(replayNotes_.exchange(false)){asleep_=false;silentFrames_=0;for(int n=0;n<2048;++n){int velocity=held_[n].load();if(velocity)engine_->noteOn(n%128,velocity/127.0f,0,n/128);}}
    // Mode changes wait for the service transaction, so no half-applied graph is audible.
    syncFullControls();
    if(diagConfig_<0)for(auto& f:fxBypass_){
        if(f.onParam<0)continue;bool want=getBool(f.onParam);
        if(want&&f.eligible.load(std::memory_order_relaxed)&&get(f.mixParam)<=1e-6f){
            unsigned mask=f.macros.load(std::memory_order_relaxed);bool zero=true;
            for(int k=0;k<4;++k)if((mask>>k&1u)&&macroParam_[size_t(k)]>=0&&getNormalised(macroParam_[size_t(k)])>1e-6f)zero=false;
            if(zero)want=false;
        }
        const float value=want?1.0f:0.0f;if(f.onValue->value()!=value)f.onValue->set(value);
    }
#ifdef VITALEON_DIAGNOSTIC
    diagStep(ctx.numFrames);
    const auto diagStart=std::chrono::steady_clock::now();
#endif
#ifdef VITALEON_GFXDIAG
    gfxStep(ctx.numFrames);gfxWaitUs_=0;
    const auto gfxStart=std::chrono::steady_clock::now();
#endif
    // Idle sleep. Vital keeps every enabled effect and global modulator running
    // on silence. Once no voice is active and the output has stayed below
    // -120 dBFS for longer than the longest delay line (4 s) plus margin, the
    // engine is skipped until the next MIDI event. Controls stay in sync.
#ifdef VITALEON_GFXDIAG
    if(gfxHeld_)asleep_=false;
#endif
    if(asleep_&&ctx.numMidiEvents==0&&engine_->getNumActiveVoices()==0&&diagConfig_<0){
        for(int ch=0;ch<ctx.numOutputs;++ch)std::fill_n(ctx.outputs[ch],ctx.numFrames,0.0f);
    }else{
        asleep_=false;
        const auto renderStart=std::chrono::steady_clock::now();
        renderBlock(ctx);
        float ratio=float(std::chrono::duration<double>(std::chrono::steady_clock::now()-renderStart).count()*preparedRate_/std::max(1,ctx.numFrames));
        if(testLoad_.load(std::memory_order_relaxed)>=0)ratio=testLoad_.load(std::memory_order_relaxed);
        if(diagConfig_<0)cpuGuard(std::max(ratio,fxLoad_.load(std::memory_order_relaxed)),ctx.numFrames);
        float peak=0;for(int i=0;i<ctx.numFrames;++i)peak=std::max(peak,std::max(std::abs(ctx.outputs[0][i]),std::abs(ctx.outputs[1][i])));
        if(peak<1e-6f&&ctx.numMidiEvents==0&&engine_->getNumActiveVoices()==0)silentFrames_+=ctx.numFrames;else silentFrames_=0;
        if(silentFrames_>int(preparedRate_*6)){asleep_=true;lastTimeline_=-1;}
    }
#ifdef VITALEON_DIAGNOSTIC
    if(diagConfig_>=0&&diagFrames_>long(preparedRate_*.6)){diagSum_+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-diagStart).count();++diagCount_;}
#endif
    updateDisplays(ctx.numFrames);
#ifdef VITALEON_GFXDIAG
    {int m=gfxMode_.load();if(m>=0&&gfxSince_>long(preparedRate_*.5)){auto& g=gfx_[size_t(m)];long us=long(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-gfxStart).count());
        g.sumUs.fetch_add(us);if(us>g.maxUs.load())g.maxUs.store(us);if(gfxWaitUs_>g.maxWaitUs.load())g.maxWaitUs.store(gfxWaitUs_);}}
#endif
    recordKeys();refreshProxies();expected=4;if(!gate_.compare_exchange_strong(expected,0)&&expected==1)gate_.store(2);
}
inline void Vitaleon::renderBlock(mpc::ProcessContext& ctx){
    engine_->setStatusUpdatesEnabled(getBool(visualEnabled_));
    if(ctx.transport.tempoValid&&ctx.transport.bpm>0)engine_->setBpm(float(ctx.transport.bpm));
    if(ctx.transport.ppqValid&&ctx.transport.bpm>0){double t=ctx.transport.ppqPos*60/ctx.transport.bpm;
        if(lastTimeline_<0||std::abs(t-lastTimeline_)>2*ctx.numFrames/ctx.sampleRate)engine_->correctToTime(t);
        lastTimeline_=t+(ctx.transport.playing?ctx.numFrames/ctx.sampleRate:0);}
    int event=0;
    for(int offset=0;offset<ctx.numFrames;){
        while(event<ctx.numMidiEvents&&ctx.midi[event].sampleOffset<=offset)handleFullMidi(ctx.midi[event++]);
        int frames=std::min(128,ctx.numFrames-offset);if(event<ctx.numMidiEvents)frames=std::min(frames,ctx.midi[event].sampleOffset-offset);
        if(fxSplit_.load(std::memory_order_relaxed)){
            engine_->processStageA(frames);   // voices; the worker may still render the previous chunk's effects
#ifdef VITALEON_GFXDIAG
            {auto t=std::chrono::steady_clock::now();fxWaitIdle();gfxWaitUs_+=long(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count());}
#else
            fxWaitIdle();
#endif
            engine_->handoff(frames);
            for(int i=0;i<frames;++i){const int at=(fxRead_+i)%kFxRing;ctx.outputs[0][offset+i]=fxRing_[0][size_t(at)];ctx.outputs[1][offset+i]=fxRing_[1][size_t(at)];}
            fxRead_=(fxRead_+frames)%kFxRing;
            fxPending_.store(frames,std::memory_order_release);fxSignal_.post();
        }else{
            engine_->process(frames);const auto* out=engine_->output()->buffer;
            for(int i=0;i<frames;++i){ctx.outputs[0][offset+i]=out[i][0];ctx.outputs[1][offset+i]=out[i][1];}
        }
        offset+=frames;
    }
}
inline void Vitaleon::handleFullMidi(const mpc::MidiEvent& e){
    int ch=e.channel();bool mpe=getBool(mpeEnabled_);int lower=getInt(mpeLower_),upper=getInt(mpeUpper_);
    bool master=mpe&&((ch==0&&lower>0)||(ch==15&&upper>0));int from=ch==0?1:15-upper,to=ch==0?lower:14;
    if(e.isNoteOn())engine_->noteOn(e.note(),e.velocity()/127.0f,0,ch);
    else if(e.isNoteOff())engine_->noteOff(e.note(),e.velocity()/127.0f,0,ch);
    else if(e.isPitchBend()){float value=e.pitchBend()/8192.0f;
        if(master){engine_->setZonedPitchWheel(value,ch,ch);engine_->setZonedPitchWheel(value,from,to);}
        else if(mpe)engine_->setPitchWheel(value*memberBendRange_[ch]/48,ch);
        else engine_->setZonedPitchWheel(value,ch,ch);
    }
    else if(e.isAftertouch()){if(master)engine_->setChannelRangeAftertouch(from,to,e.data[1]/127.0f,0);else engine_->setChannelAftertouch(ch,e.data[1]/127.0f,0);}
    if((e.data[0]&0xf0)==0xa0)engine_->setAftertouch(float(e.data[1]),e.data[2]/127.0f,0,ch);
    if(e.isController()){
        int cc=e.controller(),value=e.controllerValue();
        if(cc==101)rpnMsb_[ch]=value;if(cc==100)rpnLsb_[ch]=value;
        if(cc==6&&rpnMsb_[ch]==0){if(rpnLsb_[ch]==0)memberBendRange_[ch]=float(value);else if(rpnLsb_[ch]==6){if(ch==0)publish(mpeLower_,float(std::min(value,15)));if(ch==15)publish(mpeUpper_,float(std::min(value,15)));}}
        if(cc==120){fxWaitIdle();engine_->allSoundsOff();}else if(cc==123)engine_->allNotesOffRange(0,ch,ch);
        else if(cc==1){if(master)engine_->setModWheelAllChannels(value/127.0f);else engine_->setModWheel(value/127.0f,ch);}
        else if(cc==74){if(master)engine_->setChannelRangeSlide(from,to,value/127.0f,0);else engine_->setChannelSlide(ch,value/127.0f,0);}
        else if(cc==64){if(master){if(value>=64)engine_->sustainOnRange(from,to);else engine_->sustainOffRange(0,from,to);}else{if(value>=64)engine_->sustainOn(ch);else engine_->sustainOff(0,ch);}}
        else if(cc==66){if(master){if(value>=64)engine_->sostenutoOnRange(from,to);else engine_->sostenutoOffRange(0,from,to);}else{if(value>=64)engine_->sostenutoOn(ch);else engine_->sostenutoOff(0,ch);}}
    }
}
inline void Vitaleon::updateDisplays(int frames){
    displayCountdown_-=frames;if(displayCountdown_>0||!getBool(visualEnabled_))return;displayCountdown_=int(preparedRate_/30);
    // Only a changed value of the graph that is on screen asks for a display update.
    bool changed=false;
    auto put=[&](int index,float value){if(getNormalised(index)!=value){publishNormalised(index,value);changed=true;}};
    // The control rate is 30 ticks a second at most.
    const auto& visible=surfaceViews_[size_t(mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1))];
    const bool curveVisible=visible.group==4||(visible.group==6&&visible.member==1);
    // The Force tab bar is native: pressing a tab writes no parameter, so uiPage_
    // can lag behind the screen (CHANGELOG 0.3.32). Writing the columns of the page
    // the plugin believes visible alone left OSC 1 blank on its first visit and the
    // OSC 2/3 graphs zeroed until their page was left and entered again. So the
    // visible oscillator keeps the full rate and one further oscillator follows in
    // rotation: every graph is current within about a tenth of a second, whichever
    // tab the host shows and whichever table is selected.
    int plot[2]={-1,-1};
    if(visible.group==1&&visible.member>=1&&visible.member<=3)plot[0]=visible.member-1;
    graphTurn_=(graphTurn_+1)%3;
    if(plot[0]!=graphTurn_)plot[1]=graphTurn_;
    for(int o=0;o<3;++o){auto* t=engine_->getWavetable(o);float position=osc_[o].position->value();
        if(waveMonoSwitch_[o]->value()!=0)position=waveMono_[o]->buffer[0][0];
        if(wavePolySwitch_[o]->value()!=0&&engine_->getNumActiveVoices())position+=wavePoly_[o]->buffer[0][0];
        position=mpc::clamp(position,0.0f,float(t->numFrames()-1));int a=int(position),b=std::min(a+1,t->numFrames()-1);float blend=position-a;
        const float* wa=t->getBuffer(a);const float* wb=t->getBuffer(b);publishNormalised(wavePosition_[o],position/std::max(1,t->numFrames()-1));
        if(o!=plot[0]&&o!=plot[1])continue;
        for(int i=0;i<kWaveColumns;++i){int n=(i*(2048/kWaveColumns)+2048/kWaveColumns/2)%2048;float v=.5f+.5f*(wa[n]+blend*(wb[n]-wa[n]));
            put(waveColumn(o,i),std::round(mpc::clamp(v,0.0f,1.0f)*(kWaveLevels-1))/(kWaveLevels-1));}
    }
    auto* curve=selectedCurve();
    if(curveVisible)for(int i=0;i<kCurveColumns;++i)put(curveColumn(i),std::round(mpc::clamp(curve->valueAtPhase((i+.5f)/kCurveColumns),0.0f,1.0f)*(kCurveLevels-1))/(kCurveLevels-1));
    auto status=[](const vital::StatusOutput* p){float v=p?p->value()[0]:0;return std::isfinite(v)&&v!=vital::StatusOutput::kClearValue?mpc::clamp(v,0.0f,1.0f):0;};
    publishNormalised(lfoReadout_,status(lfoStatus_[getInt(lfoSelect_)-1]));
    {float value=status(envStatus_[getInt(envSelect_)-1]);if(visible.group==3)put(envReadout_,value);else publishNormalised(envReadout_,value);}
    const auto* phaseStatus=lfoPhase_[getInt(lfoSelect_)-1];float phase=0;
    if(phaseStatus&&phaseStatus->value()[0]!=vital::StatusOutput::kClearValue)phase=vital::utils::decodePhaseAndVoice(phaseStatus->value()).first[0];
    // Vital does not run an LFO that no route uses, and its status then keeps a
    // stale value. Park the playhead at the start instead of a random position.
    {int source=lfoSource_[size_t(getInt(lfoSelect_)-1)];bool used=false;for(const auto& route:routes_)used|=route.connected&&getInt(route.source)==source;if(!used)phase=0;}
    {float value=std::isfinite(phase)?mpc::clamp(phase,0.0f,1.0f):0;if(curveVisible)put(lfoPhaseReadout_,value);else publishNormalised(lfoPhaseReadout_,value);}
    const auto& r=routes_[getInt(routeSelect_)-1];float v=r.connected?r.change.modulation_processor->output(vital::ModulationConnectionProcessor::kModulationPreScale)->buffer[0][0]:0;
    publishNormalised(modReadout_,mpc::clamp(.5f+.5f*v,0.0f,1.0f));
    if(changed)visualVersion_.fetch_add(1,std::memory_order_relaxed);
}
inline void Vitaleon::saveExtraState(std::vector<uint8_t>& out){
    std::lock_guard<std::mutex> lock(stateMutex_);auto c=std::atomic_load(&activeCatalog_);Json state={{"vitaleon_format",1},{"assets",savedAssets_},{"loaded_name",loadedName_},{"load_message",loadMessage_},{"bank_path",c?c->path:""},{"sound_path",selectedAsset_.path},{"sound_entry",selectedAsset_.entry}};
    auto text=state.dump();out.assign(text.begin(),text.end());
}
inline void Vitaleon::loadExtraState(const uint8_t* data,size_t size){
    if(size>128*1024*1024)return;
    try {Json state=Json::parse(data,data+size);if(state.value("vitaleon_format",0)!=1)return;
        auto catalog=initialCatalog_;std::string bankPath=state.value("bank_path",std::string());
        if(!bankPath.empty()){try{std::error_code ec;catalog=std::filesystem::is_directory(bankPath,ec)?indexFolder(bankPath):indexBank(bankPath);}catch(const std::exception&){/* Embedded audio state still restores without its drive. */}}
        {std::lock_guard<std::mutex> lock(stateMutex_);stagedState_=state.at("assets");loadedName_=state.value("loaded_name",stagedState_.value("preset_name",std::string("Saved project sound")));loadMessage_=state.value("load_message",std::string("Restored: ")+loadedName_);selectedAsset_.path=state.value("sound_path",std::string());selectedAsset_.entry=state.value("sound_entry",std::string());publishLoadedName();}
        installCatalog(catalog,true);{std::lock_guard<std::mutex> browser(browserMutex_);browser_.status=state.value("load_message",std::string("Project sound restored."));}}
    catch(const std::exception& e){std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_=e.what();setLoadStatus(2);}
    requestService();
}
inline void Vitaleon::stateRestored(uint32_t count,bool){
    if(count<=Count||!getBool(FullMode)){
        migrateOldControls();std::lock_guard<std::mutex> state(stateMutex_);stagedState_=Json();
        loadedName_="Converted project";publishLoadedName();loadMessage_="Previous project converted.";setLoadStatus(0);
    }
    refreshLibrary();requestService();
    {std::lock_guard<std::mutex> nav(navigationMutex_);updateNavigation();}
}
inline void Vitaleon::programSelected(int index){
    if(index>=factoryProgramCount_&&index<factoryProgramCount_+int(filePrograms_.size())){
        int file=filePrograms_[index-factoryProgramCount_];
        {std::lock_guard<std::mutex> state(stateMutex_);stagedState_=Json();}
        publish(presetSelect_,float(file));
        for(int bank=1;bank<int(libraryBanks_.size());++bank)if(libraryBanks_[bank].name==assets_[0][file].bank){publish(libraryBank_,float(bank));break;}
        publish(libraryPage_,0);refreshLibrary();
        pendingPreset_.store(file);
        requestService();browserDisplayDirty_.store(true);
        return;
    }
    selectInit();
}
// Every control already holds its Init value; reset tables, sample and curves.
inline void Vitaleon::selectInit(){
    {std::lock_guard<std::mutex> state(stateMutex_);stagedState_=Json();selectedAsset_={};loadedName_="Init";publishLoadedName();loadMessage_="Init sound.";setLoadStatus(0);}
    programReset_.store(true);
    publish(FullMode,1);
    requestService();
}
