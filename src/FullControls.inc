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
    auto selector=[&](const char* id,const char* name,const std::vector<Asset>& files){
        // File selection is session-local; project state embeds the actual assets.
        auto p=mpc::intParam(id,name,0,int(files.size())-1,0);p.automatable=false;return addParam(p);
    };
    presetSelect_=selector("preset_select","Preset",assets_[0]);
    for(int i=0;i<3;++i){std::string id="table_"+std::to_string(i+1);tableSelect_[i]=selector(id.c_str(),("Osc "+std::to_string(i+1)+" Table").c_str(),assets_[1]);}
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
    initValues_[FullMode]=1;initValues_[Volume]=-24;initValues_[Voices]=8;
    initValues_[size_t(controlIndex("voice_override"))]=vital::VoiceHandler::kSteal;
    initValues_[size_t(controlIndex("volume"))]=3136;
    if(assets_[0].size()<=1)addProgram("Init",initValues_);
    createFilePrograms();
    createStorage();createNavigation();
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
    for(int i=0;i<3;++i){auto key="osc_"+std::to_string(i+1)+"_wave_frame";waveMono_[i]=mono.at(key);wavePoly_[i]=poly.at(key);waveMonoSwitch_[i]=engine_->getMonoModulationSwitch(key);wavePolySwitch_[i]=engine_->getPolyModulationSwitch(key);}
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
inline void Vitaleon::parameterChanged(int index,float plain){
    if(!ready_)return;
    fxPlanDirty_.store(true,std::memory_order_relaxed);
    if(navigationChanged(index,plain)||storageChanged(index))return;
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
    refreshProxies();
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
    if(formatStorage(index,out))return true;
    if(homeControls_>=0&&index>=homeControls_&&index<homeControls_+7){
        int target=homeTarget(index-homeControls_);float value=param(target).toPlain(plain);
        if(formatValue(target,value,out))return true;
        out=param(target).display(value);if(!param(target).unit.empty())out+=" "+param(target).unit;return true;
    }
    if(formatLibrary(index,out))return true;
    for(const auto& b:banks_)for(size_t i=0;i<b.proxies.size();++i)if(index==b.proxies[i])return formatValue(b.targets[mpc::clamp(getInt(b.selector)-1,0,int(b.targets.size())-1)][i],plain,out);
    if(index==advancedValue_){int target=advancedTargets_[getInt(advancedSelector_)];out=displayText(target);return true;}
    auto fileLabel=[&](int group){int n=mpc::clamp(int(plain),0,int(assets_[group].size())-1);const auto& asset=assets_[group][n];out=n?std::filesystem::path(asset.entry.empty()?asset.path:asset.entry).stem().string():asset.label;return true;};
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
