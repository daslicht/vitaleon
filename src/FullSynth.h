// SPDX-License-Identifier: GPL-3.0-or-later
// Andersson Audio, 2026-11-13. MPC control surface around the complete Vital graph.
#pragma once
#include "ControlCatalog.h"
#include "PortableAssets.h"
#include "AssetValidation.h"
#include "engine/synth_parameters.h"
#include "engine/modulation_connection_processor.h"
#include "engine/line_generator.h"
#include "engine/value_switch.h"
#include "engine/sample_source.h"
#include "wavetable/wavetable_creator.h"
#include "wavetable/wave_source.h"
#include "dsp/wav.h"
#include <thread>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <deque>
#include <random>
#include <condition_variable>
#ifdef __linux__
#include <semaphore.h>
#include <pthread.h>
#endif
#include "StorageBrowser.h"

class Vitaleon : public VitaleonLegacy {
public:
    static constexpr int FullMode=16;
    Vitaleon();
    ~Vitaleon() override;
    void prepare(double sampleRate,int maxBlock) override;
    void reset() override { resetBenchArmed_.store(!getBool(Bench));resetPending_.store(true); }
    void process(mpc::ProcessContext& ctx) override;
    void parameterChanged(int index,float plain) override;
    void saveExtraState(std::vector<uint8_t>& out) override;
    void loadExtraState(const uint8_t* data,size_t size) override;
    void stateRestored(uint32_t count,bool extra) override;
    void restoreParameter(int index,float normalized) override {publishNormalised(index,normalized);}
    void beginStateTransaction(bool writing) override;
    void endStateTransaction(bool writing) override;
    void programSelected(int index) override;
    bool formatValue(int index,float plain,std::string& out) override;
    int numPrograms() const override;
    const char* programName(int index) const override;
    void setProgram(int index) override;
    int controlIndex(const std::string& name)const { auto i=indices_.find(name);return i==indices_.end()?-1:i->second; }
    // Direct integration tests may wait for asynchronous graph/asset edits here.
    bool busy() const {return gate_.load()!=0 || inspectCurve_.load() || browserPending_.load()!=0 || requested_.load()!=completed_.load();}
#ifdef VITALEON_TEST
    std::array<double,6> testImportTimings(const nlohmann::json& data){
        std::lock_guard<std::mutex> life(lifecycleMutex_);std::lock_guard<std::mutex> state(stateMutex_);
        using Clock=std::chrono::steady_clock;std::array<double,6> times{};auto t=Clock::now();
        auto mark=[&](int i){auto now=Clock::now();times[i]=std::chrono::duration<double,std::milli>(now-t).count();t=now;};
        loadPreset(data);mark(0);syncFullControls(true,true);mark(1);connectRoutes();mark(2);engine_->checkOversampling();mark(3);warmGraph();mark(4);refreshProxies();mark(5);return times;
    }
    void testImport(const nlohmann::json& data){std::lock_guard<std::mutex> life(lifecycleMutex_);std::lock_guard<std::mutex> state(stateMutex_);fxWaitIdle();loadPreset(data);syncFullControls(true,true);connectRoutes();engine_->checkOversampling();warmGraph();refreshProxies();}
    const nlohmann::json& testAssets()const{return savedAssets_;}
    const std::vector<std::string>& testImportNotes()const{return importNotes_;}
    int testTableCount(){return tableCount();}
    int testTableCursor()const{return tableCursor_;}
    // The Init sound (every control at its default), as the "Init" program selects it.
    void testInit(){beginStateTransaction(true);for(size_t k=0;k<initValues_.size();++k)restoreParameter(int(k),param(int(k)).toNormalised(initValues_[k]));selectInit();endStateTransaction(true);reset();}
    // Deterministic deck order for tests/full_checks.cpp; the dice itself seeds from
    // std::random_device once per instance.
    void testSeedRandom(uint32_t seed){randomRng_.seed(seed);randomDeck_.clear();randomDeckStamp_=0;randomDrawn_=0;}
    // Engine checks start from the Init state instead of the first installed sound.
    void testNoStartSound(){startSound_.store(false);}
    bool testEngineSwitch(const std::string& key)const{for(const auto& b:bindings_)if(b.key==key)return b.value->value()!=0;return false;}
#endif
private:
    using Json=nlohmann::json;
    struct Binding {int index;vital::Value* value;std::string key;float last=-100;bool structural=false;};
    struct ProxyBank {int selector;std::vector<int> proxies;std::vector<std::vector<int>> targets;int selected=-1;};
    struct Route {int source,destination;bool connected=false;vital::modulation_change change{};};
    struct Asset {std::string label,path,entry,name,bank;};
    struct Catalog {std::string path,name;std::vector<Asset> files;std::vector<std::string> names;};
    std::shared_ptr<const Catalog> activeCatalog_,initialCatalog_;
    Asset selectedAsset_,pendingAsset_;
    bool externalPresetPending_=false;
    struct BrowserState {std::string path,root,status="Choose a drive, folder, then a bank.";std::vector<vitaleon_storage::Entry> entries;int page=0;};
    struct BrowserJob {int command;std::string path,root;int program=-1;};
    BrowserState browser_;
    std::mutex browserMutex_;
    std::deque<BrowserJob> browserJobs_;
    std::atomic<int> browserPending_{0};
    int storageCommands_=-1,storageRows_=-1,storagePath_=-1,storageStatus_=-1,storagePage_=-1,activeBankText_=-1;
    int storageAvailable_=-1;
    int uiGroup_=-1,uiPage_=-1,uiSection_=-1,uiKnobs_=-1,uiBank_=-1;
    int uiModPage_=-1;  // the MOD tab's one strip: LFO 1..8, MATRIX..CURVE, picker
    int uiSectionTab_=-1; // legacy 56-step strip slot, kept so no parameter index moves
    int groupSection_[7]={-1,-1,-1,-1,-1,-1,-1}; // one 8-step strip per group
    int groupPage_[7]={-1,-1,-1,-1,-1,-1,-1},groupSlot_[7]={-1,-1,-1,-1,-1,-1,-1};
    // The parameters each group's nine Q-Link slots point at, i.e. the last section
    // that group showed (0.3.34). The Force tab bar never calls back, so a tab can
    // become visible while the plugin still believes in another page; these let
    // refreshNavigation() keep every group's slots filled, so the host paints real
    // values on a tab's first visit instead of the 0 a fresh slot was created with.
    std::array<std::array<std::atomic<int>,16>,7> groupTargets_{};
    // What the host still has to hear about the Q-Link slots (0.3.23). A moving control
    // must not call into the host: 0.3.22 sent audioMasterAutomate on every movement and
    // redrew the page when the knob group changed, and the controls felt sluggish on the
    // device. The service thread sends these instead, the sound parameter and the page
    // redraw only once the control has rested for kSlotRestMs.
    static constexpr int kSlotRestMs=300;
    std::array<std::atomic<int>,16> slotAutomated_{}; // sound parameter of a moved slot, -1 none
    std::atomic<int> slotStepped_{-1};                // switch or list that stepped: redraw it now
    std::atomic<bool> bankChanged_{false},bankRedraw_{false};
    std::atomic<long long> slotEditMs_{0};            // time of the last slot write, 0 = at rest
    static long long nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void flushSlotNotifications();
    // Matrix table and its two-step target picker (helper views are hidden).
    int matrixView_=-1,pickGroupView_=-1,pickTargetView_=-1,routeTargetProxy_=-1,uiPick_=-1;
    void navigateTo(int page,int section);
    struct SurfaceView {int group,member;std::vector<std::vector<int>> sections;bool hidden=false;};
    std::vector<SurfaceView> surfaceViews_;
    std::array<std::atomic<int>,16> uiTargets_{};
    std::array<int,7> rememberedPages_{};
    std::vector<int> rememberedSections_; // the subtab each page was left on
    std::mutex navigationMutex_;
    void createStorage();
    bool storageChanged(int index);
    bool formatStorage(int index,std::string& out);
    void serviceBrowser();
    void publishBrowser();
    std::shared_ptr<const Catalog> indexBank(const std::string& path);
    void installCatalog(std::shared_ptr<const Catalog> catalog,bool lifecycleHeld=false);
    void createNavigation();
    void updateNavigation();
    bool applySlot(int slot,float plain);
    int groupMemberPage(int group,int member);
    int tabMemberIndex(int page);
    bool navigationChanged(int index,float plain);
    void refreshNavigation();
    // Run-time list selectors: the wheel's coarse fraction moves one entry (listStep).
    float listRaw_[6]{};int listIndex_[6]{};long long listMs_[6]{};float listTravel_[6]{};
    void listStep(int index,float plain);
    void publishList(int index,float plain); // sets a selector and its list cache
    // Table picker (0.3.35): the wheel moves a cursor, the entry loads at rest.
    // tools/generate_surface.py reads this number to lay out the same page.
    static constexpr int kTableRows=6;
    // The cursor knob binds to a discrete step parameter: the host moves a plain
    // parameter by 1/100 of its range per wheel click, but honours the steps of a
    // handle that the skin gave replacementStrings. One click is one table then.
    static constexpr int kTableSteps=512;
    int tableOpen_=-1,tableCommands_=-1,tableRows_=-1,tableRowStates_=-1,tableRowNames_=-1,tableRowFolders_=-1,tablePosition_=-1,tableStatus_=-1,tableNameReadout_=-1;
    int tableViewTarget_=-1,tableViewPage_=-1,tableViewStep_=-1;
    int tableCursor_=0;
    std::atomic<long long> tableLoadDue_{0};
    std::atomic<int> tableLoadSlot_{-1};
    std::mutex tableMutex_; // guards assets_[1]: RELOAD may swap it while the host reads
    void createTables();
    void createTableStep();
    void createGroupSections();
    void syncSectionFromStrip();
    void publishTables();
    void showTableCursor(int entry);
    void serviceTables();
    void serviceRandomFlash();
    void serviceRandomMidi();
    void tableReload();
    bool tableChanged(int index,float plain);
    bool formatTable(int index,std::string& out);
    int tableCount();
    int tablePageCount();
    std::string tableFolder(const Asset& asset);
    std::string tableText(const std::string& text,size_t limit);
    struct LibraryBank {std::string name;std::vector<int> files;};
    std::vector<LibraryBank> libraryBanks_;
    int factoryProgramCount_=0; // 1 = the "Init" program of a plugin without sounds, else 0
    std::vector<int> filePrograms_; // immutable after construction; native VST programs
    std::vector<float> initValues_; // plain values of the Init sound
    std::atomic<bool> startSound_{false}; // load the first installed sound on first prepare
    void selectInit();
    int libraryBank_=-1,libraryPage_=-1,libraryLoad_=-1,libraryPrevious_=-1,libraryNext_=-1,bankPrevious_=-1,bankNext_=-1;
    int libraryRows_=-1,libraryNames_=-1,libraryBankText_=-1,librarySelectedText_=-1,libraryLoadedText_=-1,libraryPageText_=-1;
    std::atomic<int> pendingPreset_{-1};
    // Random preset (0.3.35): a shuffled deck of the current bank's files. Drawing
    // without putting back means every installed sound plays once before any of them
    // can repeat, and a bank of N cannot show the same preset twice in a row. The
    // parameter lives at the very end of the list so no existing index moves.
    int libraryRandom_=-1,libraryRandomFlash_=-1,libraryRandomNote_=-1,libraryRandomLearn_=-1;
    std::vector<int> randomDeck_;long long randomDeckStamp_=0;size_t randomDrawn_=0;
    std::mt19937 randomRng_{std::random_device{}()};
    // Deadline that ends the press flash of the dice, in the same style as
    // tableLoadDue_: written by the tap, extended while a load runs, cleared by the
    // service loop. Long enough to be seen: the first repaint after a tap can only
    // arrive once the loading callback has returned.
    std::atomic<long long> randomFlashDue_{0};
    static constexpr long long kRandomFlashMs=400;
    // MIDI learn for the dice. The audio thread only records what it saw and never
    // touches a parameter; the service loop writes the note and starts the load,
    // exactly as if the dice had been tapped.
    std::atomic<bool> randomLearnArmed_{false};
    std::atomic<int> randomLearnCaptured_{-1};
    std::atomic<bool> randomMidiPending_{false};
    // UI caches must be refreshed explicitly after browser/state changes.
    // The service thread delivers host callbacks outside the engine locks.
    std::atomic<bool> browserDisplayDirty_{false};
    std::string loadedName_="Built-in / current";
    int homeControls_=-1;
    int homeFilter_=-1;
    std::array<int,7> homeFullTargets_{};
    int homeTarget(int slot)const{return homeFullTargets_[slot];}
    void refreshHome(){if(homeControls_>=0)for(int i=0;i<7;++i)publishNormalised(homeControls_+i,getNormalised(homeTarget(i)));if(homeFilter_>=0)publishNormalised(homeFilter_,getNormalised(controlIndex("filter_1_on")));}
    std::vector<Binding> bindings_;
    std::map<std::string,int> indices_;
    std::vector<ProxyBank> banks_;
    std::array<Route,64> routes_{};
    std::vector<std::string> sources_,destinations_;
    std::vector<vital::Output*> sourceOutputs_;
    std::array<std::vector<Asset>,4> assets_; // presets, tables, samples, LFO shapes
    std::array<int,3> tableSelect_{};
    int presetSelect_=-1,sampleSelect_=-1,lfoShapeSelect_=-1,loadStatus_=-1;
    int loadText_=-1;
    int lfoSelect_=-1,envSelect_=-1,routeSelect_=-1;
    int pointSelect_=-1,pointX_=-1,pointY_=-1,pointCurve_=-1,pointCount_=-1;
    int lfoShape_=-1,lfoSmooth_=-1;
    int visualEnabled_=-1;
    std::array<int,3> waveReadout_{};
    std::array<int,3> waveBankReadout_{};
    int curveBankReadout_=-1;
    std::array<int,3> wavePosition_{};
    int graphTurn_=0; // audio thread: the oscillator whose graph the rotation refreshes
    int lfoReadout_=-1,modReadout_=-1,envReadout_=-1,curveReadout_=-1;
    int lfoPhaseReadout_=-1;
    std::array<vital::Output*,3> waveMono_{},wavePoly_{};
    std::array<vital::ValueSwitch*,3> waveMonoSwitch_{},wavePolySwitch_{};
    std::array<const vital::StatusOutput*,8> lfoStatus_{},lfoPhase_{};
    std::array<int,8> lfoSource_{}; // index of lfo_N in sources_
    std::array<const vital::StatusOutput*,6> envStatus_{};
    std::array<std::array<float,128>,3> waveform_{};
    Json savedAssets_=Json::object(),stagedState_;
    std::string loadMessage_="Ready";
    std::vector<std::string> importNotes_; // what the last import left out
    std::shared_ptr<const Catalog> indexFolder(const std::string& folder);
    std::mutex stateMutex_; // ONLY non-audio state/asset service uses this.
    std::atomic<int> gate_{0}; // 0=audio owns engine, 1=park requested, 2=parked, 3=resume
    std::atomic<unsigned> requested_{0},completed_{0};
    std::atomic<bool> graphRebuild_{false};
    std::atomic<bool> quit_{false},resetPending_{false},replayNotes_{false};
    std::atomic<bool> resetBenchArmed_{true};
    std::atomic<unsigned> curveEdits_{0};
    std::atomic<bool> curveSelectionChanged_{true};
    std::atomic<bool> inspectCurve_{false};
    std::atomic<bool> programReset_{false};
    std::atomic<bool> migrateOldTables_{false};
    std::mutex lifecycleMutex_; // serialize prepare and non-audio service only
    std::thread service_;
    std::array<std::atomic<int>,2048> held_{};
    // Graph readouts already sent to the host (service thread only).
    std::chrono::steady_clock::time_point lastVisual_{};
    // Graph refresh (0.3.18). Measured on the Live II: every audioMasterAutomate call for
    // a readout can delay the MPC's audio callback (240 late callbacks in 10 s at 780
    // calls/s, still 28 in 7 s at 174 calls/s; real-time priority or calling from the
    // audio thread change nothing). ONE audioMasterUpdateDisplay per picture refreshes
    // all readouts and caused no late callback. The audio thread bumps visualVersion_
    // when a value of the visible graph changed; the service thread then asks for one
    // display update, at most every visualIntervalMs_.
    std::atomic<unsigned> visualVersion_{0};unsigned visualShown_=0;std::atomic<int> visualIntervalMs_{50};
    // Graph resolution; tools/generate_surface.py reads these four numbers.
    static constexpr int kWaveColumns=128,kWaveLevels=108,kCurveColumns=192,kCurveLevels=96;
    int waveColumn(int osc,int column)const{return column<96?waveReadout_[size_t(osc)]+column:waveBankReadout_[size_t(osc)]+column-96;}
    int curveColumn(int column)const{return column<96?curveReadout_+column:curveBankReadout_+column-96;}
#ifdef VITALEON_GFXDIAG
    // Device measurement (never in a release): the plugin holds a chord itself while
    // any page except HOME is open. On an OSC page it cycles four refresh rates
    // (10, 15, 20, 30 pictures a second), 4 s each; any other page is the baseline.
    // ADVANCED shows the figures.
    struct GfxStat{std::atomic<long> blocks{0},late{0},sumUs{0},maxUs{0},maxGapUs{0},maxWaitUs{0},calls{0},callSumUs{0},callMaxUs{0},frames{0};};
    std::array<GfxStat,5> gfx_;std::atomic<int> gfxMode_{-1};long gfxFrames_=0,gfxSince_=0;bool gfxHeld_=false;int gfxLine_=0;long gfxWaitUs_=0;
    std::chrono::steady_clock::time_point gfxLastStart_{},gfxLastReport_{};
    void gfxStep(int frames);void gfxReport();
#endif
    void notifyVisuals();
    bool ready_=false;
    bool asleep_=false;int silentFrames_=0; // audio thread only
    // ---- Effects on a second core -------------------------------------------
    // Measured on the Live II, the effect chain costs about 16 % of a 128-frame
    // block on top of the voices. Vital's voices share intermediate buffers and
    // cannot run in parallel, but voices and effect chain can form a pipeline:
    // the audio thread renders the voices of chunk k while the worker renders
    // the effects of chunk k-1. Output is delayed by kFxLatency samples.
    // Rule: the worker is idle (fxWaitIdle) whenever anything but
    // SoundEngine::processStageA touches the engine.
    struct FxSignal{
#ifdef __linux__
        sem_t semaphore;FxSignal(){sem_init(&semaphore,0,0);}~FxSignal(){sem_destroy(&semaphore);}
        void post(){sem_post(&semaphore);}void wait(){while(sem_wait(&semaphore)!=0){}}
#else
        std::mutex mutex;std::condition_variable condition;int count=0;
        void post(){{std::lock_guard<std::mutex> lock(mutex);++count;}condition.notify_one();}
        void wait(){std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[&]{return count>0;});--count;}
#endif
    };
    static constexpr int kFxRing=1024,kFxLatency=128;
    FxSignal fxSignal_;std::thread fxThread_;
    std::atomic<int> fxPending_{0};std::atomic<bool> fxQuit_{false},fxSplit_{false},fxWanted_{true};std::atomic<float> fxLoad_{0};
    std::array<std::array<float,kFxRing>,2> fxRing_{};int fxRead_=0,fxWrite_=kFxLatency;bool fxPriority_=false;
    void fxLoop();void fxWaitIdle();void fxReset();
