// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed native touch rows backed by the runtime Content index. No generated list
// of factory filenames: each instance discovers the banks on its drive.
inline void Vitaleon::createFilePrograms(){
    // createEffect reads numPrograms only once, after the constructor returns.
    // Enumerate the bank index now, but decompress/render only the chosen sound.
    factoryProgramCount_=assets_[0].size()<=1?1:0;
    std::set<std::string> used;
    for(int p=0;p<factoryProgramCount_;++p)used.insert(programName(p));
    auto shortName=[](const std::string& text,size_t limit){
        size_t end=std::min(text.size(),limit);
        while(end>0&&end<text.size()&&(uint8_t(text[end])&0xc0)==0x80)--end;
        return text.substr(0,end);
    };
    for(int file=1;file<int(assets_[0].size());++file){
        const std::string base=assets_[0][file].name.empty()?"Unnamed sound":assets_[0][file].name;
        std::string name=shortName(base,kVstMaxProgNameLen);
        for(int suffix=2;used.count(name);++suffix){
            std::string tail=" #"+std::to_string(suffix);
            name=shortName(base,kVstMaxProgNameLen-tail.size())+tail;
        }
        used.insert(name);filePrograms_.push_back(file);
        // Empty values deliberately preserve the previous sound until the file
        // passes validation. programSelected queues its complete engine state.
        addProgram(name.c_str(),{});
    }
}
inline std::string Vitaleon::textPart(const std::string& text,int part){
    size_t start=0,end=0;
    for(int i=0;i<=part;++i){start=end;end=std::min(text.size(),start+31);while(end>start&&end<text.size()&&(uint8_t(text[end])&0xc0)==0x80)--end;if(end<text.size()){size_t space=text.rfind(' ',end-1);if(space!=std::string::npos&&space>start+14)end=space+1;}}
    return text.substr(start,end-start);
}
inline void Vitaleon::createLibrary(){
    libraryBanks_.push_back({"All presets",{}});
    std::map<std::string,std::vector<int>> banks;
    for(int i=1;i<int(assets_[0].size());++i){libraryBanks_[0].files.push_back(i);banks[assets_[0][i].bank].push_back(i);}
    for(auto& bank:banks)libraryBanks_.push_back({bank.first,std::move(bank.second)});
    auto integer=[&](const char* id,const char* name,int max){auto p=mpc::intParam(id,name,0,max,0);p.automatable=false;return addParam(p);};
    auto button=[&](const char* id,const char* name){auto p=mpc::boolParam(id,name,false);p.automatable=false;return addParam(p);};
    libraryBank_=integer("library_bank","Library Bank",int(libraryBanks_.size())-1);
    libraryPage_=integer("library_page","Library Page",682);
    libraryLoad_=button("library_load","Load Preset");
    libraryPrevious_=button("library_previous","Previous Page");libraryNext_=button("library_next","Next Page");
    bankPrevious_=button("library_bank_previous","Previous Bank");bankNext_=button("library_bank_next","Next Bank");
    libraryRows_=numParams();for(int i=0;i<6;++i){auto id="library_row_"+std::to_string(i);button(id.c_str(),id.c_str());}
    libraryNames_=numParams();for(int i=0;i<12;++i)addReadout("library_name_"+std::to_string(i));
    libraryBankText_=numParams();for(int i=0;i<2;++i)addReadout("library_bank_text_"+std::to_string(i));
    librarySelectedText_=numParams();for(int i=0;i<2;++i)addReadout("library_selected_"+std::to_string(i));
    libraryLoadedText_=numParams();for(int i=0;i<2;++i)addReadout("library_loaded_"+std::to_string(i));
    libraryPageText_=addReadout("library_page_text");
}
inline int Vitaleon::libraryPageCount()const{
    int bank=mpc::clamp(getInt(libraryBank_),0,int(libraryBanks_.size())-1);
    return std::max(1,(int(libraryBanks_[bank].files.size())+5)/6);
}
inline int Vitaleon::libraryFile(int row)const{
    int bank=mpc::clamp(getInt(libraryBank_),0,int(libraryBanks_.size())-1);
    int page=mpc::clamp(getInt(libraryPage_),0,libraryPageCount()-1),n=page*6+row;
    const auto& files=libraryBanks_[bank].files;return n>=0&&n<int(files.size())?files[n]:0;
}
inline void Vitaleon::refreshLibrary(){
    if(libraryRows_<0)return;
    publish(libraryPage_,float(mpc::clamp(getInt(libraryPage_),0,libraryPageCount()-1)));
    for(int row=0;row<6;++row){int file=libraryFile(row);publish(libraryRows_+row,file&&file==getInt(presetSelect_)?1:0);
        float marker=float(file)/4096;for(int part=0;part<2;++part)publishNormalised(libraryNames_+row*2+part,marker);}
    float selected=float(getInt(presetSelect_))/4096,bank=float(getInt(libraryBank_))/4096;
    for(int i=0;i<2;++i){publishNormalised(librarySelectedText_+i,selected);publishNormalised(libraryBankText_+i,bank);}
    publishNormalised(libraryPageText_,float(getInt(libraryBank_)*683+getInt(libraryPage_))/2800000);
}
inline void Vitaleon::publishLoadedName(){
    if(libraryLoadedText_<0)return;
    float marker=float(std::hash<std::string>{}(loadedName_)&0xffffff)/16777216.0f;
    for(int i=0;i<2;++i)publishNormalised(libraryLoadedText_+i,marker);
}
inline void Vitaleon::flashRandomPress(){
    // The command handle never latches (see libraryChanged), so the lit look comes
    // from its own readout: 0.5 selects the pressed frame of the button filmstrip.
    if(libraryRandomFlash_<0)return;
    publishNormalised(libraryRandomFlash_,.5f);
    notifyHostParameterChanged(libraryRandomFlash_);
    randomFlashDue_.store(nowMs()+kRandomFlashMs,std::memory_order_relaxed);
    browserDisplayDirty_.store(true);
}
inline void Vitaleon::serviceRandomFlash(){
    // Same deadline pattern as serviceTables: the service loop runs this on every
    // pass, also while idle. A load runs inside the host's parameter callback, and
    // the host cannot repaint while it is inside that callback, so a countdown
    // started at the tap would be over before the first repaint arrives. Keep the
    // dice lit for as long as the load runs, then for the full flash duration.
    if(libraryRandomFlash_<0)return;
    if(!randomFlashDue_.load(std::memory_order_relaxed))return;
    if(requested_.load(std::memory_order_relaxed)!=completed_.load(std::memory_order_relaxed)){
        randomFlashDue_.store(nowMs()+kRandomFlashMs,std::memory_order_relaxed);return;
    }
    long long due=randomFlashDue_.load(std::memory_order_relaxed);
    if(nowMs()<due)return;
    if(!randomFlashDue_.compare_exchange_strong(due,0,std::memory_order_relaxed))return;
    publishNormalised(libraryRandomFlash_,0.f);
    notifyHostParameterChanged(libraryRandomFlash_);
    browserDisplayDirty_.store(true);
}
inline void Vitaleon::serviceRandomMidi(){
    // The audio thread only records what it saw (see handleFullMidi); everything that
    // touches parameters or loads a sound happens here, on the service thread.
    const int captured=randomLearnCaptured_.exchange(-1,std::memory_order_relaxed);
    if(captured>=0&&libraryRandomNote_>=0){
        publish(libraryRandomNote_,float(captured));
        notifyHostParameterChanged(libraryRandomNote_);
        {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="Random preset via MIDI note "+std::to_string(captured)+".";}
        setLoadStatus(0);
    }
    if(!randomMidiPending_.exchange(false,std::memory_order_relaxed))return;
    if(libraryBanks_[0].files.empty()){
        {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="No presets installed.";}
        setLoadStatus(2);return;
    }
    flashRandomPress();loadFileIndex(drawRandomFile());
}
inline bool Vitaleon::libraryChanged(int index,float plain){
    // One persistent bit change per tap. Never send a transient 1/0 pulse:
    // it needlessly relies on the host preserving both edges of one gesture.
    // Both directions are commands; release performs no parameter write.
    auto refresh=[this]{refreshLibrary();browserDisplayDirty_.store(true);};
    if(index==libraryLoad_){pendingPreset_.store(getInt(presetSelect_));for(int i=0;i<4;++i)publishNormalised(loadText_+i,.25f);requestService();browserDisplayDirty_.store(true);return true;}
    if(index==libraryRandomLearn_){
        // One-shot like the dice. Arming clears the old note, so LEARN twice always
        // ends at "off" and there is no extra button that only means "forget".
        if(plain<=0)return true;
        publish(index,0);
        if(randomLearnArmed_.exchange(false)){
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="MIDI learn cancelled.";}
        }else{
            publish(libraryRandomNote_,-1);
            randomLearnArmed_.store(true);
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="MIDI learn: play the note that should deal a random preset.";}
        }
        notifyHostParameterChanged(libraryRandomNote_);
        setLoadStatus(0);
        return true;
    }
    if(index==libraryRandom_){
        // One tap is one write with the value 1; the handle goes straight back to 0.
        // Acting only on 1 means a host that also reports the release cannot start a
        // second load from the same press.
        if(plain<=0)return true;
        publish(index,0);
        flashRandomPress();
        if(libraryBanks_[0].files.empty()){
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="No presets installed.";}
            setLoadStatus(2);return true;
        }
        loadFileIndex(drawRandomFile());return true;
    }
    if(index==libraryBank_||index==bankPrevious_||index==bankNext_){
        if(index!=libraryBank_)publish(libraryBank_,float(mpc::clamp(getInt(libraryBank_)+(index==bankNext_?1:-1),0,int(libraryBanks_.size())-1)));
        publish(libraryPage_,0);refresh();return true;
    }
    if(index==libraryPage_||index==libraryPrevious_||index==libraryNext_){
        if(index!=libraryPage_)publish(libraryPage_,float(mpc::clamp(getInt(libraryPage_)+(index==libraryNext_?1:-1),0,libraryPageCount()-1)));
        refresh();return true;
    }
    if(index>=libraryRows_&&index<libraryRows_+6){int file=libraryFile(index-libraryRows_);if(file)publishList(presetSelect_,float(file));refresh();return true;}
    if(index==presetSelect_){refresh();return true;} // browsing never changes sound
    return false;
}
inline bool Vitaleon::formatLibrary(int index,std::string& out){
    if(libraryRows_<0)return false;
    if(index>=libraryNames_&&index<libraryNames_+12){int row=(index-libraryNames_)/2,part=(index-libraryNames_)%2,file=libraryFile(row);
        out=textPart(file?assets_[0][file].name:(row==0?"No presets in this bank":""),part);return true;}
    if(index>=libraryBankText_&&index<libraryBankText_+2){out=textPart(libraryBanks_[mpc::clamp(getInt(libraryBank_),0,int(libraryBanks_.size())-1)].name,index-libraryBankText_);return true;}
    if(index>=librarySelectedText_&&index<librarySelectedText_+2){int file=mpc::clamp(getInt(presetSelect_),0,int(assets_[0].size())-1);out=textPart(file?assets_[0][file].name:"Select a sound in the list",index-librarySelectedText_);return true;}
    if(index>=libraryLoadedText_&&index<libraryLoadedText_+2){std::lock_guard<std::mutex> lock(stateMutex_);out=textPart(loadedName_,index-libraryLoadedText_);return true;}
    if(index==libraryPageText_){out=std::to_string(filePrograms_.size())+" installed sounds";return true;}
    if(index==libraryRandomNote_){
        if(randomLearnArmed_.load())out="MIDI learn: play a note";
        else{const int note=getInt(index);out=note<0?"MIDI off":"MIDI "+std::to_string(note);}
        return true;
    }
    return false;
}
inline int Vitaleon::drawRandomFile(){
    // The deck always covers every installed sound and not the bank that happens to be
    // open: the bank selector follows browsing, so a bank-scoped deck would quietly
    // narrow to one sound right after a bank preset was loaded. The deck is shuffled
    // without putting cards back, so N sounds play once each before the next cycle;
    // a plain random pick would repeat the same sound about every 32 presses with N = 32
    // and show some duplicate among seven presses in half of the cases.
    const auto& files=libraryBanks_[0].files;
    if(files.empty())return 0;
    long long stamp=0;for(int file:files)stamp+=file;
    if(randomDeck_.size()!=files.size()||randomDeckStamp_!=stamp){
        randomDeck_=files;
        std::shuffle(randomDeck_.begin(),randomDeck_.end(),randomRng_);
        randomDrawn_=0;randomDeckStamp_=stamp;
        // The first card of a fresh cycle must not be the sound that is selected last,
        // or a cycle boundary would look like a repeat.
        if(randomDeck_.size()>1&&randomDeck_.front()==getInt(presetSelect_))std::swap(randomDeck_.front(),randomDeck_.back());
    }
    const int file=randomDeck_[randomDrawn_++];
    if(randomDrawn_>=randomDeck_.size()){randomDeck_.clear();randomDrawn_=0;randomDeckStamp_=0;}
    return file;
}
