// Execute the actual sketch's task against deterministic hardware/RTOS stubs.
// This validates state transitions; it does not emulate USB hardware or scheduling.
#include "../firmware/RecoilEngine/RecoilEngine.ino"
#include <cassert>
void reset(uint64_t end=300000) {
 fake::now=0;fake::end=end;fake::wake=0;fake::rng=7;
 fake::reports.clear();fake::rejectReports=false;fake::isMounted=true;fake::isReady=true;fake::isSuspended=false;
 fake::trigger=[](uint64_t){return false;};fake::step=[](uint64_t){};fake::onDelay=[](){};
 g_tasksReady.store(true);g_engineFault.store(false);
 g_flashSaveRequested.store(false);g_flashSaveGranted.store(false);
 g_atomicState.packed.store(packControl(14,MODE_CLOSE,true));
 engineWakeTimer=(void*)1;r32InitDynamics();
}
void run() {try{EngineTask(nullptr);}catch(const fake::Stop&){} }
void normalPress() {
 reset();fake::trigger=[](uint64_t t){return t>=10000 && t<200000;};run();
 assert(!fake::reports.empty());
 for(auto r:fake::reports)assert(r.at>=13000 && r.at<205000);
}
void bootHeld() {
 reset();fake::trigger=[](uint64_t t){return t<90000 || (t>=150000 && t<250000);};run();
 assert(!fake::reports.empty());for(auto r:fake::reports)assert(r.at>=153000);
}
void reconnectHeld(bool suspend) {
 reset(400000);
 fake::trigger=[](uint64_t t){return t>=10000 && (t<250000 || t>=300000);};
 fake::step=[suspend](uint64_t t){
   fake::isMounted=suspend || !(t>=90000 && t<150000);
   fake::isSuspended=suspend && t>=90000 && t<150000;
 };
 run();bool before=false, after=false;
 for(auto r:fake::reports){if(r.at<90000)before=true;if(r.at>303000)after=true;assert(r.at<90000 || r.at>=303000);}
 assert(before && after);
}
void longBusyRecovery() {
 reset();fake::trigger=[](uint64_t t){return t>=10000 && t<250000;};
 fake::step=[](uint64_t t){fake::isReady=!(t>=70000 && t<170000);};
 run(); bool after=false;
 for(auto r:fake::reports) {
   assert(r.at<70000 || r.at>=170000);
   if(r.at>=170000){after=true;assert(std::abs(r.x)<=2 && std::abs(r.y)<=2);}
 }
 assert(after);
}
void rejectedQueueRecovery() {
 reset();fake::trigger=[](uint64_t t){return t>=10000 && t<250000;};
 fake::step=[](uint64_t t){fake::rejectReports=t>=70000 && t<170000;};
 run();bool recovered=false;
 for(auto r:fake::reports) {
   assert(r.at<70000 || r.at>=170000);
   if(!recovered && r.at>=170000) {
     assert(r.x==0 && r.y==0); // a stale transport first accepts a zero-motion probe
     recovered=true;
   }
 }
 assert(recovered);
}
void zeroProfileStopsTail() {
 reset();fake::trigger=[](uint64_t t){return t>=10000 && t<250000;};
 fake::step=[](uint64_t t){if(t>=100000)setControlProfile(0);};
 run();bool before=false;
 for(auto r:fake::reports){assert(r.at<117000);before=true;}
 assert(before); // profile refresh is intentionally still every 16 ticks
}
void delayedSpring() {
 reset(120000);fake::trigger=[](uint64_t t){return t>=10000 && t<54000;};
 // Let the tap produce movement but block all reports from release to 100ms.
 fake::step=[](uint64_t t){fake::isReady=t<55000 || t>=100000;};
 run();assert(!fake::reports.empty());
 for(auto r:fake::reports)assert(r.at<55000); // expired return never escapes
}
void flashHandshake() {
 reset(100000);fake::trigger=[](uint64_t){return false;};
 bool grantObserved=false;
 fake::step=[](uint64_t t){if(t>=10000 && t<11000)g_flashSaveRequested.store(true);};
 fake::onDelay=[&](){grantObserved=g_flashSaveGranted.load();assert(grantObserved);g_flashSaveRequested.store(false);};
 run();assert(grantObserved);assert(!g_flashSaveGranted.load());assert(fake::reports.empty());
}
void failedNvsWriteRetries() {
 reset();prefs.data.clear();settingsStoreReady=true;needSaveProfile=true;needSaveMode=true;
 fake::prefsWriteOk=false;assert(!saveConfigSafely());
 assert(needSaveProfile && needSaveMode && settingsSaveFailed);
 fake::prefsWriteOk=true;assert(saveConfigSafely());
 assert(!needSaveProfile && !needSaveMode && !settingsSaveFailed);
 assert(prefs.getInt("prof_idx",-1)==14);
}
int main(){
 normalPress();bootHeld();reconnectHeld(false);reconnectHeld(true);
 longBusyRecovery();rejectedQueueRecovery();zeroProfileStopsTail();delayedSpring();flashHandshake();failedNvsWriteRetries();
 std::puts("PASS: 10 actual-sketch integration scenarios (stub hardware)");
}
