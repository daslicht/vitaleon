// SPDX-License-Identifier: GPL-3.0-or-later
#include "SurfaceMap.h"
inline void Vitaleon::createNavigation(){
    auto specification=Json::parse(surfaceMapJson);
    auto find=[&](std::string name){for(int i=0;i<numParams();++i)if(param(i).name==name)return i;std::replace(name.begin(),name.end(),'_',' ');for(int i=0;i<numParams();++i)if(param(i).name==name)return i;throw std::runtime_error("Unknown surface parameter: "+name);};
    rememberedPages_.fill(-1);
    for(const auto& page:specification){SurfaceView view{page.at("group"),page.at("member"),{}};view.hidden=page.value("hidden",false);
        if(page.at("chart").is_string()){const std::string chart=page.at("chart");const int at=int(surfaceViews_.size());
            if(chart=="mxtable")matrixView_=at;else if(chart=="pickgroup")pickGroupView_=at;else if(chart=="picktarget")pickTargetView_=at;}for(const auto& section:page.at("sections")){std::vector<int> row;for(const auto& item:section.at("items"))row.push_back(find(item));view.sections.push_back(std::move(row));}if(rememberedPages_[view.group]<0&&!view.hidden)rememberedPages_[view.group]=int(surfaceViews_.size());surfaceViews_.push_back(std::move(view));}
    rememberedSections_.assign(surfaceViews_.size(),0);
    auto integer=[&](const char* id,const char* name,int max){auto p=mpc::intParam(id,name,0,max,0);p.automatable=false;return addParam(p);};
    uiGroup_=integer("ui_group","UI Group",6);uiPage_=integer("ui_page","UI Page",int(surfaceViews_.size())-1);uiSection_=integer("ui_section","UI Section",31);
    uiKnobs_=numParams();for(int i=0;i<16;++i){auto id="ui_knob_"+std::to_string(i),name="UI Control "+std::to_string(i+1);auto p=mpc::floatParam(id.c_str(),name.c_str(),0,1,0);p.automatable=false;addParam(p);uiTargets_[i].store(-1);slotAutomated_[i].store(-1);}
    // Appended after every existing parameter (index 1855). A momentary trigger for
    // the PICK buttons of the matrix rows: the plugin resets it to 0 at once, so
    // the same row can be picked again (a radio button that is already lit sends nothing).
    uiPick_=integer("ui_pick","UI Pick",64);
    // Appended (index 1856): the Q-Link group of four last turned, 0 = slots 1-4.
    // The host does not tell a plugin which group its knobs are banked to.
    uiBank_=integer("ui_bank","UI Bank",3);
    // Groups own their Q-Links (0.3.34): the Force tab bar gives every group its
    // own Q-Links.json entry, so the knob that moves names its group and the
    // plugin needs no page parameter from the skin to know what is on screen.
    for(int g=0;g<7;++g){
        // A strip button is one step of this parameter: the host quantises it
        // into numButtonsInGroup steps (SDK ui-reference 6.4). With the page index
        // space of every view (0..39) the four OSC buttons wrote step 13, so
        // groupMemberPage() fell back to page 1 and every OSC page, and every
        // Q-Link slot on it, edited oscillator 1 whatever the strip showed.
        int pages=0;for(const auto& view:surfaceViews_)if(view.group==g)++pages;
        auto p=mpc::intParam(("ui_gpage_"+std::to_string(g)).c_str(),("Group "+std::to_string(g)+" Page").c_str(),0,std::max(1,pages)-1,0);p.automatable=false;groupPage_[g]=addParam(p);
        for(int i=0;i<9;++i){
            auto s=mpc::floatParam(("ui_g"+std::to_string(g)+"_slot_"+std::to_string(i)).c_str(),("Group "+std::to_string(g)+" Slot "+std::to_string(i+1)).c_str(),0,1,0);s.automatable=false;
            const int at=addParam(s);if(i==0)groupSlot_[g]=at;
        }
    }
    // LFO and OTHER share the MOD tab, so its strip needs one parameter: a
    // single value lights exactly one button and defaults to LFO 1.
    {auto m=mpc::intParam("ui_mod_page","MOD Page",0,12,0);m.automatable=false;uiModPage_=addParam(m);}
    // Legacy slot (0.3.38): the section strip first wrote one 56-step handle for
    // every group. A 56-step group with only seven buttons on a page made the host
    // write slots no page has, so each group now owns its own handle below. The
    // slot stays so no later parameter index moves; nothing reads it.
    {auto s=mpc::intParam("ui_section_tab","Group Section",0,55,0);s.automatable=false;uiSectionTab_=addParam(s);}
    updateNavigation();
}
inline void Vitaleon::createGroupSections(){
    // Appended after every real parameter (Table Step is the last one) so no
    // existing slot index moves. One 8-step handle per group: the ids its section
    // buttons write are the section numbers 0..6 again, the pattern the Force
    // handled before the table became a section, and a tap still names its group
    // because a different handle is written per group.
    for(int g=0;g<7;++g){
        auto p=mpc::intParam(("ui_gsec_"+std::to_string(g)).c_str(),("Group "+std::to_string(g)+" Section").c_str(),0,7,0);
        p.automatable=false;groupSection_[g]=addParam(p);
    }
}
inline void Vitaleon::updateNavigation(){
    int page=mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1);const auto& v=surfaceViews_[page];publish(uiGroup_,float(v.group));if(!v.hidden)rememberedPages_[v.group]=page;
    int section=mpc::clamp(getInt(uiSection_),0,int(v.sections.size())-1);publish(uiSection_,float(section));
    // A group's section buttons write that group's 8-step handle and its content
    // conditions read the same one, so a tap switches the section without the
    // plugin. This only steps in when the section moved another way (the table
    // opens, a project is restored): then the strip still shows the old step.
    if(groupSection_[v.group]>=0){
        const int step=mpc::clamp(section,0,7);
        if(step!=getInt(groupSection_[v.group])){publish(groupSection_[v.group],float(step));if(ready_)notifyHostParameterChanged(groupSection_[v.group]);}
    }
    // A page keeps the subtab it was left on: OSC 1 > PHASE > OSC 2 > OSC 1 lands
    // on PHASE again instead of jumping back to SOUND.
    if(!v.hidden&&size_t(page)<rememberedSections_.size())rememberedSections_[size_t(page)]=section;
    if(v.group==3)publish(envSelect_,float(v.member));
    if(v.group==4){publish(lfoSelect_,float(v.member));publish(curveTarget_,0);curveSelectionChanged_.store(true);inspectCurve_.store(true);}
    if(v.group==6&&v.member==1){publish(curveTarget_,1);curveSelectionChanged_.store(true);inspectCurve_.store(true);}
    // Mirror the strip so the tab bar shows exactly one selected subtab.
    {int modPage=-1;
     if(v.group==4)modPage=tabMemberIndex(page);
     else if(v.group==6)modPage=v.hidden?12:8+tabMemberIndex(page);
     if(modPage>=0&&uiModPage_>=0)publish(uiModPage_,float(modPage));}
    const auto& targets=v.sections[section];for(int i=0;i<16;++i)uiTargets_[i].store(i<int(targets.size())?targets[i]:-1);
    slotEditMs_.store(0,std::memory_order_relaxed); // other controls now: every slot shows its new target
    // 0.3.36: every oscillator page carries its table list as its seventh section.
    // The page names the oscillator LOAD writes, and entering the section puts the
    // cursor on the table that oscillator already uses.
    if(v.group==1&&v.member>=1&&tableViewTarget_>=0){
        const int o=mpc::clamp(v.member-1,0,2);publish(tableViewTarget_,float(o));
        if(section==6&&tableRowStates_>=0){
            const int count=std::max(1,tableCount());
            const int selected=mpc::clamp(getInt(tableSelect_[o]),0,count-1);
            showTableCursor(selected);
            publishTables();
        }
    }
    refreshProxies();browserDisplayDirty_.store(true);
}
inline int Vitaleon::groupMemberPage(int group,int member){
    // Group pages count members inside the group, as the tab bar does.
    int k=0,first=-1;
    for(int i=0;i<int(surfaceViews_.size());++i)if(surfaceViews_[i].group==group&&!surfaceViews_[i].hidden){
        if(first<0)first=i;if(k==member)return i;++k;}
    return first<0?0:first;
}
inline int Vitaleon::tabMemberIndex(int page){
    const int group=surfaceViews_[page].group;int k=0;
    for(int i=0;i<page;++i)if(surfaceViews_[i].group==group&&!surfaceViews_[i].hidden)++k;
    return k;
}
// The strip handle is written by the host even when its callback never arrives, so
// the plugin takes the section it holds before a knob maps its slot.
inline void Vitaleon::syncSectionFromStrip(){
    if(uiPage_<0||uiSection_<0)return;
    const int page=mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1);
    if(page==matrixView_||page==pickGroupView_||page==pickTargetView_)return;
    const auto& v=surfaceViews_[page];
    if(v.group<0||v.group>6||groupSection_[v.group]<0||v.sections.size()<=1)return;
    const int shown=mpc::clamp(getInt(groupSection_[v.group]),0,int(v.sections.size())-1);
    if(shown==mpc::clamp(getInt(uiSection_),0,31))return;
    publish(uiSection_,float(shown));
    std::lock_guard<std::mutex> lock(navigationMutex_);
    rememberedSections_[size_t(page)]=shown;
    const auto& targets=v.sections[size_t(shown)];
    for(int i=0;i<16;++i)uiTargets_[i].store(i<int(targets.size())?targets[i]:-1);
}
inline bool Vitaleon::applySlot(int slot,float plain){
    // A section the host switched without calling back would still hold the old
    // section here and the knob would edit that section's parameter.
    syncSectionFromStrip();
    int target=uiTargets_[slot].load();if(target<0)return true;
    // Record the stable sound parameter, never a page-dependent UI slot.
    int automated=target;
    if(target>=homeControls_&&target<homeControls_+7)automated=homeTarget(target-homeControls_);
    if(target==homeFilter_)automated=controlIndex("filter_1_on");
    if(target==advancedValue_)automated=advancedTargets_[getInt(advancedSelector_)];
    if(target==effectType_)automated=effectOrder_;
    for(const auto& bank:banks_)for(size_t k=0;k<bank.proxies.size();++k)if(target==bank.proxies[k])automated=bank.targets[mpc::clamp(getInt(bank.selector)-1,0,int(bank.targets.size())-1)][k];
    // Knobs and faders on screen are bound to this very slot (0.3.22), so the
    // host moves the control it wrote and nothing has to be redrawn.
    const float before=get(target);
    setNormalised(target,plain);
    if(param(automated).automatable)slotAutomated_[size_t(slot)].store(automated,std::memory_order_relaxed);
    // A switch or list on screen is bound to its own parameter: redraw it when it steps.
    if(param(target).kind!=mpc::Param::Kind::Float&&get(target)!=before)slotStepped_.store(target,std::memory_order_relaxed);
    const int bank=slot/4;
    if(uiBank_>=0&&getInt(uiBank_)!=bank){publish(uiBank_,float(bank));bankChanged_.store(true,std::memory_order_relaxed);}
    slotEditMs_.store(nowMs(),std::memory_order_relaxed);
    return true;
}
// LFO and OTHER share the MOD tab, so one Q-Link bank serves both groups.
static constexpr int kTabBank[7]={0,1,2,3,4,5,4};
inline bool Vitaleon::navigationChanged(int index,float plain){
    if(uiGroup_<0)return false;
    if(index==uiModPage_){
        // k < 8: LFO 1..8, k = 8..11: MATRIX, RANDOM, EXPR, CURVE.
        const int k=mpc::clamp(int(std::lround(plain)),0,12);
        const int page=k<8?groupMemberPage(4,k):(k<12?groupMemberPage(6,k-8):groupMemberPage(6,0));
        std::lock_guard<std::mutex> lock(navigationMutex_);
        publish(uiGroup_,float(surfaceViews_[page].group));publish(uiPage_,float(page));publish(uiSection_,float(rememberedSections_[size_t(page)]));
        updateNavigation();return true;}
    if(index>=uiKnobs_&&index<uiKnobs_+16)return applySlot(index-uiKnobs_,plain);
    // A group slot carries its group: the hardware knob that moved tells the plugin
    // which tab is on screen, so no skin page state is involved.
    for(int g=0;g<7;++g){
        if(index==groupPage_[g]){
            std::lock_guard<std::mutex> lock(navigationMutex_);
            const int page=groupMemberPage(g,int(std::lround(plain)));
            publish(uiGroup_,float(g));publish(uiPage_,float(page));publish(uiSection_,float(rememberedSections_[size_t(page)]));updateNavigation();return true;}
        if(groupSlot_[g]>=0&&index>=groupSlot_[g]&&index<groupSlot_[g]+9){
            const int page=groupMemberPage(g,getInt(groupPage_[g]));
            // Two groups on one tab share its bank: a write there must not pull
            // the screen to the other group's remembered page.
            const int visible=mpc::clamp(surfaceViews_[mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1)].group,0,6);
            if(kTabBank[visible]!=kTabBank[g]&&(getInt(uiPage_)!=page||getInt(uiGroup_)!=g)){
                std::lock_guard<std::mutex> lock(navigationMutex_);
                publish(uiGroup_,float(g));publish(uiPage_,float(page));publish(uiSection_,float(rememberedSections_[size_t(page)]));updateNavigation();}
            return applySlot(index-groupSlot_[g],plain);}
    }
    if(index==uiPick_){const int route=int(std::lround(plain));
        if(route>0&&matrixView_>=0&&getInt(uiPage_)==matrixView_){publish(uiPick_,0);set(routeSelect_,float(route));navigateTo(pickGroupView_,0);}
        return true;}
    // A section button writes the 8-step handle of its own group, so the tap names
    // the group and never a stale page: resolve the page first, then let
    // updateNavigation clamp the section to it.
    for(int g=0;g<7;++g)if(index==groupSection_[g]){
        const int section=mpc::clamp(int(std::lround(plain)),0,7);
        // The host echoes the value the plugin published to keep the strip lit. That
        // echo is not a tap: resolving a page from it would pull the screen around.
        const auto& shown=surfaceViews_[mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1)];
        if(shown.group==g&&getInt(uiSection_)==section)return true;
        std::lock_guard<std::mutex> lock(navigationMutex_);
        int page;
        if(g==4||g==6){
            // LFO and OTHER share the MOD tab, whose strip is one parameter.
            const int mod=mpc::clamp(getInt(uiModPage_),0,12);
            page=mod<8?groupMemberPage(4,mod):(mod<12?groupMemberPage(6,mod-8):groupMemberPage(6,0));
            if(surfaceViews_[page].group!=g)page=groupMemberPage(g,0);
        }else page=groupMemberPage(g,getInt(groupPage_[g]));
        publish(uiGroup_,float(g));publish(uiPage_,float(page));publish(uiSection_,float(section));
        updateNavigation();return true;}
    if(index!=uiGroup_&&index!=uiPage_&&index!=uiSection_)return false;
    std::lock_guard<std::mutex> lock(navigationMutex_);
    if(index==uiGroup_){const int page=mpc::clamp(rememberedPages_[mpc::clamp(int(std::lround(plain)),0,6)],0,int(surfaceViews_.size())-1);publish(uiPage_,float(page));publish(uiSection_,float(rememberedSections_[size_t(page)]));}
    // Entering the matrix shows the page of the selected route.
    else if(index==uiPage_){const int page=mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1);
        publish(uiSection_,page==matrixView_?float((getInt(routeSelect_)-1)/8):float(rememberedSections_[size_t(page)]));}
    // Target picker step 1 -> 2: a group button sets the section, the entry view shows that group.
    else if(index==uiSection_&&pickGroupView_>=0&&getInt(uiPage_)==pickGroupView_&&std::lround(plain)>0)publish(uiPage_,float(pickTargetView_));
    updateNavigation();return true;
}
inline void Vitaleon::navigateTo(int page,int section){
    if(page<0||uiPage_<0)return;
    std::lock_guard<std::mutex> lock(navigationMutex_);publish(uiPage_,float(page));publish(uiSection_,float(section));updateNavigation();
}
inline void Vitaleon::refreshNavigation(){
    if(uiKnobs_<0)return;
    const bool moving=slotEditMs_.load(std::memory_order_relaxed)!=0;
    for(int i=0;i<16;++i){int target=uiTargets_[i].load();
        if(target<0){publishNormalised(uiKnobs_+i,0);continue;}
        // While it moves, a stepped target keeps the knob's own position inside its
        // step: writing the step's value back would undo the small increments of a
        // turning Q-Link. At rest, and after a page change, the slot shows the target.
        const auto& t=param(target);
        if(moving&&t.kind!=mpc::Param::Kind::Float&&t.toPlain(getNormalised(uiKnobs_+i))==get(target))continue;
        publishNormalised(uiKnobs_+i,getNormalised(target));}
    // The tab on screen shows the slots of its group; publish them alongside.
    if(uiPage_>=0){const int g=mpc::clamp(surfaceViews_[mpc::clamp(getInt(uiPage_),0,int(surfaceViews_.size())-1)].group,0,6);
        // The tab owns the bank, not the group: MOD publishes into the LFO bank
        // so its matrix knobs move the values the host actually reads.
        const int bank=kTabBank[g];
        if(groupSlot_[bank]>=0)for(int i=0;i<9;++i){const int target=uiTargets_[i].load();
            publishNormalised(groupSlot_[bank]+i,target<0?0.0f:getNormalised(target));}}
}
// Service thread, no lock held. See slotAutomated_.
inline void Vitaleon::flushSlotNotifications(){
    if(uiKnobs_<0)return;
    const int stepped=slotStepped_.exchange(-1,std::memory_order_relaxed);if(stepped>=0)notifyHostParameterChanged(stepped);
    // One parameter, not the page: the frame follows at once where the host redraws
    // on this call; the page redraw at rest covers a host that does not.
    if(bankChanged_.exchange(false,std::memory_order_relaxed)){notifyHostParameterChanged(uiBank_);bankRedraw_.store(true,std::memory_order_relaxed);}
    const long long last=slotEditMs_.load(std::memory_order_relaxed);
    if(last==0||nowMs()-last<kSlotRestMs)return;
    for(auto& slot:slotAutomated_){const int sound=slot.exchange(-1,std::memory_order_relaxed);if(sound>=0)notifyHostParameterChanged(sound);}
    if(bankRedraw_.exchange(false,std::memory_order_relaxed))requestDisplayUpdate();
    long long expected=last;slotEditMs_.compare_exchange_strong(expected,0,std::memory_order_relaxed);
}
