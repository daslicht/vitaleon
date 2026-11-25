// SPDX-License-Identifier: GPL-3.0-or-later
// Catalog snapshots never mutate while the host reads their names.
inline int Vitaleon::numPrograms()const{auto c=std::atomic_load(&activeCatalog_);return c?int(c->names.size()):mpc::Plugin::numPrograms();}
inline const char* Vitaleon::programName(int i)const{
    auto c=std::atomic_load(&activeCatalog_);if(!c)return mpc::Plugin::programName(i);
    // The ABI copies this string immediately; a thread-local owner keeps it alive
    // even when another thread switches the catalog during that copy.
    thread_local char name[kVstMaxProgNameLen+1]{};
    if(i<0||i>=int(c->names.size())){name[0]=0;return name;}
    const auto& text=c->names[i];size_t n=std::min(text.size(),size_t(kVstMaxProgNameLen));std::memcpy(name,text.data(),n);name[n]=0;return name;
}
inline void Vitaleon::setProgram(int i){
    if(i<0||i>=numPrograms())return;
    if(i<factoryProgramCount_){mpc::Plugin::setProgram(i);return;}
    auto before=std::atomic_load(&activeCatalog_);if(before&&i<int(before->names.size())&&before->files.at(i-factoryProgramCount_).path.empty())return;
    beginStateTransaction(true);
    struct EndTransaction{Vitaleon* p;~EndTransaction(){p->endStateTransaction(true);}} transaction{this};
    auto c=std::atomic_load(&activeCatalog_);if(!c||i>=int(c->names.size())||c->files.at(i-factoryProgramCount_).path.empty())return;
    if(c==initialCatalog_){currentProgram_=i;programSelected(i);stateLoadPending_.store(true);return;}
    {std::lock_guard<std::mutex> state(stateMutex_);pendingAsset_=c->files.at(i-factoryProgramCount_);externalPresetPending_=true;stagedState_=Json();}
    currentProgram_=i;requestService();browserDisplayDirty_.store(true);stateLoadPending_.store(true);
}
inline void Vitaleon::createStorage(){
    auto c=std::make_shared<Catalog>();c->name="All installed banks";
    for(int i=0;i<mpc::Plugin::numPrograms();++i)c->names.push_back(mpc::Plugin::programName(i));
    for(int file:filePrograms_)c->files.push_back(assets_[0][file]);
    initialCatalog_=c;std::atomic_store(&activeCatalog_,std::shared_ptr<const Catalog>(c));
    const char* commands[]={"Drives","Up Folder","Refresh Drives","Previous Files","Next Files","Installed Banks","Open Row 1","Open Row 2","Open Row 3","Open Row 4","Open Row 5","Open Row 6"};
    storageCommands_=numParams();for(int i=0;i<12;++i){auto id="storage_command_"+std::to_string(i);auto p=mpc::boolParam(id.c_str(),commands[i],false);p.automatable=false;addParam(p);}
    storageRows_=numParams();for(int i=0;i<12;++i)addReadout("storage_row_"+std::to_string(i));
    storagePath_=numParams();for(int i=0;i<3;++i)addReadout("storage_path_"+std::to_string(i));
    storageStatus_=numParams();for(int i=0;i<4;++i)addReadout("storage_status_"+std::to_string(i));
    storagePage_=addReadout("storage_page");activeBankText_=numParams();for(int i=0;i<2;++i)addReadout("active_bank_"+std::to_string(i));
    storageAvailable_=numParams();for(int i=0;i<6;++i)addReadout("storage_available_"+std::to_string(i));
    browser_.entries=vitaleon_storage::discover(mpc::pluginFolder());publishBrowser();
}
inline bool Vitaleon::storageChanged(int index){
    if(index<storageCommands_||index>=storageCommands_+12)return false;
    // Every tap is one command, whatever value the toggle sends. The button goes
    // straight back to "off" and the display update that follows each command
    // shows it unlit: on the Live II the buttons used to stay lit after a tap and
    // looked like selections (user report, 0.3.18).
    publish(index,0);
    int cmd=index-storageCommands_;
    if(cmd>=6&&favouritesFiltered()){
        // The FAVS view draws its own list in these rows, so a row tap loads the favourite
        // it shows instead of opening a browser entry. Same path as the LOAD button.
        std::vector<int> files;{std::lock_guard<std::mutex> lock(favouritesMutex_);files=favouritesFiles_;}
        const int pages=std::max(1,(int(files.size())+5)/6),at=mpc::clamp(getInt(libraryPage_),0,pages-1)*6+cmd-6;
        if(at<int(files.size())){
            const int file=files[size_t(at)];
            favouriteCursor_=at;publishList(presetSelect_,float(file));
            pendingPreset_.store(file);for(int i=0;i<4;++i)publishNormalised(loadText_+i,.25f);
            requestService();refreshLibrary();browserDisplayDirty_.store(true);
        }
        return true;
    }
    std::lock_guard<std::mutex> lock(browserMutex_);
    BrowserJob job{cmd,browser_.path,browser_.root};
    if(cmd>=6){size_t row=size_t(browser_.page*6+cmd-6);if(row>=browser_.entries.size())return true;auto entry=browser_.entries[row];job.path=entry.path;if(browser_.path.empty())job.root=entry.path;job.command=entry.directory?12:13;
        if(entry.program>=0){job.command=15;job.program=entry.program;}
        else if(!entry.directory){auto ext=std::filesystem::path(entry.path).extension().string();for(auto& ch:ext)ch=char(std::tolower((unsigned char)ch));if(ext==".vital")job.command=14;}}
    if(browserJobs_.size()>=16)return true;
    browserJobs_.push_back(std::move(job));browserPending_.fetch_add(1);return true;
}
inline void Vitaleon::publishBrowser(){
    static std::atomic<unsigned> serial{0};float marker=float((++serial)%16000000)/16000000;
    for(int p=storageRows_;p<numParams()&&p<=activeBankText_+1;++p)publishNormalised(p,marker);
    // Three states per row: 0 empty, 1 OPEN (drive, folder, bank), 2 LOAD (sound).
    // While the favourites view is on its own list owns these rows (refreshLibrary).
    if(!favouritesFiltered())for(int row=0;row<6;++row){const int n=browser_.page*6+row;float state=0;
        if(n<int(browser_.entries.size()))state=browser_.entries[size_t(n)].sound?1.0f:0.5f;
        publishNormalised(storageAvailable_+row,state);}
    browserDisplayDirty_.store(true);
}
inline std::shared_ptr<const Vitaleon::Catalog> Vitaleon::indexBank(const std::string& path){
    BankArchive bank(path);auto c=std::make_shared<Catalog>();c->path=path;c->name=std::filesystem::path(path).stem().string();std::set<std::string> entries,used;
    for(int i=0;i<factoryProgramCount_;++i){c->names.push_back(mpc::Plugin::programName(i));used.insert(c->names.back());}
    for(const auto& e:bank.entries()){
        auto ext=std::filesystem::path(e.name).extension().string();for(auto& a:ext)a=char(std::tolower((unsigned char)a));if(ext!=".vital")continue;
        BankArchive::check(entries.insert(e.name).second,"Duplicate preset entry in bank");
        BankArchive::check(!(e.flags&1)&&(e.method==0||e.method==8)&&e.size<=64*1024*1024&&e.compressed<=64*1024*1024,"Unsupported preset entry in bank");
        BankArchive::check(c->files.size()<4095,"Bank has more than 4095 presets");
        c->files.push_back({e.name,path,e.name,std::filesystem::path(e.name).stem().string(),c->name});
    }
    BankArchive::check(!c->files.empty(),"No Vital presets in this bank");
    std::sort(c->files.begin(),c->files.end(),[](const Asset& a,const Asset& b){return a.entry<b.entry;});
    auto shorten=[](const std::string& s,size_t limit){size_t n=std::min(s.size(),limit);while(n&&n<s.size()&&(uint8_t(s[n])&0xc0)==0x80)--n;return s.substr(0,n);};
    for(const auto& asset:c->files){auto name=shorten(asset.name,24);for(int n=2;used.count(name);++n){auto suffix=" #"+std::to_string(n);name=shorten(asset.name,24-suffix.size())+suffix;}used.insert(name);c->names.push_back(name);}
    return c;
}
// A folder of loose .vital files behaves like a bank: its sounds appear in the
// MPC PRESET list. Nothing is read here except the directory itself.
inline std::shared_ptr<const Vitaleon::Catalog> Vitaleon::indexFolder(const std::string& folder){
    namespace fs=std::filesystem;auto c=std::make_shared<Catalog>();c->path=folder;c->name=fs::path(folder).filename().string();if(c->name.empty())c->name=folder;std::set<std::string> used;
    for(int i=0;i<factoryProgramCount_;++i){c->names.push_back(mpc::Plugin::programName(i));used.insert(c->names.back());}
    std::error_code ec;
    for(fs::directory_iterator it(folder,fs::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec)){
        if(!it->is_regular_file(ec)){ec.clear();continue;}auto ext=it->path().extension().string();for(auto& ch:ext)ch=char(std::tolower((unsigned char)ch));if(ext!=".vital")continue;
        BankArchive::check(c->files.size()<4095,"Folder has more than 4095 presets");
        c->files.push_back({it->path().filename().string(),it->path().string(),"",it->path().stem().string(),c->name});
    }
    BankArchive::check(!c->files.empty(),"No Vital presets in this folder");
    std::sort(c->files.begin(),c->files.end(),[](const Asset& a,const Asset& b){return a.label<b.label;});
    auto shorten=[](const std::string& s,size_t limit){size_t n=std::min(s.size(),limit);while(n&&n<s.size()&&(uint8_t(s[n])&0xc0)==0x80)--n;return s.substr(0,n);};
    for(const auto& asset:c->files){auto name=shorten(asset.name,24);for(int n=2;used.count(name);++n){auto suffix=" #"+std::to_string(n);name=shorten(asset.name,24-suffix.size())+suffix;}used.insert(name);c->names.push_back(name);}
    return c;
}
inline void Vitaleon::installCatalog(std::shared_ptr<const Catalog> c,bool lifecycleHeld){
    // Serialize with program selection/project transactions, but do not park DSP:
    // browsing and changing the list must not interrupt the sounding voice.
    std::unique_lock<std::mutex> life(lifecycleMutex_,std::defer_lock);if(!lifecycleHeld)life.lock();
    // A bank change preserves the current sound. Keep its slot only when its
    // stable path/entry still belongs to this catalog; otherwise mark no slot.
    int selected=currentProgram_.load();if(selected<0||selected>=factoryProgramCount_)selected=-1;
    {std::lock_guard<std::mutex> state(stateMutex_);
        if(!selectedAsset_.path.empty())selected=-1;
        else for(int i=0;i<factoryProgramCount_;++i)if(loadedName_==mpc::Plugin::programName(i))selected=i;
        for(size_t i=0;i<c->files.size();++i)if(!selectedAsset_.path.empty()&&c->files[i].path==selectedAsset_.path&&c->files[i].entry==selectedAsset_.entry)selected=factoryProgramCount_+int(i);
    }
    if(selected<0){auto retained=std::make_shared<Catalog>(*c);selected=int(retained->names.size());retained->names.push_back("[Current sound]");retained->files.push_back({"","","","[Current sound]",c->name});c=retained;}
    std::atomic_store(&activeCatalog_,c);
    currentProgram_=selected;
    if(effect_)effect_->numPrograms=int(c->names.size());
    browserDisplayDirty_.store(true);
}
inline void Vitaleon::serviceBrowser(){
    BrowserJob job;
    {std::lock_guard<std::mutex> lock(browserMutex_);if(browserJobs_.empty())return;job=std::move(browserJobs_.front());browserJobs_.pop_front();}
    try{
        if(job.command==13||job.command==5){
            {std::lock_guard<std::mutex> lock(browserMutex_);browser_.status="Reading bank index...";publishBrowser();}
            auto c=job.command==5?initialCatalog_:indexBank(job.path);installCatalog(c);
            std::lock_guard<std::mutex> lock(browserMutex_);
            if(job.command==5)browser_.status="Installed sounds are in PRESET.";
            else{
                // An opened bank is shown like a folder: its sounds are rows with LOAD.
                std::vector<vitaleon_storage::Entry> sounds;
                for(size_t i=0;i<c->files.size();++i)sounds.push_back({c->files[i].name,job.path,false,true,int(i)});
                browser_.entries=std::move(sounds);browser_.path=job.path;browser_.root=job.root;browser_.page=0;
                browser_.status=std::to_string(c->files.size())+" sounds. Tap LOAD.";
            }
            publishBrowser();
        }else if(job.command==15){
            // A sound row of the open bank.
            auto active=std::atomic_load(&activeCatalog_);
            bool valid=active&&active->path==job.path&&job.program>=0&&size_t(job.program)<active->files.size()&&!active->files[size_t(job.program)].path.empty();
            {std::lock_guard<std::mutex> lock(browserMutex_);browser_.status=valid?"Loading sound...":"Open the bank again.";publishBrowser();}
            if(valid)setProgram(factoryProgramCount_+job.program);
        }else if(job.command==14){
            // Single .vital file: list its folder in PRESET and load the tapped sound.
            {std::lock_guard<std::mutex> lock(browserMutex_);browser_.status="Loading sound...";publishBrowser();}
            auto c=indexFolder(std::filesystem::path(job.path).parent_path().string());installCatalog(c);
            auto active=std::atomic_load(&activeCatalog_);
            for(size_t i=0;i<active->files.size();++i)if(active->files[i].path==job.path){setProgram(factoryProgramCount_+int(i));break;}
        }else if(job.command==3||job.command==4){
            std::lock_guard<std::mutex> lock(browserMutex_);int last=std::max(0,(int(browser_.entries.size())-1)/6);browser_.page=mpc::clamp(browser_.page+(job.command==4?1:-1),0,last);publishBrowser();
        }else{
            if(job.command==0||job.command==2){job.path.clear();job.root.clear();}
            if(job.command==1){if(job.path==job.root){job.path.clear();job.root.clear();}else job.path=std::filesystem::path(job.path).parent_path().string();}
            auto entries=job.path.empty()?vitaleon_storage::discover(mpc::pluginFolder()):vitaleon_storage::list(job.path,job.root);
            std::lock_guard<std::mutex> lock(browserMutex_);browser_.entries=std::move(entries);browser_.path=job.path;browser_.root=job.root;browser_.page=0;browser_.status=browser_.entries.empty()?"Nothing to open here.":browser_.path.empty()?"Choose a drive.":"OPEN a folder, LOAD a sound.";publishBrowser();
        }
    }catch(const std::exception& e){std::lock_guard<std::mutex> lock(browserMutex_);browser_.status=std::string("Cannot open: ")+e.what();publishBrowser();}
    browserPending_.fetch_sub(1);
}
inline bool Vitaleon::formatStorage(int index,std::string& out){
    if(index<storageRows_||index>activeBankText_+1)return false;
    if(index>=activeBankText_){auto c=std::atomic_load(&activeCatalog_);out=textPart(c?c->name:"Installed banks",index-activeBankText_);return true;}
    if(favouritesFiltered()){
        // The FAVS view prints its own list in the LIBRARY rows. Snapshot the files first:
        // the favourites set is written from the host thread and its lock must not nest
        // inside the browser lock.
        std::vector<int> files;{std::lock_guard<std::mutex> lock(favouritesMutex_);files=favouritesFiles_;}
        const int pages=std::max(1,(int(files.size())+5)/6),page=mpc::clamp(getInt(libraryPage_),0,pages-1);
        if(index<storageRows_+12){
            const int row=(index-storageRows_)/2,part=(index-storageRows_)%2,at=page*6+row;
            if(part==0)out=at<int(files.size())?(at==favouriteCursor_?std::string(">"):std::string("FAV")):std::string();
            else if(at<int(files.size())){
                // The kind column carries the cursor, so the name keeps the full row text.
                std::string name=assets_[0][size_t(files[size_t(at)])].name;
                if(name.size()>31){size_t head=19,tail=name.size()-9;
                    while(head>0&&(uint8_t(name[head])&0xc0)==0x80)--head;while(tail<name.size()&&(uint8_t(name[tail])&0xc0)==0x80)++tail;
                    name=name.substr(0,head)+"..."+name.substr(tail);}
                out=name;
            }else if(row==0)out="No favourites yet";
            return true;
        }
        if(index>=storagePath_&&index<storagePath_+3){out=index==storagePath_?std::to_string(files.size())+" favourites":std::string();return true;}
        if(index==storagePage_){out=std::to_string(page+1)+" / "+std::to_string(pages);return true;}
        if(index>=storageStatus_&&index<storageStatus_+4){std::lock_guard<std::mutex> lock(browserMutex_);out=textPart(browser_.status,index-storageStatus_);return true;}
        return true;
    }
    std::lock_guard<std::mutex> lock(browserMutex_);
    if(index<storageRows_+12){
        // Two columns per row: the kind, and the name in ONE label (the two halves of
        // a split name used to stand apart with a gap in between).
        int row=(index-storageRows_)/2,part=(index-storageRows_)%2,n=browser_.page*6+row;out.clear();
        if(n<int(browser_.entries.size())){const auto& e=browser_.entries[size_t(n)];
            if(part==0)out=e.directory?(browser_.path.empty()?"DRIVE":"FOLDER"):e.sound?"SOUND":"BANK";
            else{std::string name=e.directory||e.program>=0?e.name:std::filesystem::path(e.name).stem().string();
                if(name.size()>31){size_t head=19,tail=name.size()-9;
                    while(head>0&&(uint8_t(name[head])&0xc0)==0x80)--head;while(tail<name.size()&&(uint8_t(name[tail])&0xc0)==0x80)++tail;
                    name=name.substr(0,head)+"..."+name.substr(tail);}
                out=name;}
        }
    }
    else if(index>=storagePath_&&index<storagePath_+3){
        // One label: the last 31 characters of the place, cut at a folder boundary where possible.
        std::string place=browser_.path.empty()?"Drives":browser_.path;
        if(place.size()>31){size_t cut=place.size()-28;while(cut<place.size()&&(uint8_t(place[cut])&0xc0)==0x80)++cut;size_t slash=place.find('/',cut);if(slash!=std::string::npos&&slash+1<place.size()&&place.size()-slash<=28)cut=slash;place="..."+place.substr(cut);}
        out=index==storagePath_?place:std::string();
    }
    else if(index>=storageStatus_&&index<storageStatus_+4){
        // The page cell is the wheel now, so the drive browser's page reads in the status
        // line instead of a label that no longer exists.
        std::string message=browser_.status+" Page "+std::to_string(browser_.page+1)+"/"+std::to_string(std::max(1,(int(browser_.entries.size())+5)/6));
        out=textPart(message,index-storageStatus_);
    }
    else if(index==storagePage_)out=std::to_string(browser_.page+1)+" / "+std::to_string(std::max(1,(int(browser_.entries.size())+5)/6))+" - "+std::to_string(browser_.entries.size())+" items";
    return true;
}
