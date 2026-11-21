// SPDX-License-Identifier: GPL-3.0-or-later
#include "AssetValidation.h"
#include "BankArchive.h"

inline void Vitaleon::scanAssets(){
    for(auto& group:assets_)group.push_back({"Built-in / current",""});
    const char* folders[]={"Presets","Wavetables","Samples","LFOs"};
    for(int group=0;group<4;++group){
        const auto root=std::filesystem::path(mpc::contentPath(folders[group]));std::error_code ec;
        if(!std::filesystem::exists(root,ec))continue;
        for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end&&assets_[group].size()<4096;it.increment(ec)){
            if(ec){ec.clear();continue;}if(!it->is_regular_file(ec))continue;
            std::string ext=it->path().extension().string();for(auto& c:ext)c=char(std::tolower((unsigned char)c));
            if((group==0&&ext==".vital")||(group==1&&(ext==".vitaltable"||ext==".wav"))||(group==2&&ext==".wav")||(group==3&&ext==".vitallfo"))
                assets_[group].push_back({it->path().lexically_relative(root).generic_string(),it->path().string()});
        }
    }
    std::error_code ec;auto root=std::filesystem::path(mpc::contentPath("Banks"));
    if(std::filesystem::exists(root,ec))for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)){
        if(ec){ec.clear();continue;}if(!it->is_regular_file(ec))continue;std::string ext=it->path().extension().string();for(auto& c:ext)c=char(std::tolower((unsigned char)c));if(ext!=".vitalbank"&&ext!=".zip")continue;
        try {BankArchive bank(it->path().string());for(const auto& e:bank.entries()){std::string type=std::filesystem::path(e.name).extension().string();for(auto& c:type)c=char(std::tolower((unsigned char)c));int group=type==".vital"?0:type==".vitaltable"?1:type==".vitallfo"?3:type==".wav"?(e.name.find("Wavetable")!=std::string::npos?1:2):-1;
            if(group>=0&&assets_[group].size()<4096)assets_[group].push_back({it->path().stem().string()+" / "+e.name,it->path().string(),e.name});}}
        catch(const std::exception& e){loadMessage_=std::string("Bank skipped: ")+it->path().filename().string()+": "+e.what();}
    }
    for(auto& group:assets_){std::sort(group.begin()+1,group.end(),[](const Asset& a,const Asset& b){return a.label<b.label;});
        for(auto& asset:group){asset.name=asset.path.empty()?asset.label:std::filesystem::path(asset.entry.empty()?asset.path:asset.entry).stem().string();
            asset.bank=asset.entry.empty()?"Loose presets":std::filesystem::path(asset.path).stem().string();}}
}
inline std::vector<uint8_t> Vitaleon::readAssetBytes(const Asset& asset){
    if(!asset.entry.empty()){BankArchive bank(asset.path);return bank.extract(asset.entry);}
    std::error_code ec;auto size=std::filesystem::file_size(asset.path,ec);
    vitaleon_assets::require(!ec&&size<=64*1024*1024,"Asset missing or larger than 64 MiB");
    std::vector<uint8_t> bytes;vitaleon_assets::require(mpc::readFile(asset.path,bytes),"Cannot read asset");return bytes;
}
inline Vitaleon::Json Vitaleon::readAsset(const Asset& asset){
    auto bytes=readAssetBytes(asset);Json j=Json::parse(bytes.begin(),bytes.end());vitaleon_assets::tree(j);return j;
}
inline void Vitaleon::defaultTables(){
    savedAssets_["wavetables"]=Json::array();
    for(int o=0;o<3;++o){auto* table=engine_->getWavetable(o);
        WavetableCreator creator(table);creator.initPredefinedWaves();creator.render();savedAssets_["wavetables"].push_back(creator.stateToJson());
    }
}
// One-time conversion of the original sixteen-parameter project/program format.
// Only the full graph renders audio; the old fields remain reserved for state IDs.
inline void Vitaleon::migrateOldControls(){
    for(const auto& b:bindings_)publish(b.index,param(b.index).def);
    auto put=[&](const char* key,float value){publish(controlIndex(key),value);};
    put("volume",(get(Volume)+80)*(get(Volume)+80));
    put("filter_1_on",get(Filter));put("filter_1_cutoff",69+12*std::log2(get(Cutoff)/440));
    put("filter_1_resonance",get(Resonance));put("filter_1_model",vital::constants::kDigital);put("filter_1_style",0);
    put("env_1_attack",std::pow(get(Attack)*.001f,.25f));put("env_1_decay",std::pow(get(Decay)*.001f,.25f));
    put("env_1_sustain",get(Sustain));put("env_1_release",std::pow(get(Release)*.001f,.25f));
    put("oversampling",0);put("voice_override",vital::VoiceHandler::kSteal);
    for(const char* key:{"chorus_on","compressor_on","delay_on","distortion_on","eq_on","filter_fx_on","flanger_on","phaser_on","reverb_on","filter_2_on","sample_on"})put(key,0);
    const int spectral[]={vital::SynthOscillator::kNoSpectralMorph,vital::SynthOscillator::kFormScale,vital::SynthOscillator::kHarmonicScale,vital::SynthOscillator::kSmear};
    for(int o=0;o<3;++o){auto pre="osc_"+std::to_string(o+1)+"_";
        auto osc=[&](const char* suffix,float value){publish(controlIndex(pre+suffix),value);};
        osc("on",o<getInt(Oscillators)?1:0);osc("unison_voices",get(Unison));osc("unison_detune",std::sqrt(get(Detune)));
        osc("wave_frame",get(WavePos)*85);osc("spectral_morph_type",float(spectral[getInt(Spectral)]));osc("spectral_morph_amount",get(Warp));
        osc("destination",vital::constants::kFilter1);osc("level",.5f);osc("random_phase",.75f);osc("tune",o==0?0:(o==1?-.07f:.07f));
        publishList(tableSelect_[o],0);
    }
    for(const auto& route:routes_){publish(route.source,0);publish(route.destination,0);}
    publishList(presetSelect_,0);publishList(sampleSelect_,0);publishList(lfoShapeSelect_,0);publish(FullMode,1);
    migrateOldTables_.store(true);programReset_.store(true);refreshProxies();
}
inline void Vitaleon::migrationTables(){
    const vital::PredefinedWaveFrames::Shape shapes[]={vital::PredefinedWaveFrames::kSin,vital::PredefinedWaveFrames::kTriangle,vital::PredefinedWaveFrames::kSaw,vital::PredefinedWaveFrames::kSquare};
    savedAssets_["wavetables"]=Json::array();
    for(int o=0;o<3;++o){
        WavetableCreator creator(engine_->getWavetable(o));auto* group=new WavetableGroup();auto* wave=new WaveSource();
        wave->setInterpolationStyle(WaveSource::kLinear);wave->setInterpolationMode(WaveSource::kTime);
        for(int i=0;i<4;++i){wave->insertNewKeyframe(i*85);wave->getKeyframe(i)->wave_frame()->copy(vital::PredefinedWaveFrames::getWaveFrame(shapes[i]));}
        group->addComponent(wave);creator.addGroup(group);auto state=creator.stateToJson();state["full_normalize"]=false;state["remove_all_dc"]=false;
        creator.jsonToState(state);savedAssets_["wavetables"].push_back(creator.stateToJson());
    }
}
inline void Vitaleon::loadTable(int oscillator,const Json& table){
    vitaleon_assets::table(table);
    if(savedAssets_.count("wavetables")&&savedAssets_["wavetables"].size()==3&&savedAssets_["wavetables"][oscillator]==table)return;
    WavetableCreator creator(engine_->getWavetable(oscillator));creator.jsonToState(table);
    savedAssets_["wavetables"][oscillator]=creator.stateToJson();
}
inline void Vitaleon::loadCurve(LineGenerator* curve,const Json& data){vitaleon_assets::curve(data);curve->jsonToState(data);}
inline void Vitaleon::loadSampleState(const Json& data,const vitaleon_assets::SampleData* decoded){
    // The saved PCM is the exact input used for the current band-limited sample.
    // Reuse its prepared buffers when another preset embeds identical data.
    if(savedAssets_.count("sample")&&savedAssets_["sample"]==data)return;
    vitaleon_assets::SampleData owned;
    if(!decoded){owned=vitaleon_assets::sample(data);decoded=&owned;}
    const auto& sample=*decoded;auto* target=engine_->getSample();
    if(sample.right.empty())target->loadSample(sample.left.data(),int(sample.left.size()),sample.rate);
    else target->loadSample(sample.left.data(),sample.right.data(),int(sample.left.size()),sample.rate);
    target->setName(sample.name);savedAssets_["sample"]=data;
}
inline void Vitaleon::captureAssets(){
    if(!savedAssets_.count("sample")){
        auto* sample=engine_->getSample();int count=sample->originalLength();Json data={{"name",sample->getName()},{"length",count},{"sample_rate",sample->sampleRate()}};
        for(int ch=0;ch<(sample->isStereo()?2:1);++ch){std::vector<int16_t> pcm(count);vital::utils::floatToPcmData(pcm.data(),sample->originalBuffer(ch!=0),count);data[ch?"samples_stereo":"samples"]=Base64::toBase64(pcm.data(),pcm.size()*2).toStdString();}
        loadSampleState(data);
    }
    savedAssets_["lfos"]=Json::array();savedAssets_["line_mappings"]=Json::array();
    for(int i=0;i<8;++i)savedAssets_["lfos"].push_back(engine_->getLfoSource(i)->stateToJson());
    for(int i=0;i<64;++i)savedAssets_["line_mappings"].push_back(engine_->getModulationBank().atIndex(i)->modulation_processor->lineMapGenerator()->stateToJson());
}
inline void Vitaleon::restoreAssets(const Json& state,bool tables,bool samples){
    if(tables&&state.count("wavetables")){vitaleon_assets::require(state["wavetables"].size()==3,"Expected three wavetables");for(int i=0;i<3;++i)loadTable(i,state["wavetables"][i]);}
    if(samples&&state.count("sample")&&!state["sample"].is_null())loadSampleState(state["sample"]);
    if(state.count("lfos")){vitaleon_assets::require(state["lfos"].size()<=8,"Too many LFOs");for(size_t i=0;i<state["lfos"].size();++i)loadCurve(engine_->getLfoSource(int(i)),state["lfos"][i]);}
    if(state.count("line_mappings")){vitaleon_assets::require(state["line_mappings"].size()<=64,"Too many mappings");for(size_t i=0;i<state["line_mappings"].size();++i)loadCurve(engine_->getModulationBank().atIndex(int(i))->modulation_processor->lineMapGenerator(),state["line_mappings"][i]);}
    captureAssets();for(const char* key:{"preset_name","author"}){if(state.count(key))savedAssets_[key]=state[key];else savedAssets_.erase(key);}curveSelectionChanged_.store(true);
}
inline void Vitaleon::loadPreset(const Json& preset){
    using namespace vitaleon_assets;
    tree(preset);
    require(preset.is_object()&&preset.count("settings")&&preset["settings"].is_object(),"Not a Vital preset");
    const auto& s=preset["settings"];const std::string version=preset.value("synth_version",std::string("unknown"));
    bool modernStacks=LoadSave::compareVersionStrings(version,"1.5.0")>=0;
    for(int o=1;o<=3;++o)modernStacks|=s.count("osc_"+std::to_string(o)+"_spectral_morph_phase")!=0;
    // Tolerant import: Vital itself ignores settings it does not know. Load
    // everything this engine can represent and report what was left out,
    // instead of refusing the whole sound.
    importNotes_.clear();std::map<std::string,double> overrides;
    auto note=[&](const std::string& text){if(std::find(importNotes_.begin(),importNotes_.end(),text)==importNotes_.end())importNotes_.push_back(text);};
    auto controlValue=[&](const std::string& key,double value){
        auto forced=overrides.find(key);if(forced!=overrides.end())return forced->second;
        // 1.5 inserts Three Octaves at ID 5, shifting Power Chord..Odd Harmonics.
        // Internal IDs retain the 1.0 layout; new choices are appended at 11/12.
        if(modernStacks&&key.find("osc_")==0&&key.size()>12&&key.substr(key.size()-12)=="_stack_style"){
            if(value==5)return 11.0;if(value>=6&&value<=11)return value-1;
        }return value;
    };
    require(LoadSave::compareVersionStrings(version,"1.0.0")>=0,"Pre-1.0 Vital preset needs conversion in Vital");
    // Never silently clamp newer DSP modes to unrelated modes in this engine.
    for(auto i=s.begin();i!=s.end();++i)if(i.value().is_number()){
        const auto key=i.key();if(key=="polyphony"){require(number(i.value())>=1&&number(i.value())<=32,"Polyphony outside 1–32");continue;}if(key=="beats_per_minute")continue;
        // Compatibility metadata, not a new DSP control: newer presets store a
        // spectral phase that has no effect for morph modes 0–11. The unmodulated
        // default is accepted silently, any other value is ignored and reported.
        bool phaseDefault=false;
        for(int osc=1;osc<=3;++osc){auto prefix="osc_"+std::to_string(osc)+"_";
            if(key==prefix+"spectral_morph_phase"){phaseDefault=true;if(std::abs(number(i.value())-.5)>=1e-7)note("OSC "+std::to_string(osc)+" spectral phase ignored");}
            // Spectral modes added after Vital 1.0.6 do not exist in this engine:
            // the oscillator loads without spectral morphing (never another mode).
            if(key==prefix+"spectral_morph_type"&&number(i.value())>param(controlIndex(key)).max+1e-5){overrides[key]=0;note("OSC "+std::to_string(osc)+" spectral mode "+std::to_string(int(number(i.value())))+" not available, off");}
        }
        if(phaseDefault)continue;
        // 1.6 writes both ramp fields for every modulation slot. This older
        // engine has no added ramp stage: accept only the minimum setting as
        // a compatibility bypass, never a configured/modulated ramp. This is
        // not a claim of sample-identical 1.6 smoothing (see COMPATIBILITY.md).
        bool minimumRamp=false;
        for(int slot=1;slot<=64;++slot){auto prefix="modulation_"+std::to_string(slot)+"_ramp_";
            if(key==prefix+"up"||key==prefix+"down"){minimumRamp=true;if(number(i.value())!=-10.0)note("Modulation ramps ignored");}
        }
        if(minimumRamp)continue;
        int index=controlIndex(key);if(index<0){note("Ignored: "+key);continue;}
        double value=controlValue(key,number(i.value()));require(value>=param(index).min-1e-5&&value<=param(index).max+1e-5,"Unsupported preset value: "+key+" (Vital "+version+")");
    }
    require(!s.count("modulations")||s["modulations"].size()<=64,"More than 64 modulations");
    if(s.count("modulations"))for(const auto& route:s["modulations"]){std::string src=route.at("source"),dst=route.at("destination");
        if(!src.empty()&&std::find(sources_.begin(),sources_.end(),src)==sources_.end())note("Mod source skipped: "+src);
        if(!dst.empty()&&std::find(destinations_.begin(),destinations_.end(),dst)==destinations_.end())note("Mod target skipped: "+dst);
        if(route.count("line_mapping"))curve(route["line_mapping"]);}
    if(s.count("lfos")){require(s["lfos"].size()<=8,"Too many LFOs");for(const auto& l:s["lfos"])curve(l);}
    // jsonToState already renders. Prepare each changed table exactly once,
    // retaining the results until ALL preflight checks pass. Reuse unchanged
    // current tables; their canonical JSON describes the current engine data.
    std::array<std::unique_ptr<vital::Wavetable>,3> preparedTables;
    std::array<Json,3> preparedTableStates;
    if(s.count("wavetables")){
        require(s["wavetables"].size()==3,"Expected three wavetables");
        for(int o=0;o<3;++o){const auto& t=s["wavetables"][o];table(t);
            if(savedAssets_.count("wavetables")&&savedAssets_["wavetables"].size()==3&&savedAssets_["wavetables"][o]==t)continue;
            preparedTables[o]=std::make_unique<vital::Wavetable>(257);
            WavetableCreator c(preparedTables[o].get());c.jsonToState(t);preparedTableStates[o]=c.stateToJson();
        }
    }
    std::unique_ptr<SampleData> preparedSample;
    if(s.count("sample")&&!s["sample"].is_null())preparedSample=std::make_unique<SampleData>(sample(s["sample"]));
    // Preflight above must succeed before switching out of the legacy graph.
    clearRoutes();
    if(!s.count("wavetables"))defaultTables();
    // Do not build a fresh band-limited noise sample immediately before replacing
    // it with the preset's embedded sample. Missing samples still reset to noise.
    if(!s.count("sample")||s["sample"].is_null()){engine_->getSample()->init();savedAssets_.erase("sample");}
    for(int i=0;i<8;++i)engine_->getLfoSource(i)->initTriangle();
    programReset_.store(false);migrateOldTables_.store(false);curveEdits_.store(0);
    for(const auto& b:bindings_)publish(b.index,s.count(b.key)?float(controlValue(b.key,s[b.key].get<double>())):param(b.index).def);
    // MPC budget: imported sounds start at 1x oversampling and at most eight
    // voices. Nearly every Vital preset stores 2x, which roughly doubles the
    // cost per voice. Both remain adjustable (HOME > VOICES) and are saved
    // with the MPC project; the preset file itself is never changed.
    publish(Voices,std::min(8.0f,s.value("polyphony",8.0f)));publish(controlIndex("oversampling"),0);publish(FullMode,1);
    for(int i=0;i<64;++i){publish(routes_[i].source,0);publish(routes_[i].destination,0);auto* mapping=engine_->getModulationBank().atIndex(i)->modulation_processor->lineMapGenerator();mapping->initLinear();
        if(s.count("modulations")&&size_t(i)<s["modulations"].size()){const auto& r=s["modulations"][i];std::string src=r["source"],dst=r["destination"];
            if(!src.empty()&&!dst.empty()&&std::find(sources_.begin(),sources_.end(),src)!=sources_.end()&&std::find(destinations_.begin(),destinations_.end(),dst)!=destinations_.end()){publish(routes_[i].source,float(std::find(sources_.begin(),sources_.end(),src)-sources_.begin()));publish(routes_[i].destination,float(std::find(destinations_.begin(),destinations_.end(),dst)-destinations_.begin()));}
            if(r.count("line_mapping"))loadCurve(mapping,r["line_mapping"]);}}
    for(int o=0;o<3;++o)if(preparedTables[o]){
        engine_->getWavetable(o)->adoptPrepared(*preparedTables[o]);
        savedAssets_["wavetables"][o]=std::move(preparedTableStates[o]);
    }
    if(preparedSample)loadSampleState(s["sample"],preparedSample.get());
    restoreAssets(s,false,false);savedAssets_["preset_name"]=preset.value("preset_name",std::string("Imported"));savedAssets_["author"]=preset.value("author",std::string());
    // A preset brings its own tables, sample and LFO shapes. The file selectors go
    // back to "Built-in / current" so they never name a file that is no longer loaded
    // (seen on the Live II: table 14 chosen, factory sound loaded, selector still said 14).
    for(int o=0;o<3;++o){publishList(tableSelect_[o],0);lastTable_[o]=0;}
    publishList(sampleSelect_,0);lastSample_=0;publishList(lfoShapeSelect_,0);lastShape_=0;
}
inline void Vitaleon::loadSelections(){
    for(int o=0;o<3;++o){int selection=getInt(tableSelect_[o]);if(selection==lastTable_[o])continue;lastTable_[o]=selection;if(!selection)continue;
        Asset asset;{std::lock_guard<std::mutex> tables(tableMutex_);if(selection>=int(assets_[1].size()))continue;asset=assets_[1][size_t(selection)];}
        auto extension=std::filesystem::path(asset.entry.empty()?asset.path:asset.entry).extension().string();for(auto& c:extension)c=char(std::tolower((unsigned char)c));if(extension==".wav"){
            dsp::WavData w;vitaleon_assets::require(dsp::parseWav(readAssetBytes(asset),w)&&w.numFrames()<=16777216,"Invalid wavetable WAV");
            WavetableCreator c(engine_->getWavetable(o));c.initFromAudioFile(w.channels[0].data(),int(w.numFrames()),int(w.sampleRate),WavetableCreator::kWavetableSplice,FileSource::kNoInterpolate);c.render();savedAssets_["wavetables"][o]=c.stateToJson();
        }else loadTable(o,readAsset(asset));
    }
    int sampleIndex=getInt(sampleSelect_);if(sampleIndex!=lastSample_){lastSample_=sampleIndex;if(sampleIndex){const auto& asset=assets_[2].at(sampleIndex);dsp::WavData w;
        vitaleon_assets::require(dsp::parseWav(readAssetBytes(asset),w)&&w.numFrames()>=4&&w.numFrames()<=16777216,"Invalid sample WAV");
        Json data={{"name",asset.label},{"length",w.numFrames()},{"sample_rate",int(w.sampleRate)}};
        for(int ch=0;ch<std::min(2,w.numChannels());++ch){std::vector<int16_t> pcm(w.numFrames());for(size_t i=0;i<pcm.size();++i){vitaleon_assets::require(std::isfinite(w.channels[ch][i]),"Non-finite WAV");pcm[i]=int16_t(std::round(mpc::clamp(w.channels[ch][i],-1.0f,1.0f)*32767));}data[ch?"samples_stereo":"samples"]=Base64::toBase64(pcm.data(),pcm.size()*2).toStdString();}loadSampleState(data);}}
    int shape=getInt(lfoShapeSelect_);if(shape!=lastShape_){lastShape_=shape;if(shape){loadCurve(engine_->getLfoSource(getInt(lfoSelect_)-1),readAsset(assets_[3].at(shape)));curveSelectionChanged_.store(true);}}
}
inline void Vitaleon::editLfo(){
    auto* curve=selectedCurve();
    unsigned edits=curveEdits_.exchange(0);
    if(edits){
        switch((edits&1)?getInt(lfoShape_):0){case 1:curve->initTriangle();break;case 2:curve->initSin();break;case 3:curve->initSawUp();break;case 4:curve->initSawDown();break;case 5:curve->initSquare();break;default:{
            int n=(edits&2)?getInt(pointCount_):curve->getNumPoints();if(n!=curve->getNumPoints()){while(curve->getNumPoints()<n)curve->addMiddlePoint(curve->getNumPoints()-2);while(curve->getNumPoints()>n)curve->removePoint(curve->getNumPoints()-2);}
            int p=mpc::clamp(getInt(pointSelect_)-1,0,n-1);float lo=p?curve->getPoint(p-1).first:0,hi=p+1<n?curve->getPoint(p+1).first:1;
            auto point=curve->getPoint(p);if(edits&4)point.first=mpc::clamp(get(pointX_),lo,hi);if(edits&8)point.second=1-get(pointY_);
            curve->setPoint(p,point);if(edits&16)curve->setPower(p,get(pointCurve_));break;}}
        if(edits&32)curve->setSmooth(getBool(lfoSmooth_));curve->render();publish(lfoShape_,0);
    }
    curveSelectionChanged_.store(false);refreshCurveControls();
}
inline void Vitaleon::refreshCurveControls(){
    const auto* curve=selectedCurve();int p=mpc::clamp(getInt(pointSelect_)-1,0,curve->getNumPoints()-1);
    auto edits=curveEdits_.load();publish(pointSelect_,float(p+1));
    if(!(edits&2))publish(pointCount_,float(curve->getNumPoints()));
    if(!(edits&4))publish(pointX_,curve->getPoint(p).first);
    if(!(edits&8))publish(pointY_,1-curve->getPoint(p).second);
    if(!(edits&16))publish(pointCurve_,curve->getPower(p));
    if(!(edits&32))publish(lfoSmooth_,curve->smooth()?1:0);
}
