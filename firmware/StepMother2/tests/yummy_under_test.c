#include "../src/ui.c"
int test_yummy_active(void) {return panel_active;}
int test_yummy_wave(void) {return params.wave;}
int test_yummy_page(void) {return page;}
int test_yummy_playing(void) {return playing;}
int test_yummy_held(void) {return n_held;}
int test_yummy_recording(void) {return rec_armed;}
int test_yummy_poly(void) {return pat()->poly;}
void test_yummy_poly_set(void) {pat()->poly=1;}

int test_yummy_auto_armed(void) {return auto_armed;}
int test_yummy_auto_count(void) {int n=0;for(int a=0;a<AUTO_PARAMS;a++)for(int st=0;st<SEQ_STEPS;st++)if(autom[edit_slot][a][st]!=AUTO_NONE)n++;return n;}

float test_yummy_mix(void){return params.distortion;}
float test_yummy_drive(void){return params.dist_drive;}
int test_yummy_dist_type(void){return params.dist_type;}
void test_yummy_drive_set(float x){params.dist_drive=x;params_dirty=true;}

// 0.3.2
void test_yummy_set_scale(int sc){set_scale((seq_scale_t)sc);}
int test_yummy_scale(void){return scale;}
int test_yummy_root(void){return root;}
void test_yummy_note_out(int n,int vel){note_out((uint8_t)n,(uint8_t)vel);}
int test_yummy_midi_out(void){return midi_out;}
void test_yummy_set_midi_out(int on){midi_out=(uint8_t)(on!=0);}
int test_yummy_channel(void){return midi_channel;}
int test_yummy_lead(void){return midi_lead;}
