// SPDX-License-Identifier: GPL-3.0-or-later
inline void Vitaleon::createControls(){
    // Preserve the historical ID/index for project compatibility; no mode switch.
    auto engineMode=mpc::boolParam("full_engine","Full Engine",true);engineMode.automatable=false;addParam(engineMode);
    const Json catalog=Json::parse(kControlCatalog);
    const auto controls=engine_->getControls();
    for(const auto& c:catalog["controls"]){
        const std::string key=c["id"],id="v_"+key;
        if(key=="polyphony"||key=="beats_per_minute")continue;
        std::string name=key;
        for(auto& ch:name)if(ch=='_')ch=' ';
        float lo=c["min"],hi=c["max"],def=c["default"];
        mpc::Param p=mpc::floatParam(id.c_str(),name.c_str(),lo,hi,def);
        // Vital declares the envelope times (scale 4 = kQuartic) with a quartic display
        // curve. Mirroring it on the host parameter puts the short times under the
        // first part of the travel instead of the first four percent.
        if(c["scale"].get<int>()==4)p.skew=0.25f;
        if(c["scale"].get<int>()==vital::ValueDetails::kIndexed){
            p=mpc::intParam(id.c_str(),name.c_str(),int(lo),int(hi),int(def));
            if(lo==0&&hi==1)p=mpc::boolParam(id.c_str(),name.c_str(),def!=0);
            else if(c.count("choices")&&lo==0)p=mpc::choiceParam(id.c_str(),name.c_str(),c["choices"].get<std::vector<std::string>>(),int(def));
        }
        const int index=addParam(p);indices_[key]=index;
        bindings_.push_back({index,controls.at(key),key,-100,key=="oversampling"});
    }
    sources_.push_back("None");destinations_.push_back("None");
    for(const auto& name:catalog["sources"])sources_.push_back(name.get<std::string>());
    for(const auto& name:catalog["destinations"])destinations_.push_back(name.get<std::string>());
    sourceOutputs_.push_back(nullptr);
    for(size_t i=1;i<sources_.size();++i)sourceOutputs_.push_back(engine_->getModulationSource(sources_[i]));
    for(int i=0;i<64;++i){
        auto pre="route_"+std::to_string(i+1);auto name="Route "+std::to_string(i+1);
        routes_[i].source=addParam(mpc::choiceParam((pre+"_source").c_str(),(name+" Source").c_str(),sources_));
        routes_[i].destination=addParam(mpc::choiceParam((pre+"_destination").c_str(),(name+" Target").c_str(),destinations_));
    }
    scanAssets();
    auto selector=[&](const char* id,const char* name,const std::vector<Asset>& files,int range=-1){
        // File selection is session-local; project state embeds the actual assets.
        // A table selector keeps a wide range: RELOAD may add files while it runs.
        auto p=mpc::intParam(id,name,0,range>=0?range:int(files.size())-1,0);p.automatable=false;return addParam(p);
    };
    presetSelect_=selector("preset_select","Preset",assets_[0]);
    // The three table slots stay in the parameter list for index compatibility:
    // projects and automation saved with the first table-picker build refer to
    // them, and removing them would shift every later parameter's index.
    for(int i=0;i<3;++i){std::string id="table_"+std::to_string(i+1);tableSelect_[i]=selector(id.c_str(),("Osc "+std::to_string(i+1)+" Table").c_str(),assets_[1],4095);}
    sampleSelect_=selector("sample_select","Sample File",assets_[2]);
    lfoShapeSelect_=selector("lfo_file","LFO File",assets_[3]);
    loadStatus_=addParam(mpc::intParam("load_status","Load Status",0,2,0));
    visualEnabled_=addParam(mpc::boolParam("visuals","Live Graphics",true));
    createBanks();createReadouts();
    advancedTargets_.reserve(bindings_.size());std::vector<std::string> names;
    for(const auto& b:bindings_){advancedTargets_.push_back(b.index);names.push_back(b.key);}
    advancedSelector_=addParam(mpc::choiceParam("advanced_parameter","Parameter",names));
    advancedValue_=addParam(mpc::floatParam("advanced_value","Value",0,1,0));
    curveTarget_=addParam(mpc::choiceParam("curve_target","Curve Target",{"Selected LFO","Selected Route"}));
    effectSlot_=addParam(mpc::intParam("effect_slot","FX Position",1,9,1));
    effectType_=addParam(mpc::choiceParam("effect_type","FX At Position",{"Chorus","Compressor","Delay","Distortion","EQ","Filter","Flanger","Phaser","Reverb"}));
    effectOrder_=controlIndex("effect_chain_order");mpeEnabled_=controlIndex("mpe_enabled");
    mpeLower_=addParam(mpc::intParam("mpe_lower","Lower MPE Members",0,15,15));mpeUpper_=addParam(mpc::intParam("mpe_upper","Upper MPE Members",0,15,0));
    for(int o=0;o<3;++o){waveBankReadout_[o]=numParams();for(int i=0;i<96;++i)addReadout("display_wave_bank_"+std::to_string(o)+"_"+std::to_string(i));}
    curveBankReadout_=numParams();for(int i=0;i<96;++i)addReadout("display_lfo_bank_"+std::to_string(i));
    lfoPhaseReadout_=addReadout("display_lfo_phase");
    loadText_=numParams();for(int i=0;i<4;++i)addReadout("display_load_text_"+std::to_string(i));
    createLibrary();
    // Append-only overview proxies target the full engine. Attack/release
    // always edit the amplitude envelope, independently of the envelope editor.
    homeControls_=numParams();
    const char* homeKeys[]={"volume","filter_1_cutoff","filter_1_resonance","env_1_attack","env_1_release","osc_1_unison_voices","osc_1_unison_detune"};
    const char* homeNames[]={"Master","Cutoff","Resonance","Attack","Release","Unison","Detune"};
    for(int i=0;i<7;++i){homeFullTargets_[i]=controlIndex(homeKeys[i]);auto id="home_"+std::to_string(i);auto name="Home "+std::string(homeNames[i]);addParam(mpc::floatParam(id.c_str(),name.c_str(),0,1,0));}
    homeFilter_=addParam(mpc::boolParam("home_filter_on","Home Filter",true));
    // The PRESET list holds the installed sounds and nothing else; the first one
    // is loaded when the plugin starts (see prepare). Only a plugin without any
    // installed sound gets one "Init" program, so that it can still be played.
    for(int i=0;i<numParams();++i)initValues_.push_back(param(i).def);
    initValues_[FullMode]=1;initValues_[Voices]=8;
    initValues_[size_t(controlIndex("voice_override"))]=vital::VoiceHandler::kSteal;
    initValues_[size_t(controlIndex("volume"))]=3136;
    if(assets_[0].size()<=1)addProgram("Init",initValues_);
    createFilePrograms();
    createStorage();createTables();createNavigation();createTableStep();createGroupSections();
    // Appended after every other parameter (BUILDING.md: new parameters go last), so
    // no existing index moves. The LIBRARY page binds it by name to its dice button;
    // one tap deals the next sound of the random deck.
    {auto p=mpc::boolParam("library_random","Random Preset",false);p.automatable=false;libraryRandom_=addParam(p);}
    // Press feedback for the dice, appended last for the same reason. A display-only
    // readout: the host picks the button's filmstrip frame from its value (0 dark,
    // 0.5 pressed), and because it is not the command handle, the next tap still
    // arrives while the first one is lit.
    libraryRandomFlash_=addReadout("library_random_flash");
    // MIDI learn for the dice, appended last like every other new parameter. The int
    // keeps the learned note (-1 = off) and therefore travels with the project chunk;
    // the bool is the one-shot LEARN command (value 1 arms, the handle returns to 0).
    {auto p=mpc::intParam("library_random_note","Random MIDI Note",-1,127,-1);p.automatable=false;libraryRandomNote_=addParam(p);}
    {auto p=mpc::boolParam("library_random_learn","Random MIDI Learn",false);p.automatable=false;libraryRandomLearn_=addParam(p);}
    // Favourites, appended last again. Each heart binds its row parameter directly, so
    // the host itself paints filled or outline from the value; the plugin owns the
    // truth and republishes it whenever the row shows another preset.
    libraryFavouriteRow_=numParams();
    for(int i=0;i<6;++i){auto p=mpc::boolParam(("library_favourite_row_"+std::to_string(i)).c_str(),("Favourite Row "+std::to_string(i+1)).c_str(),false);p.automatable=false;addParam(p);}
    {auto p=mpc::intParam("library_favourite_note","Favourite MIDI Note",-1,127,-1);p.automatable=false;libraryFavouriteNote_=addParam(p);}
    {auto p=mpc::boolParam("library_favourite_learn","Favourite MIDI Learn",false);p.automatable=false;libraryFavouriteLearn_=addParam(p);}
    // The favourites-only view, appended last like every other new parameter. Lit means
    // the LIST shows nothing but presets that carry a heart.
    {auto p=mpc::boolParam("library_favourites_only","Favourites Only",false);p.automatable=false;libraryFavouritesOnly_=addParam(p);}
    // One display readout per heart. The button on top is a one-shot command that returns
    // to 0, so the heart no longer depends on a value the host owns and can reset.
    libraryFavouriteState_=numParams();for(int i=0;i<6;++i)addReadout("library_favourite_state_"+std::to_string(i));
    libraryLoadedFavourite_=addReadout("library_loaded_favourite");
    // The favourites list's wheel, appended last like every other new parameter. It is a
    // discrete step handle (the skin gives it kFavouriteSteps replacementStrings), so one
    // click is one favourite and the page follows the cursor like the wavetable picker.
    // The name carries no "Library " prefix: the skin must label it with fixed strings.
    {auto p=mpc::intParam("library_favourite_step","Favourite Step",0,kFavouriteSteps-1,0);p.automatable=false;libraryFavouriteStep_=addParam(p);}
    // One readout per row: 0 empty, 0.5 a favourite, 1 the row the wheel points at. The
    // host paints the row frame from it, so no host repaint can lose the marker.
    libraryFavouriteRowState_=numParams();for(int i=0;i<6;++i)addReadout("library_favourite_row_state_"+std::to_string(i));
    // Row select and wheel push, appended last again. A tap on a row moves the list cursor
    // (no load, so a stray tap never replaces the sounding preset); the push is its own
    // command because the host's hardware focus stays on the tapped row while the wheel
    // moves the cursor, so 'Open Row N' from the push acted on the wrong entry.
    libraryRowSelect_=numParams();
    for(int i=0;i<6;++i){auto p=mpc::boolParam(("library_row_select_"+std::to_string(i)).c_str(),("Row Select "+std::to_string(i+1)).c_str(),false);p.automatable=false;addParam(p);}
    {auto p=mpc::boolParam("library_row_enter","Row Enter",false);p.automatable=false;libraryRowEnter_=addParam(p);}
    // The learned MIDI keys are settings of the plugin, not of the project (user report: a
    // new instance started with MIDI off). Read them once both parameters exist, so every
    // instance starts with the same remote controls.
    loadMidiLearn();applyMidiLearn();
}

