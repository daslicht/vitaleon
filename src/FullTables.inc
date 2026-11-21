// SPDX-License-Identifier: GPL-3.0-or-later
// Table picker (0.3.35, user request). The wavetable list comes from the Content
// folder at run time, so its names can never be generated labels: the view fills
// readouts and the rows are buttons. The data wheel turns Table Cursor and the
// entry is loaded once the wheel rests, so a fast spin parses only the table it
// lands on. The list itself reads that same cursor, so nothing can lag behind.
inline std::string Vitaleon::tableText(const std::string& text,size_t limit){
    size_t end=std::min(text.size(),limit);
    while(end>0&&end<text.size()&&(uint8_t(text[end])&0xc0)==0x80)--end;
    return text.substr(0,end);
}
inline int Vitaleon::tableCount(){std::lock_guard<std::mutex> tables(tableMutex_);return int(assets_[1].size());}
inline int Vitaleon::tablePageCount(){return std::max(1,(tableCount()+kTableRows-1)/kTableRows);}
inline std::string Vitaleon::tableFolder(const Asset& asset){
    const size_t slash=asset.label.find_last_of('/');
    if(slash==std::string::npos)return tableText(asset.bank,28);
    std::string folder=asset.label.substr(0,slash);
    while(!folder.empty()&&folder.back()==' ')folder.pop_back();
    return tableText(folder,28);
}
inline void Vitaleon::createTables(){
    auto integer=[&](const char* id,const char* name,int max){auto p=mpc::intParam(id,name,0,std::max(1,max),0);p.automatable=false;return addParam(p);};
    tableViewTarget_=integer("table_view_target","Table Oscillator",2);
    // Kept as a parameter instead of a member: it is already saved in projects
    // and automated by hosts. Deleting it would shift the index of every
    // parameter after it and silently retarget old automation.
    tableViewPage_=integer("table_view_page","Table Page",511);
    // Legacy slot: the knob wrote this 0..1 cursor before the step handle existed. It
    // stays so no later parameter index moves; nothing reads or writes it any more.
    {auto p=mpc::floatParam("table_view_cursor","Table Cursor",0,1,0);p.automatable=false;addParam(p);}

    tableOpen_=numParams();
    for(int i=0;i<3;++i){auto p=mpc::boolParam(("table_open_"+std::to_string(i+1)).c_str(),("Open Osc "+std::to_string(i+1)+" Table").c_str(),false);p.automatable=false;addParam(p);}
    const char* commands[]={"Table Previous Page","Table Next Page","Table Reload","Table Built-in"};
    tableCommands_=numParams();
    for(int i=0;i<4;++i){auto p=mpc::boolParam(("table_command_"+std::to_string(i)).c_str(),commands[i],false);p.automatable=false;addParam(p);}
    tableRows_=numParams();
    for(int i=0;i<kTableRows;++i){auto p=mpc::boolParam(("table_row_"+std::to_string(i)).c_str(),("Select Table Row "+std::to_string(i+1)).c_str(),false);p.automatable=false;addParam(p);}
    tableRowStates_=numParams();for(int i=0;i<kTableRows;++i)addReadout("table_row_"+std::to_string(i)+"_state");
    tableRowNames_=numParams();for(int i=0;i<kTableRows;++i)addReadout("table_row_"+std::to_string(i)+"_name");
    tableRowFolders_=numParams();for(int i=0;i<kTableRows;++i)addReadout("table_row_"+std::to_string(i)+"_folder");
    tablePosition_=addReadout("table_position");
    tableStatus_=numParams();for(int i=0;i<2;++i)addReadout("table_status_"+std::to_string(i));
    tableNameReadout_=numParams();for(int i=0;i<3;++i)addReadout("display_table_name_"+std::to_string(i+1));
    tableCursor_=0;
}
// TEMPORARY (user request, to be removed): four readouts the OSC page shows so a
// wrong page can be read off the device — which step the section strip holds, which
// page/section the plugin picked, and what the host last handed UI Section.
inline void Vitaleon::createDebug(){
    auto readout=[&](const char* id){auto p=mpc::intParam(id,id,0,4095,0);p.automatable=false;return addParam(p);};
    debugStrip_=readout("debug_strip");debugPage_=readout("debug_page");
    debugSection_=readout("debug_section");debugUiSec_=readout("debug_uisec");
    debugLine_=readout("debug_line");
}
// TEMPORARY: every callback the plugin actually receives, before any decision.
// A tap that leaves no EVT line never reached the plugin at all.
inline void Vitaleon::logEvent(const char* tag,int index,float plain){
    const int page=uiPage_>=0?mpc::clamp(getInt(uiPage_),0,4095):-1;
    {std::lock_guard<std::mutex> lock(debugLogMutex_);
     std::ofstream out("/tmp/vitaleon-sections.log",std::ios::app);
     if(out)out<<"t="<<std::time(nullptr)<<" EVT "<<tag<<" idx="<<index<<" val="<<int(std::lround(plain))<<" page="<<page<<"\n";}
}
// TEMPORARY: one line per section change, readable over ssh from the device.
inline void Vitaleon::logSection(int page,int section){
    const int group=page>=0&&page<int(surfaceViews_.size())?surfaceViews_[size_t(page)].group:-1;
    const int strip=group>=0&&groupSection_[group]>=0?getInt(groupSection_[group]):-1;
    const int ui=uiSection_>=0?mpc::clamp(getInt(uiSection_),0,31):-1;
    {std::lock_guard<std::mutex> lock(debugLogMutex_);
     if(page==debugLastPage_&&section==debugLastSection_&&strip==debugLastStrip_&&ui==debugLastUi_)return;
     debugLastPage_=page;debugLastSection_=section;debugLastStrip_=strip;debugLastUi_=ui;
     std::ofstream out("/tmp/vitaleon-sections.log",std::ios::app);
     if(out)out<<"t="<<std::time(nullptr)<<" STRIP "<<strip<<" PAGE "<<page<<" SEC "<<section<<" UISEC "<<ui<<"\n";}
}
inline bool Vitaleon::formatDebug(int index,std::string& out){
    if(index==debugStrip_){out="STRIP "+std::to_string(getInt(debugStrip_));return true;}
    if(index==debugPage_){out="PAGE "+std::to_string(getInt(debugPage_));return true;}
    if(index==debugSection_){out="SEC "+std::to_string(getInt(debugSection_));return true;}
    if(index==debugUiSec_){out="UISEC "+std::to_string(getInt(debugUiSec_));return true;}
    if(index==debugLine_){out="STRIP "+std::to_string(getInt(debugStrip_))+" PAGE "+std::to_string(getInt(debugPage_))+" SEC "+std::to_string(getInt(debugSection_))+" UISEC "+std::to_string(getInt(debugUiSec_));return true;}
    return false;
}
inline void Vitaleon::publishTables(){
    if(tableRowStates_<0)return;
    const int count=tableCount();
    const int pages=tablePageCount();
    const int cursor=mpc::clamp(tableCursor_,0,std::max(0,count-1));
    const int page=mpc::clamp(getInt(tableViewPage_),0,pages-1);
    // A fresh value in every readout makes the host ask for its text again.
    static std::atomic<unsigned> serial{0};
    const float marker=float((++serial)%16000000)/16000000;
    for(int p=tableRowStates_;p<numParams()&&p<tableNameReadout_+3;++p)publishNormalised(p,marker);
    for(int row=0;row<kTableRows;++row){
        const int at=page*kTableRows+row;
        if(at>=count){publishNormalised(tableRowStates_+row,0);continue;}
        publishNormalised(tableRowStates_+row,at==cursor?1.0f:0.5f);
    }
    for(int o=0;o<3;++o){
        const int selected=mpc::clamp(getInt(tableSelect_[o]),0,std::max(0,count-1));
        publishNormalised(tableNameReadout_+o,float(selected+1)/float(std::max(1,count)));
    }
}
// Appended after every other parameter, so no existing slot index moves.
inline void Vitaleon::createTableStep(){
    // 0.3.37: the wheel control. A plain parameter is turned by 1/100 of its range
    // per click and its own steps are ignored, so a fraction of the cursor had to be
    // turned back into an entry after every click and the host kept its own position.
    // The skin gives this one kTableSteps replacementStrings: the value is the entry,
    // one click is exactly one table and the knob cannot drift against the list.
    auto s=mpc::intParam("table_view_step","Table Step",0,kTableSteps-1,0);s.automatable=false;tableViewStep_=addParam(s);
}
// Every cursor control — the step knob, a row button, BUILT-IN, a loaded project —
// lands here: the step handle and the page follow the entry.
inline void Vitaleon::showTableCursor(int entry){
    if(tableRowStates_<0)return;
    tableCursor_=mpc::clamp(entry,0,std::max(0,tableCount()-1));
    if(tableViewStep_>=0)publish(tableViewStep_,float(mpc::clamp(tableCursor_,0,kTableSteps-1)));
    if(tableViewPage_>=0)publish(tableViewPage_,float(tableCursor_/kTableRows));
}
inline void Vitaleon::tableReload(){
    // Content/Wavetables may have gained files. Tables inside a bank stay as they
    // were; a bank entry can only change by re-reading the bank.
    std::vector<Asset> files{{"Built-in / current","","","Built-in / current",""}};
    const auto root=std::filesystem::path(mpc::contentPath("Wavetables"));std::error_code ec;
    if(std::filesystem::exists(root,ec))for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end&&files.size()<4096;it.increment(ec)){
        if(ec){ec.clear();continue;}if(!it->is_regular_file(ec))continue;
        std::string extension=it->path().extension().string();for(auto& c:extension)c=char(std::tolower((unsigned char)c));
        if(extension==".vitaltable"||extension==".wav")files.push_back({it->path().lexically_relative(root).generic_string(),it->path().string()});
    }
    std::sort(files.begin()+1,files.end(),[](const Asset& a,const Asset& b){return a.label<b.label;});
    for(auto& asset:files){
        asset.name=asset.path.empty()?asset.label:std::filesystem::path(asset.path).stem().string();
        asset.bank="Loose tables";
    }
    {std::lock_guard<std::mutex> tables(tableMutex_);assets_[1].swap(files);}
    for(int o=0;o<3;++o){
        const int last=std::max(0,tableCount()-1);
        if(getInt(tableSelect_[o])>last)publishList(tableSelect_[o],float(last));
    }
    // A reload can drop the entry the cursor points at.
    showTableCursor(tableCursor_);
    {std::lock_guard<std::mutex> state(stateMutex_);loadMessage_="Tables: "+std::to_string(tableCount());}
}
inline bool Vitaleon::tableChanged(int index,float plain){
    (void)plain;
    if(tableOpen_<0)return false;
    if(index>=tableOpen_&&index<tableOpen_+3){
        publish(index,0);
        const int o=index-tableOpen_;
        const int count=std::max(1,tableCount());
        const int selected=mpc::clamp(getInt(tableSelect_[o]),0,count-1);
        publish(tableViewTarget_,float(o));
        showTableCursor(selected);
        // 0.3.36: the list is the oscillator page's seventh section, not a page.
        navigateTo(groupMemberPage(1,o),6);
        publishTables();browserDisplayDirty_.store(true);return true;
    }
    // The discrete knob: its value is the entry, so no fraction has to be decoded.
    if(index==tableViewStep_){
        showTableCursor(getInt(tableViewStep_));
        // Load once the wheel rests, so a spin parses only the table it lands on.
        tableLoadSlot_.store(mpc::clamp(getInt(tableViewTarget_),0,2),std::memory_order_relaxed);
        tableLoadDue_.store(nowMs()+120,std::memory_order_relaxed);
        publishTables();browserDisplayDirty_.store(true);return true;
    }
    if(index>=tableCommands_&&index<tableCommands_+4){
        publish(index,0);
        const int command=index-tableCommands_;
        const int pages=tablePageCount();
        if(command==0||command==1)publish(tableViewPage_,float(mpc::clamp(getInt(tableViewPage_)+(command?1:-1),0,pages-1)));
        else if(command==2)tableReload();
        else{
            // Built-in: index 0 is the built-in table, at once.
            const int slot=mpc::clamp(getInt(tableViewTarget_),0,2);
            showTableCursor(0);publishList(tableSelect_[slot],0);
            tableLoadSlot_.store(slot);tableLoadDue_.store(0);
            requestService(false);
        }
        publishTables();browserDisplayDirty_.store(true);return true;
    }
    if(index>=tableRows_&&index<tableRows_+kTableRows){
        publish(index,0);
        const int slot=mpc::clamp(getInt(tableViewTarget_),0,2);
        const int page=mpc::clamp(getInt(tableViewPage_),0,tablePageCount()-1);
        const int at=page*kTableRows+(index-tableRows_);
        if(at<tableCount()){
            showTableCursor(at);
            publishList(tableSelect_[slot],float(at));
            tableLoadSlot_.store(slot);tableLoadDue_.store(0);
            requestService(false);
        }
        publishTables();browserDisplayDirty_.store(true);return true;
    }
    return false;
}
inline void Vitaleon::serviceTables(){
    long long due=tableLoadDue_.load(std::memory_order_relaxed);
    if(!due||nowMs()<due)return;
    if(!tableLoadDue_.compare_exchange_strong(due,0,std::memory_order_relaxed))return;
    const int slot=mpc::clamp(tableLoadSlot_.load(std::memory_order_relaxed),0,2);
    const int count=tableCount();if(count<=0)return;
    const int cursor=mpc::clamp(tableCursor_,0,count-1);
    if(getInt(tableSelect_[slot])!=cursor)publishList(tableSelect_[slot],float(cursor));
    requestService(false);
    publishTables();browserDisplayDirty_.store(true);
}
inline bool Vitaleon::formatTable(int index,std::string& out){
    if(tableRowStates_<0)return false;
    const int count=tableCount();
    const int pages=tablePageCount();
    const int page=mpc::clamp(getInt(tableViewPage_),0,pages-1);
    if(index>=tableRowStates_&&index<tableRowStates_+kTableRows){
        const int at=page*kTableRows+(index-tableRowStates_);
        out=at<count&&at==mpc::clamp(tableCursor_,0,std::max(0,count-1))?">":"";
        return true;
    }
    if(index>=tableRowNames_&&index<tableRowNames_+kTableRows){
        const int at=page*kTableRows+(index-tableRowNames_);
        if(at<count){std::lock_guard<std::mutex> tables(tableMutex_);const auto& asset=assets_[1][size_t(at)];out=tableText(asset.name,31);}
        return true;
    }
    if(index>=tableRowFolders_&&index<tableRowFolders_+kTableRows){
        const int at=page*kTableRows+(index-tableRowFolders_);
        if(at<count){std::lock_guard<std::mutex> tables(tableMutex_);out=tableFolder(assets_[1][size_t(at)]);}
        return true;
    }
    if(index==tablePosition_){
        out="Page "+std::to_string(page+1)+"/"+std::to_string(pages);
        if(count>0)out+="  "+std::to_string(mpc::clamp(tableCursor_,0,count-1)+1)+"/"+std::to_string(count);
        return true;
    }
    if(index>=tableStatus_&&index<tableStatus_+2){
        std::lock_guard<std::mutex> state(stateMutex_);
        const std::string message=pendingPreset_.load()>=0?"Load requested...":loadMessage_;
        size_t start=0,end=0;
        for(int part=0;part<=index-tableStatus_;++part){
            start=end;end=std::min(message.size(),start+31);
            while(end>start&&end<message.size()&&(uint8_t(message[end])&0xc0)==0x80)--end;
            if(end<message.size()){size_t space=message.rfind(' ',end-1);if(space!=std::string::npos&&space>start+14)end=space+1;}
        }
        out=message.substr(start,end-start);return true;
    }
    if(index>=tableNameReadout_&&index<tableNameReadout_+3){
        const int o=index-tableNameReadout_;
        const int selected=mpc::clamp(getInt(tableSelect_[o]),0,std::max(0,count-1));
        if(count>0&&selected>0){std::lock_guard<std::mutex> tables(tableMutex_);out=tableText(assets_[1][size_t(selected)].name,31);}
        else out="Built-in / current";
        return true;
    }
    return false;
}