public:
    void testSplit(bool enabled){fxWaitIdle();fxWanted_.store(enabled);fxReset();fxSplit_.store(enabled&&fxThread_.joinable()&&engine_->canSplit());}
    bool testSplitActive()const{return fxSplit_.load();}
private:
    // CPU guard (audio thread only). Measured on the Live II: one voice pair of a
    // dense preset costs about 22 % of a 128-frame block, effects another 16 %.
    // Instead of dropouts the oldest voices are stolen while the engine needs
    // more than 80 % of the block time; the limit recovers after 3 calm seconds.
    float cpuLoad_=0;int dynamicVoices_=32,guardUser_=-1;long guardHold_=0,calmFrames_=0;
    void cpuGuard(float ratio,int frames);
public:
    std::atomic<float> testLoad_{-1.0f};
    int testDynamicVoices()const{return dynamicVoices_;}
private:
    // Effects that are switched on but mixed to exactly 0 (the usual "macro opens
    // the effect" preset design) are not processed. See planEffectBypass().
    struct FxBypass{int onParam=-1,mixParam=-1;vital::Value* onValue=nullptr;std::atomic<bool> eligible{false};std::atomic<unsigned> macros{0};};
    std::array<FxBypass,8> fxBypass_;std::array<int,4> macroParam_{{-1,-1,-1,-1}};
    std::atomic<bool> fxPlanDirty_{true};
    void planEffectBypass();
    // Display-refresh diagnostic (ADVANCED page only): a free-running 2 s ramp
    // written to three parameters that differ only in how the host is told.
    int diagConfig_=-1,diagCount_=0,diagShown_=0;long diagFrames_=0;double diagSum_=0;
    std::array<std::atomic<int>,8> diagResult_{};std::atomic<int> diagBlock_{0},diagVersion_{0};
    void diagApply(int config);void diagStep(int frames);void diagReport();
