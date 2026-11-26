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
    // 28 characters per part: the message line lost 30 px to the heart badge in front of it.
    size_t start=0,end=0;
    for(int i=0;i<=part;++i){start=end;end=std::min(text.size(),start+28);while(end>start&&end<text.size()&&(uint8_t(text[end])&0xc0)==0x80)--end;if(end<text.size()){size_t space=text.rfind(' ',end-1);if(space!=std::string::npos&&space>start+14)end=space+1;}}
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
    // The hearts only mirror what the file already said; a missing or broken file is an
    // empty list, never an error, so the library starts with the sounds it can see.
    loadFavourites();
    // One lookup table from a browser entry to the preset's favourite key, built once:
    // assets_ never changes after the scan and the drive browser asks for it per repaint.
    presetLabels_.clear();
    for(int file=1;file<int(assets_[0].size());++file)
        presetLabels_[presetLabelKey(assets_[0][file].path,assets_[0][file].entry)]=assets_[0][file].label;
}
inline std::string Vitaleon::favouritesPath(){return mpc::pluginFolder()+"/Favourites.json";}
inline bool Vitaleon::isFavourite(const std::string& label)const{
    if(label.empty())return false;
    std::lock_guard<std::mutex> lock(favouritesMutex_);return favourites_.count(label)!=0;
}
inline void Vitaleon::setFavourite(const std::string& label,bool wanted){
    if(label.empty())return;
    {std::lock_guard<std::mutex> lock(favouritesMutex_);
     if(wanted)favourites_.insert(label);else favourites_.erase(label);}
    // The service loop owns the file: the host callback stays free of disk I/O. Wake it,
    // otherwise an idle loop would not write the tap until something else happens.
    favouritesDirty_.store(true,std::memory_order_relaxed);
    requestService();
}
inline void Vitaleon::loadFavourites(){
    std::set<std::string> loaded;
    std::vector<uint8_t> bytes;
    if(mpc::readFile(favouritesPath(),bytes)){
        try{Json state=Json::parse(bytes.begin(),bytes.end());
            if(state.is_object()&&state.value("format",0)==1&&state.count("favourites"))
                for(const auto& entry:state.at("favourites"))if(entry.is_string())loaded.insert(entry.get<std::string>());
        }catch(const std::exception&){/* a broken file simply means no favourites yet */}
    }
    std::lock_guard<std::mutex> lock(favouritesMutex_);favourites_=std::move(loaded);
}
inline void Vitaleon::saveFavourites(){
    // Write a temp file and rename it over the old one: a power cut mid-write leaves the
    // previous list instead of a truncated file the next start has to throw away.
    std::vector<std::string> labels;{std::lock_guard<std::mutex> lock(favouritesMutex_);labels.assign(favourites_.begin(),favourites_.end());}
    const Json state={{"format",1},{"favourites",labels}};
    const std::string path=favouritesPath(),temp=path+".tmp",text=state.dump();
    std::FILE* file=std::fopen(temp.c_str(),"wb");
    if(file==nullptr)return;
    const bool written=std::fwrite(text.data(),1,text.size(),file)==text.size();
    std::fclose(file);
    if(written&&std::rename(temp.c_str(),path.c_str())==0)return;
    std::remove(temp.c_str());
}
inline std::string Vitaleon::midiLearnPath(){return mpc::pluginFolder()+"/MidiLearn.json";}
inline void Vitaleon::loadMidiLearn(){
    int random=-1,favourite=-1;
    std::vector<uint8_t> bytes;
    if(mpc::readFile(midiLearnPath(),bytes)){
        try{Json state=Json::parse(bytes.begin(),bytes.end());
            if(state.is_object()&&state.value("format",0)==1){
                random=state.value("random_note",-1);favourite=state.value("favourite_note",-1);
            }
        }catch(const std::exception&){/* a broken file simply means no learned keys */}
    }
    randomNoteStored_.store(mpc::clamp(random,-1,127),std::memory_order_relaxed);
    favouriteNoteStored_.store(mpc::clamp(favourite,-1,127),std::memory_order_relaxed);
}
inline void Vitaleon::applyMidiLearn(){
    // Called once the parameters exist and again after a project restore: a chunk that was
    // saved with MIDI off must not switch off a key learned in another instance.
    const int random=randomNoteStored_.load(std::memory_order_relaxed),favourite=favouriteNoteStored_.load(std::memory_order_relaxed);
    if(libraryRandomNote_>=0&&getInt(libraryRandomNote_)!=random)publish(libraryRandomNote_,float(random));
    if(libraryFavouriteNote_>=0&&getInt(libraryFavouriteNote_)!=favourite)publish(libraryFavouriteNote_,float(favourite));
    browserDisplayDirty_.store(true);
}
inline void Vitaleon::saveMidiLearn(){
    // Nothing to write while the parameters already match the file.
    const int random=libraryRandomNote_>=0?getInt(libraryRandomNote_):-1,favourite=libraryFavouriteNote_>=0?getInt(libraryFavouriteNote_):-1;
    if(random==randomNoteStored_.load(std::memory_order_relaxed)&&favourite==favouriteNoteStored_.load(std::memory_order_relaxed))return;
    randomNoteStored_.store(random,std::memory_order_relaxed);favouriteNoteStored_.store(favourite,std::memory_order_relaxed);
    // Temp file and rename, like Favourites.json: a power cut leaves the previous keys.
    const Json state={{"format",1},{"random_note",random},{"favourite_note",favourite}};
    const std::string path=midiLearnPath(),temp=path+".tmp",text=state.dump();
    std::FILE* file=std::fopen(temp.c_str(),"wb");
    if(file==nullptr)return;
    const bool written=std::fwrite(text.data(),1,text.size(),file)==text.size();
    std::fclose(file);
    if(written&&std::rename(temp.c_str(),path.c_str())==0)return;
    std::remove(temp.c_str());
}
inline void Vitaleon::serviceMidiLearn(){
    // The host callback only marks the file dirty; writing stays on the service thread.
    if(midiLearnDirty_.exchange(false,std::memory_order_relaxed))saveMidiLearn();
}
inline bool Vitaleon::favouritesFiltered()const{return libraryFavouritesOnly_>=0&&getBool(libraryFavouritesOnly_);}
inline void Vitaleon::rebuildFavouritesFiles(){
    // Snapshot first: isFavourite takes the same lock, and a view that rebuilt while the
    // set was held would deadlock the host thread on its own tap.
    std::set<std::string> labels;{std::lock_guard<std::mutex> lock(favouritesMutex_);labels=favourites_;}
    std::vector<int> files;files.reserve(labels.size());
    if(!libraryBanks_.empty())for(int file:libraryBanks_[0].files)if(labels.count(assets_[0][file].label))files.push_back(file);
    std::lock_guard<std::mutex> lock(favouritesMutex_);favouritesFiles_=std::move(files);
}
inline size_t Vitaleon::favouriteCount()const{std::lock_guard<std::mutex> lock(favouritesMutex_);return favouritesFiles_.size();}
inline int Vitaleon::libraryPageCount()const{
    if(favouritesFiltered()){std::lock_guard<std::mutex> lock(favouritesMutex_);return std::max(1,(int(favouritesFiles_.size())+5)/6);}
    int bank=mpc::clamp(getInt(libraryBank_),0,int(libraryBanks_.size())-1);
    return std::max(1,(int(libraryBanks_[bank].files.size())+5)/6);
}
inline int Vitaleon::libraryFile(int row)const{
    if(favouritesFiltered()){
        // Same layout as a bank, only over the favourited files. The page count is
        // computed here: libraryPageCount() would take the lock a second time.
        std::lock_guard<std::mutex> lock(favouritesMutex_);
        const int pages=std::max(1,(int(favouritesFiles_.size())+5)/6),n=mpc::clamp(getInt(libraryPage_),0,pages-1)*6+row;
        return n>=0&&n<int(favouritesFiles_.size())?favouritesFiles_[n]:0;
    }
    int bank=mpc::clamp(getInt(libraryBank_),0,int(libraryBanks_.size())-1);
    int page=mpc::clamp(getInt(libraryPage_),0,libraryPageCount()-1),n=page*6+row;
    const auto& files=libraryBanks_[bank].files;return n>=0&&n<int(files.size())?files[n]:0;
}
inline void Vitaleon::refreshLibrary(){
    if(libraryRows_<0)return;
    // The filtered list is the heart set seen through the installed files; rebuilding it
    // here keeps it right after a heart tap, a MIDI toggle or a fresh file read.
    const bool filtered=favouritesFiltered();
    if(filtered)rebuildFavouritesFiles();
    // The favourites view is a list like the wavetable picker: the cursor owns the page, so
    // the wheel can neither leave the highlight behind nor scroll past the last entry.
    const int favourites=int(favouriteCount());
    favouriteCursor_=mpc::clamp(favouriteCursor_,0,std::max(0,favourites-1));
    if(filtered){
        if(libraryFavouriteStep_>=0)publish(libraryFavouriteStep_,float(favouriteCursor_));
        publish(libraryPage_,float(favouriteCursor_/6));
    }
    publish(libraryPage_,float(mpc::clamp(getInt(libraryPage_),0,libraryPageCount()-1)));
    for(int row=0;row<6;++row){int file=libraryFile(row);publish(libraryRows_+row,file&&file==getInt(presetSelect_)?1:0);
        if(libraryFavouriteRow_>=0){
            // The row handle is the one-shot tap and stays at 0; the readout under the image
            // is what the host paints, so a host repaint can never lose the heart.
            publish(libraryFavouriteRow_+row,0);
        }
        if(filtered&&libraryFavouriteState_>=0){
            // Every row of this view holds a favourite, so its heart is filled; an empty
            // row carries none. The drive browser publishes its own hearts (publishBrowser),
            // because its rows are not this list.
            publishNormalised(libraryFavouriteState_+row,file?1.f:0.f);
        }
        if(filtered&&libraryFavouriteRowState_>=0){
            // Row frame: 0 empty, 0.5 a favourite, 1 the row the wheel points at.
            const int at=mpc::clamp(getInt(libraryPage_),0,libraryPageCount()-1)*6+row;
            publishNormalised(libraryFavouriteRowState_+row,file?(at==favouriteCursor_?1.f:.5f):0.f);
        }
        float marker=float(file)/4096;for(int part=0;part<2;++part)publishNormalised(libraryNames_+row*2+part,marker);}
    if(!filtered){
        // The drive browser frames the same rows, so the plugin list names the entry a tap
        // acts on from the first repaint on (user report: no focus on start or after a load).
        // Its hearts follow those rows too: a heart tap, a MIDI toggle or a fresh file read
        // repaints them here, not only when the browser is navigated.
        std::lock_guard<std::mutex> lock(browserMutex_);publishBrowserFocus();publishBrowserHearts();
    }
    float selected=float(getInt(presetSelect_))/4096,bank=float(getInt(libraryBank_))/4096;
    for(int i=0;i<2;++i){publishNormalised(librarySelectedText_+i,selected);publishNormalised(libraryBankText_+i,bank);}
    if(libraryLoadedFavourite_>=0){std::string loaded;{std::lock_guard<std::mutex> lock(stateMutex_);loaded=selectedAsset_.label;}
        publishNormalised(libraryLoadedFavourite_,isFavourite(loaded)?1.f:0.f);}
    if(filtered&&storageRows_>=0){
        // The LIBRARY rows draw the favourites list while the view is on, so the browser
        // readouts carry its names and LOAD buttons. A fresh marker in every readout makes
        // the host ask for the text again (the same trick publishBrowser() uses).
        static std::atomic<unsigned> serial{0};
        const float marker=float((++serial)%16000000)/16000000;
        for(int p=storageRows_;p<numParams()&&p<=activeBankText_+1;++p)publishNormalised(p,marker);
        for(int row=0;row<6;++row)publishNormalised(storageAvailable_+row,libraryFile(row)?2.f:0.f);
    }
    publishNormalised(libraryPageText_,float(getInt(libraryBank_)*683+getInt(libraryPage_))/2800000);
}
inline void Vitaleon::publishLoadedName(){
    if(libraryLoadedText_<0)return;
    float marker=float(std::hash<std::string>{}(loadedName_)&0xffffff)/16777216.0f;
    for(int i=0;i<2;++i)publishNormalised(libraryLoadedText_+i,marker);
    // Called under stateMutex_ by the load paths, so only the favourites lock is taken.
    if(libraryLoadedFavourite_>=0)publishNormalised(libraryLoadedFavourite_,isFavourite(selectedAsset_.label)?1.f:0.f);
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
        // The note was played before it existed, so it may still sit in held_ (replayed
        // after every rebuild). Clear it: the note is a command, not a key.
        for(int ch=0;ch<16;++ch)held_[ch*128+captured].store(0);
        notifyHostParameterChanged(libraryRandomNote_);
        {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="Random preset via MIDI note "+std::to_string(captured)+".";}
        setLoadStatus(0);saveMidiLearn();
    }
    if(!randomMidiPending_.exchange(false,std::memory_order_relaxed))return;
    if(libraryBanks_[0].files.empty()){
        {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="No presets installed.";}
        setLoadStatus(2);return;
    }
    flashRandomPress();loadFileIndex(drawRandomFile());
}
inline void Vitaleon::serviceFavourites(){
    // The audio thread only records what it saw (see process()); every parameter write,
    // message and file write happens here, on the service thread.
    const int captured=favouriteLearnCaptured_.exchange(-1,std::memory_order_relaxed);
    if(captured>=0&&libraryFavouriteNote_>=0){
        publish(libraryFavouriteNote_,float(captured));
        // The key was played before it became a command and may still sit in held_.
        for(int ch=0;ch<16;++ch)held_[ch*128+captured].store(0);
        notifyHostParameterChanged(libraryFavouriteNote_);
        {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="Favourite key is MIDI note "+std::to_string(captured)+".";}
        setLoadStatus(0);saveMidiLearn();
    }
    if(favouriteMidiPending_.exchange(false,std::memory_order_relaxed)){
        // A note can only address the sound that is already loaded, never a list row.
        std::string label,name;
        {std::lock_guard<std::mutex> lock(stateMutex_);label=selectedAsset_.label;name=loadedName_;}
        if(label.empty()){
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="Load a sound from the list before favouriting it.";}
            setLoadStatus(2);
        }else{
            const bool now=!isFavourite(label);setFavourite(label,now);
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_=(now?"Favourite: ":"Favourite removed: ")+name;}
            setLoadStatus(0);refreshLibrary();browserDisplayDirty_.store(true);
        }
    }
    if(favouritesDirty_.exchange(false,std::memory_order_relaxed))saveFavourites();
}
inline bool Vitaleon::libraryChanged(int index,float plain){
    // One persistent bit change per tap. Never send a transient 1/0 pulse:
    // it needlessly relies on the host preserving both edges of one gesture.
    // Both directions are commands; release performs no parameter write.
    auto refresh=[this]{refreshLibrary();browserDisplayDirty_.store(true);};
    if(index==libraryFavouriteStep_){
        // The wheel of the favourites list moves the cursor and the page follows it. While
        // the view is off the same cell pages the drive browser, so the control is never
        // a dead knob between the two modes.
        if(favouritesFiltered()){
            favouriteCursor_=mpc::clamp(getInt(index),0,std::max(0,int(favouriteCount())-1));
            refresh();return true;
        }
        std::lock_guard<std::mutex> lock(browserMutex_);
        // Same wheel, same list: in the drive browser it walks the entries one click at a
        // time and the page follows the focus, exactly like the favourites list. Clamped to
        // the entries, so the wheel can never point past the last one.
        const int count=int(browser_.entries.size());
        browserCursor_=mpc::clamp(getInt(index),0,std::max(0,count-1));
        browser_.page=browserCursor_/6;
        publish(index,float(browserCursor_));
        publishBrowser();
        // A sound under the resting wheel loads itself, like the table picker; a drive,
        // folder or bank is only focused (OPEN stays a tap, the push opens as well).
        if(count>0&&browser_.entries[size_t(browserCursor_)].sound){browserLoadRow_.store(browserCursor_,std::memory_order_relaxed);browserLoadDue_.store(nowMs()+kBrowserLoadMs,std::memory_order_relaxed);}
        else browserLoadDue_.store(0,std::memory_order_relaxed);
        return true;
    }
    if(index==libraryLoad_){pendingPreset_.store(getInt(presetSelect_));for(int i=0;i<4;++i)publishNormalised(loadText_+i,.25f);requestService();browserDisplayDirty_.store(true);return true;}
    if(index==libraryRandomLearn_){
        // One-shot like the dice. Arming clears the old note, so LEARN twice always
        // ends at "off" and there is no extra button that only means "forget".
        if(plain<=0)return true;
        publish(index,0);
        favouriteLearnArmed_.store(false);
        if(randomLearnArmed_.exchange(false)){
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="MIDI learn cancelled.";}
        }else{
            publish(libraryRandomNote_,-1);
            midiLearnDirty_.store(true,std::memory_order_relaxed);requestService(false);
            randomLearnArmed_.store(true);
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="MIDI learn: play the note that should deal a random preset.";}
        }
        notifyHostParameterChanged(libraryRandomNote_);
        setLoadStatus(0);
        return true;
    }
    if(index==libraryFavouriteLearn_){
        // Same one-shot arming as the dice: arming forgets the old note, so two taps
        // always end at "Fav off". Arming one note disarms the other, because one
        // played key cannot mean two different things.
        if(plain<=0)return true;
        publish(index,0);
        if(favouriteLearnArmed_.exchange(false)){
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="Favourite learn cancelled.";}
        }else{
            randomLearnArmed_.store(false);
            publish(libraryFavouriteNote_,-1);
            midiLearnDirty_.store(true,std::memory_order_relaxed);requestService(false);
            favouriteLearnArmed_.store(true);
            {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_="Favourite learn: play the note that marks a sound.";}
        }
        notifyHostParameterChanged(libraryFavouriteNote_);
        setLoadStatus(0);
        return true;
    }
    if(index==libraryFavouritesOnly_){
        // A view, not a bank: the LIST is filtered to hearted presets and returns to its
        // first page, so the first favourite is always the first row.
        rebuildFavouritesFiles();favouriteCursor_=0;publish(libraryPage_,0);
        {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_=plain>0.5f?"Favourites only.":"Back to every preset of this bank.";}
        setLoadStatus(0);refresh();return true;
    }
    if(index>=libraryFavouriteRow_&&index<libraryFavouriteRow_+6){
        // The tap toggles the heart of the preset the row shows, in the view that is on:
        // the favourites list shows its own files, the drive browser its entries. A drive,
        // folder or bank row holds no preset, so its slot stays empty and the tap writes
        // nothing (user report: the hearts pointed at another preset than the visible row).
        // One tap toggles: the handle returns to 0, so a host that also reports the release
        // cannot toggle twice, and the tap direction comes from the stored state. The
        // release is filtered first: publish() re-enters this handler, and publishing 0
        // before this line would recurse into itself until the stack ran out.
        if(plain<=0)return true;
        publish(index,0);
        const int row=index-libraryFavouriteRow_;
        const std::string label=favouriteLabelForRow(row);
        if(label.empty())return true;
        const bool wanted=!isFavourite(label);
        setFavourite(label,wanted);
        std::string name=std::filesystem::path(label).filename().stem().string();
        while(!name.empty()&&name.front()==' ')name.erase(0,1);
        {std::lock_guard<std::mutex> lock(stateMutex_);loadMessage_=(wanted?"Favourite: ":"Favourite removed: ")+name;}
        setLoadStatus(0);refresh();return true;
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
        // PREV/NEXT move the cursor with the page, so the highlight never stays behind.
        if(favouritesFiltered())favouriteCursor_=mpc::clamp(getInt(libraryPage_)*6,0,std::max(0,int(favouriteCount())-1));
        refresh();return true;}
    if(index>=libraryRows_&&index<libraryRows_+6){int file=libraryFile(index-libraryRows_);if(file)publishList(presetSelect_,float(file));refresh();return true;}
    if(index==libraryRandomNote_||index==libraryFavouriteNote_){
        // The keys are settings of the plugin, not of the project: whatever writes them
        // (a host, a controller, an old project) the file follows, so the next instance
        // starts with the same remote controls.
        midiLearnDirty_.store(true,std::memory_order_relaxed);browserDisplayDirty_.store(true);requestService(false);return true;
    }
    if(index==presetSelect_){refresh();return true;} // browsing never changes sound
    return false;
}
inline bool Vitaleon::formatLibrary(int index,std::string& out){
    if(libraryRows_<0)return false;
    const bool filtered=favouritesFiltered();
    if(index>=libraryNames_&&index<libraryNames_+12){int row=(index-libraryNames_)/2,part=(index-libraryNames_)%2,file=libraryFile(row);
        out=textPart(file?assets_[0][file].name:(row==0?(filtered?"No favourites yet":"No presets in this bank"):""),part);return true;}
    if(index>=libraryBankText_&&index<libraryBankText_+2){out=textPart(libraryBanks_[mpc::clamp(getInt(libraryBank_),0,int(libraryBanks_.size())-1)].name,index-libraryBankText_);return true;}
    if(index>=librarySelectedText_&&index<librarySelectedText_+2){int file=mpc::clamp(getInt(presetSelect_),0,int(assets_[0].size())-1);out=textPart(file?assets_[0][file].name:"Select a sound in the list",index-librarySelectedText_);return true;}
    if(index>=libraryLoadedText_&&index<libraryLoadedText_+2){std::lock_guard<std::mutex> lock(stateMutex_);out=textPart(loadedName_,index-libraryLoadedText_);return true;}
    if(index==libraryPageText_){out=filtered?std::to_string(favouriteCount())+" favourites":std::to_string(filePrograms_.size())+" installed sounds";return true;}
    if(libraryFavouriteStep_>=0&&index==libraryFavouriteStep_){
        // The same cell pages the drive browser while the view is off, so it never looks
        // dead; with the view on it is the position in the favourites list.
        if(filtered){const int count=int(favouriteCount());out=(count?std::to_string(mpc::clamp(favouriteCursor_,0,count-1)+1):std::string("0"))+"/"+std::to_string(count);return true;}
        std::lock_guard<std::mutex> lock(browserMutex_);
        out=std::to_string(browser_.page+1)+"/"+std::to_string(std::max(1,(int(browser_.entries.size())+5)/6));return true;
    }
    if(index==libraryRandomNote_){
        if(randomLearnArmed_.load())out="MIDI learn: play a note";
        else{const int note=getInt(index);out=note<0?"MIDI off":"MIDI "+std::to_string(note);}
        return true;
    }
    if(index==libraryFavouriteNote_){
        if(favouriteLearnArmed_.load())out="Fav learn: play a note";
        else{const int note=getInt(index);out=note<0?"Fav off":"Fav "+std::to_string(note);}
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
