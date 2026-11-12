// Host-only access to the actual private fixed-point functions; not firmware.
#include "../src/voice303.c"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  curves_init(); double rational_error=0, sine_error=0;
  int32_t prev=-ONE_Q28;
  for(int i=0;i<=60000;i++) {
    double x=-3.0+6.0*i/60000;
    int32_t y=rational_q28((int32_t)(x*ONE_Q28));
    double expected=x*(27+x*x)/(27+9*x*x);
    double e=fabs((double)y/ONE_Q28-expected);
    if(e>rational_error)rational_error=e;
    if(y<prev || y>ONE_Q28 || y<-ONE_Q28) return 1;
    prev=y;
  }
  for(int i=0;i<=60000;i++) {
    double x=-20.0+40.0*i/60000;
    int32_t y=sine_q22((int32_t)(x*(1<<22)));
    double e=fabs((double)y/ONE_Q28-sin(x));
    if(e>sine_error)sine_error=e;
  }
  printf("rational LUT max error %.9f; sine LUT max error %.9f\n",rational_error,sine_error);
  if(rational_error>0.00003 || sine_error>0.00003) return 1;
  puts("PASS rational boundedness/monotonicity and signed sine phase wrapping");
  return 0;
}