public:
    bool testAsleep()const{return asleep_;}
    bool testSlotsAtRest()const{return slotEditMs_.load()==0;}
private:
    int displayCountdown_=0,lastPreset_=0,lastSample_=0,lastShape_=0;
    std::array<int,3> lastTable_{{0,0,0}};
    double preparedRate_=44100,lastTimeline_=-1;
    int advancedSelector_=-1,advancedValue_=-1;
    int curveTarget_=-1,effectSlot_=-1,effectType_=-1,effectOrder_=-1,mpeEnabled_=-1,mpeLower_=-1,mpeUpper_=-1;
    std::array<int,16> rpnMsb_{},rpnLsb_{};
    std::array<float,16> memberBendRange_{};
    std::vector<int> advancedTargets_;
    void createControls();
    void createBanks();
    void createReadouts();
    void refreshProxies();
    void syncFullControls(bool force=false,bool structural=false);
    void renderBlock(mpc::ProcessContext& ctx);
    void updateDisplays(int frames);
    void handleFullMidi(const mpc::MidiEvent& e);
    void serviceLoop();
    void parkEngine();
    void serviceChanges(bool rebuild);
    void connectRoutes();
    void clearRoutes();
    void warmGraph();
    void scanAssets();
    void createLibrary();
    void createFilePrograms();
    void refreshLibrary();
    bool libraryChanged(int index,float plain);
    bool formatLibrary(int index,std::string& out);
    int libraryFile(int row)const;
    int libraryPageCount()const;
    void loadFileIndex(int file);
    int drawRandomFile();
    void flashRandomPress();
    static std::string textPart(const std::string& text,int part);
    void publishLoadedName();
    void loadSelections();
    void loadPreset(const Json& preset);
    void restoreAssets(const Json& state,bool tables=true,bool samples=true);
    void loadTable(int oscillator,const Json& table);
    void loadSampleState(const Json& sample,const vitaleon_assets::SampleData* decoded=nullptr);
    void loadCurve(LineGenerator* curve,const Json& data);
    void editLfo();
    void refreshCurveControls();
    void setLoadStatus(int status){
        if(status==1)loadMessage_="Loading...";
        publish(loadStatus_,float(status));
        float marker=status==1?.5f:float(std::hash<std::string>{}(loadMessage_)&0xffffff)/16777216.0f;
        if(loadText_>=0)for(int i=0;i<4;++i)publishNormalised(loadText_+i,marker);
        if(storageAvailable_>=0){std::lock_guard<std::mutex> browser(browserMutex_);browser_.status=loadMessage_;publishBrowser();}
        browserDisplayDirty_.store(true);
    }
    LineGenerator* selectedCurve(){return getBool(curveTarget_)?engine_->getModulationBank().atIndex(getInt(routeSelect_)-1)->modulation_processor->lineMapGenerator():engine_->getLfoSource(getInt(lfoSelect_)-1);}
    void defaultTables();
    void migrateOldControls();
    void migrationTables();
    void captureAssets();
    Json readAsset(const Asset& asset);
    std::vector<uint8_t> readAssetBytes(const Asset& asset);
    void requestService(bool rebuild=true){if(ready_){if(rebuild)graphRebuild_.store(true);requested_.fetch_add(1,std::memory_order_release);}}
    int addProxyBank(const std::string& title,const std::string& prefix,int count,const std::vector<std::string>& suffixes);
    int addReadout(const std::string& id);
    void publish(int index,float plain){publishNormalised(index,param(index).toNormalised(plain));}
};

#include "FullControls.inc"
#include "FullAssets.inc"
#include "FullLibrary.inc"
#include "FullStorage.inc"
#include "FullNavigation.inc"
#include "FullTables.inc"
#include "FullRuntime.inc"
