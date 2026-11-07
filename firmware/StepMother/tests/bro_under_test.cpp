#include "../src/stepbro.cpp"
extern "C" {
int test_bro_step(int slot,int voice,int step) {return bro::state.slots[slot].steps[voice][step];}
int test_bro_playhead(void) {return bro::playhead;}
int test_bro_local_stopped(void) {return bro::locallyStopped;}
int test_bro_active_slot(void) {return bro::activeSlot[0];}
int test_bro_chain_on(void) {return bro::chainOn[0];}
int test_bro_gate(int slot) {return bro::state.slots[slot].gatePercent;}
int test_bro_stream_muted(void) {return bro::streamMuted[0];}
int test_bro_resume_pending(void) {return bro::resumePending;}
int test_bro_loop_len(void) {return bro::globalLoopLen();}
void test_bro_clear(int slot) {memset(bro::state.slots[slot].steps,0,sizeof(bro::state.slots[slot].steps));for(int v=0;v<32;v++)bro::mutedVoice[slot][v]=false;bro::recalcHasContent(slot);bro::recalcLoopLength();}
int test_bro_channel(void) {return bro::state.streamChannel[0];}
int test_bro_chain_editing(void) {return bro::chainEditing;}
int test_bro_chain_length(void) {return bro::chainLen[0];}
int test_bro_recording(void) {return bro::recordArmed;}
void test_bro_seed(int slot,int voice,int step,int vel) {bro::state.slots[slot].steps[voice][step]=vel;bro::recalcHasContent(slot);bro::recalcLoopLength();}
}