inline int Vitaleon::addProxyBank(const std::string& title,const std::string& prefix,int count,const std::vector<std::string>& suffixes){
    ProxyBank bank;bank.selector=addParam(mpc::intParam(("edit_"+prefix).c_str(),(title+" Select").c_str(),1,count,1));
    for(int n=1;n<=count;++n){std::vector<int> row;for(const auto& s:suffixes)row.push_back(controlIndex(prefix+"_"+std::to_string(n)+"_"+s));bank.targets.push_back(row);}
    for(size_t i=0;i<suffixes.size();++i){
        mpc::Param p=param(bank.targets[0][i]);p.id="edit_"+prefix+"_"+suffixes[i];p.name=title+" "+suffixes[i];bank.proxies.push_back(addParam(p));
    }
    const int selector=bank.selector;banks_.push_back(std::move(bank));return selector;
}
inline void Vitaleon::createBanks(){
    envSelect_=addProxyBank("Env","env",6,{"delay","attack","hold","decay","sustain","release","attack_power","decay_power","release_power"});
    lfoSelect_=addProxyBank("LFO","lfo",8,{"frequency","sync","tempo","sync_type","delay_time","fade_time","phase","stereo","smooth_mode","smooth_time","keytrack_transpose","keytrack_tune"});
    addProxyBank("Random","random",4,{"frequency","sync","tempo","style","stereo","sync_type","keytrack_transpose","keytrack_tune"});
    ProxyBank bank;bank.selector=routeSelect_=addParam(mpc::intParam("edit_route","Route Select",1,64,1));
    for(int i=0;i<64;++i){auto pre="modulation_"+std::to_string(i+1)+"_";bank.targets.push_back({routes_[i].source,routes_[i].destination,controlIndex(pre+"amount"),controlIndex(pre+"bipolar"),controlIndex(pre+"stereo"),controlIndex(pre+"bypass"),controlIndex(pre+"power")});}
    const char* names[]={"Source","Target","Amount","Bipolar","Stereo","Bypass","Curve"};
    for(int i=0;i<7;++i){auto p=param(bank.targets[0][i]);p.id="edit_route_"+std::to_string(i);p.name=names[i];bank.proxies.push_back(addParam(p));}
    routeTargetProxy_=bank.proxies[1];
    banks_.push_back(std::move(bank));
    lfoShape_=addParam(mpc::choiceParam("lfo_shape","LFO Shape",{"Custom","Triangle","Sine","Saw Up","Saw Down","Square"}));
    lfoSmooth_=addParam(mpc::boolParam("lfo_curve_smooth","Smooth Curve",false));
    pointSelect_=addParam(mpc::intParam("lfo_point","Point",1,100,1));
    pointCount_=addParam(mpc::intParam("lfo_points","Point Count",2,100,3));
    pointX_=addParam(mpc::floatParam("lfo_point_x","Point Time",0,1,0));
    pointY_=addParam(mpc::floatParam("lfo_point_y","Point Level",0,1,0));
    pointCurve_=addParam(mpc::floatParam("lfo_point_power","Point Curve",-20,20,0));
}
inline int Vitaleon::addReadout(const std::string& id){auto p=mpc::floatParam(id.c_str(),id.c_str(),0,1,0);p.automatable=false;return addParam(p);}
inline void Vitaleon::createReadouts(){
    for(int o=0;o<3;++o){wavePosition_[o]=addReadout("display_position_"+std::to_string(o));waveReadout_[o]=numParams();for(int i=0;i<96;++i)addReadout("display_wave_"+std::to_string(o)+"_"+std::to_string(i));}
    curveReadout_=numParams();for(int i=0;i<96;++i)addReadout("display_lfo_curve_"+std::to_string(i));
    lfoReadout_=addReadout("display_lfo_value");modReadout_=addReadout("display_mod_value");envReadout_=addReadout("display_env_value");
    const auto& mono=engine_->getMonoModulations();const auto& poly=engine_->getPolyModulations();
    for(int i=0;i<3;++i){auto key="osc_"+std::to_string(i+1)+"_wave_frame";waveMono_[i]=mono.at(key);wavePoly_[i]=poly.at(key);waveMonoSwitch_[i]=engine_->getMonoModulationSwitch(key);wavePolySwitch_[i]=engine_->getPolyModulationSwitch(key);
        // The live frame position the graphs read: the engine control of that key.
        for(const auto& b:bindings_)if(b.key==key)wavePositionValue_[i]=b.value;}
    for(int i=0;i<8;++i){auto key="lfo_"+std::to_string(i+1);lfoSource_[i]=int(std::find(sources_.begin(),sources_.end(),key)-sources_.begin());lfoStatus_[i]=engine_->getStatusOutput(key);lfoPhase_[i]=engine_->getStatusOutput(key+"_phase");}
    for(int i=0;i<6;++i)envStatus_[i]=engine_->getStatusOutput("env_"+std::to_string(i+1));
}
inline void Vitaleon::refreshProxies(){
    refreshHome();
    for(const auto& bank:banks_){int sel=mpc::clamp(getInt(bank.selector)-1,0,int(bank.targets.size())-1);
        for(size_t i=0;i<bank.proxies.size();++i)publishNormalised(bank.proxies[i],getNormalised(bank.targets[sel][i]));}
    if(advancedSelector_>=0)publishNormalised(advancedValue_,getNormalised(advancedTargets_[getInt(advancedSelector_)]));
    if(effectSlot_>=0){int order[9];vital::utils::decodeFloatToOrder(order,get(effectOrder_),9);publish(effectType_,float(order[getInt(effectSlot_)-1]));}
    refreshNavigation();
}
inline void Vitaleon::publishList(int index,float plain){
    // A selector written by the plugin itself (preset load, library browsing) must
    // move its cache too; publishNormalised alone sends no parameterChanged.
    publishNormalised(index,param(index).toNormalised(plain));
    // Tables have their own view now, so only the file selectors keep a cache.
    const int slot=index==presetSelect_?3:index==sampleSelect_?4:index==lfoShapeSelect_?5:-1;
    if(slot<0)return;
    listIndex_[slot]=int(std::lround(plain));listRaw_[slot]=getNormalised(index);listMs_[slot]=0;listTravel_[slot]=0;
}
inline void Vitaleon::listStep(int index,float plain){
    // The lists of the file selectors are filled at run time, so a skin cannot name
    // their entries and the host turns them as continuous parameters: it moves 1/100
    // of the range per wheel click and ignores the parameter's own steps (host
    // contract; discrete steps come only from replacementStrings). With 33 tables a
    // click therefore moved 0.01 while one table is 1/32 of the range - two clicks
    // for the first table, three for every later one. A click is one entry now, a
    // drag or swipe keeps following the position, and a jump stays absolute.
    const int slot=index==presetSelect_?3:index==sampleSelect_?4:index==lfoShapeSelect_?5:-1;
    if(slot<0)return;
    const int last=std::max(0,int(param(index).max));
    const float raw=getNormalised(index),delta=raw-listRaw_[slot];
    const long long now=nowMs();
    // A wheel click arrives alone; a drag or a swipe reports a stream of changes.
    const bool click=now-listMs_[slot]>=25;
    listRaw_[slot]=raw;listMs_[slot]=now;
    if(last==0)return;
    const float step=1.0f/float(last);
    // The host reports a click twice and its second write had replaced the stored
    // entry with its own fraction: put the entry back.
    if(std::fabs(delta)<step*.01f){publishNormalised(index,float(listIndex_[slot])*step);listTravel_[slot]=0;return;}
    int next=listIndex_[slot];
    if(click&&std::fabs(delta)>=0.25f){next=int(std::lround(plain));listTravel_[slot]=0;}       // preset load, list change
    else if(click&&std::fabs(delta)<step*1.5f){next+=(delta>0?1:-1);listTravel_[slot]=0;}       // one click, one entry
    else{
        // Follow the travel instead of stepping once per report: a drag moves the list
        // with the finger and never one entry per touch event. Reading the direction
        // from the travel also keeps reverse turns from ever running forward.
        listTravel_[slot]+=delta;
        const int moved=int(listTravel_[slot]/step);
        if(moved){next+=moved;listTravel_[slot]-=float(moved)*step;}
    }
    listIndex_[slot]=next=mpc::clamp(next,0,last);
    publishNormalised(index,float(next)*step);
    // The host draws the value it was told last; without this it showed the entry
    // only on the following click.
    notifyHostParameterChanged(index);
}
inline void Vitaleon::parameterChanged(int index,float plain){
    if(!ready_)return;
    listStep(index,plain);
    fxPlanDirty_.store(true,std::memory_order_relaxed);
    if(navigationChanged(index,plain)||tableChanged(index,plain)||storageChanged(index))return;
    if(index==FullMode){publish(FullMode,1);return;}
    // The audio thread refreshes proxy readouts. Use the host callback's value,
    // never reread a proxy that may already have been refreshed to its old target.
    if(index>=homeControls_&&index<homeControls_+7){setNormalised(homeTarget(index-homeControls_),plain);return;}
    if(index==homeFilter_){set(controlIndex("filter_1_on"),plain);return;}
    if(libraryChanged(index,plain))return;
    for(const auto& b:banks_)for(size_t i=0;i<b.proxies.size();++i)if(index==b.proxies[i]){
        int sel=mpc::clamp(getInt(b.selector)-1,0,int(b.targets.size())-1);set(b.targets[sel][i],plain);
        // Target picker: choosing an entry (or NO TARGET) returns to the matrix page of that route.
        if(index==routeTargetProxy_&&uiPage_>=0&&(getInt(uiPage_)==pickTargetView_||getInt(uiPage_)==pickGroupView_))navigateTo(matrixView_,(getInt(routeSelect_)-1)/8);
        return;}
    if(index==advancedValue_){setNormalised(advancedTargets_[getInt(advancedSelector_)],plain);return;}
    if(index==effectType_){int order[9];vital::utils::decodeFloatToOrder(order,get(effectOrder_),9);int position=getInt(effectSlot_)-1,target=int(std::lround(plain));for(int i=0;i<9;++i)if(order[i]==target){std::swap(order[i],order[position]);break;}set(effectOrder_,vital::utils::encodeOrderToFloat(order,9));return;}
    // A section button the host wrote without a callback is still the section on
    // screen; sync it before the proxies fill the visible slots.
    syncSectionFromStrip();
    refreshProxies();
    // A Track QControl writes the sound parameter directly. The screen knob and a
    // Screen-mode Q-Link are separate proxy parameters, so mirror the change into
    // the visible ones and let the service thread announce them while it turns.
    if(!writingSlot_&&param(index).automatable){
        int proxy=-1,group=-1;
        for(int i=0;i<16;++i)if(uiTargets_[i].load()==index){proxy=uiKnobs_+i;break;}
        const int page=uiPage_>=0?mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1):-1;
        if(page>=0){const int g=mpc::clamp(surfaceViews_[page].group,0,6);const int bank=g==6?4:g;
            if(groupSlot_[bank]>=0)for(int i=0;i<9;++i)if(groupTargets_[size_t(bank)][size_t(i)].load()==index){group=groupSlot_[bank]+i;break;}}
        if(proxy>=0||group>=0)noteLive(proxy,-1,group);
    }
    if(index==curveTarget_||index==routeSelect_||index==pointSelect_||index==lfoSelect_){
        // Selecting a view is read-only: it must never stop/retrigger held notes.
        curveSelectionChanged_.store(true);inspectCurve_.store(true);return;
    }
    unsigned curveEdit=index==lfoShape_?1:index==pointCount_?2:index==pointX_?4:index==pointY_?8:index==pointCurve_?16:index==lfoSmooth_?32:0;
    if(curveEdit){curveEdits_.fetch_or(curveEdit);requestService(false);return;}
    if(index==lfoShapeSelect_){requestService(false);return;}
    if(index==FullMode||index==controlIndex("oversampling")||index==presetSelect_||index==sampleSelect_||index==lfoShapeSelect_||
       index==curveTarget_||index==routeSelect_||index==pointSelect_||index==lfoSelect_||index==pointCount_||index==pointX_||index==pointY_||index==pointCurve_||index==lfoShape_||index==lfoSmooth_){
        if(index==curveTarget_||index==routeSelect_||index==pointSelect_||index==lfoSelect_)curveSelectionChanged_.store(true);
        requestService();return;}
    for(int p:tableSelect_)if(index==p){requestService();return;}
    for(const auto& r:routes_)if(index==r.source||index==r.destination){requestService();return;}
}
inline bool Vitaleon::formatValue(int index,float plain,std::string& out){
    if(uiKnobs_>=0&&index>=uiKnobs_&&index<uiKnobs_+16){int target=uiTargets_[index-uiKnobs_].load();out=target>=0?displayText(target):"Unused";return true;}
    // The Force's Screen-mode knobs write one slot per group. They are proxies for
    // the control on the visible page, so their value text must be the target's
    // text (Hz, dB, semitones) and not the raw 0..1 slot value.
    for(int g=0;g<7;++g)if(groupSlot_[g]>=0&&index>=groupSlot_[g]&&index<groupSlot_[g]+9){
        const int target=slotDisplayTarget(g,index-groupSlot_[g]);out=target>=0?displayText(target):"Unused";return true;}
    // The matrix rows and the picker show a route's source and target. The host
    // only has the engine key, so spell it the way the rest of the screen does
    // ("Filter 1 Cutoff", not "filter_1_cutoff").
    for(const auto& r:routes_)if(index==r.source||index==r.destination){
        const auto& list=index==r.source?sources_:destinations_;
        const int n=mpc::clamp(int(plain),0,int(list.size())-1);if(n==0){out="None";return true;}
        const int named=controlIndex(list[size_t(n)]);
        out=named>=0?param(named).name:list[size_t(n)];return true;}
    if(formatStorage(index,out))return true;
    if(homeControls_>=0&&index>=homeControls_&&index<homeControls_+7){
        int target=homeTarget(index-homeControls_);float value=param(target).toPlain(plain);
        if(formatValue(target,value,out))return true;
        out=param(target).display(value);if(!param(target).unit.empty())out+=" "+param(target).unit;return true;
    }
    if(formatLibrary(index,out))return true;
    if(formatTable(index,out))return true;
    for(const auto& b:banks_)for(size_t i=0;i<b.proxies.size();++i)if(index==b.proxies[i])return formatValue(b.targets[mpc::clamp(getInt(b.selector)-1,0,int(b.targets.size())-1)][i],plain,out);
    if(index==advancedValue_){int target=advancedTargets_[getInt(advancedSelector_)];out=displayText(target);return true;}
    auto fileLabel=[&](int group){
        if(group==1){
            std::lock_guard<std::mutex> tables(tableMutex_);
            if(assets_[1].empty()){out="Built-in / current";return true;}
            const int n=mpc::clamp(int(plain),0,int(assets_[1].size())-1);const auto& asset=assets_[1][n];
            out=n?std::filesystem::path(asset.entry.empty()?asset.path:asset.entry).stem().string():asset.label;return true;
        }
        const int n=mpc::clamp(int(plain),0,int(assets_[group].size())-1);const auto& asset=assets_[group][n];
        out=n?std::filesystem::path(asset.entry.empty()?asset.path:asset.entry).stem().string():asset.label;return true;};

    if(index==presetSelect_)return fileLabel(0);if(index==sampleSelect_)return fileLabel(2);if(index==lfoShapeSelect_)return fileLabel(3);
    for(int p:tableSelect_)if(index==p)return fileLabel(1);
    if(index==loadStatus_){std::lock_guard<std::mutex> lock(stateMutex_);out=pendingPreset_.load()>=0?"Load requested...":loadMessage_;return true;}
    if(index>=loadText_&&index<loadText_+4){
        std::lock_guard<std::mutex> lock(stateMutex_);const std::string message=pendingPreset_.load()>=0?"Load requested...":loadMessage_;size_t start=0,end=0;
        for(int part=0;part<=index-loadText_;++part){start=end;end=std::min(message.size(),start+31);while(end>start&&end<message.size()&&(uint8_t(message[end])&0xc0)==0x80)--end;if(end<message.size()){size_t space=message.rfind(' ',end-1);if(space!=std::string::npos&&space>start+14)end=space+1;}}
        out=message.substr(start,end-start);return true;
    }
    for(const auto& b:bindings_)if(b.index==index){
        if(param(index).kind!=mpc::Param::Kind::Float)return false;
        const auto& d=vital::Parameters::getDetails(b.key);double v=plain;
        switch(d.value_scale){case vital::ValueDetails::kQuadratic:v=v*v;break;case vital::ValueDetails::kCubic:v=v*v*v;break;case vital::ValueDetails::kQuartic:v=v*v*v*v;break;case vital::ValueDetails::kExponential:v=std::exp2(v);break;case vital::ValueDetails::kSquareRoot:v=std::sqrt(std::max(0.0,v));break;default:break;}
        v+=d.post_offset;if(d.display_invert&&v!=0)v=1/v;v*=d.display_multiply;
        std::string units=d.display_units;
        if(units==" semitones"&&b.key.size()>=7&&b.key.compare(b.key.size()-7,7,"_cutoff")==0){
            // Show the base filter frequency, before note tracking/modulation.
            v=vital::utils::midiNoteToFrequency(plain);units=" Hz";
            if(v>=1000){v/=1000;units=" kHz";}
        }else if(units==" semitones")units=" st";
        else if(units==" secs"){
            if(v>=0&&v<1){v*=1000;units=" ms";}else units=" s";
        }
        char text[80];std::snprintf(text,sizeof(text),"%.3g%s",v,units.c_str());out=text;return true;
    }return false;
}
