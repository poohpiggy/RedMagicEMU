#include <stdint.h>
#include <stdio.h>
#include "verify.h"
int main(void) {
  uint32_t a[64],b[64];
  for(uint32_t i=0;i<64;++i){a[i]=input_word(i);b[i]=FILL_WORD;}
  if (mismatches(a,64,0,0)!=0 || mismatches(b,64,1,0)!=0) return 1;
  if (mismatches(a,64,0,1)!=1 || mismatches(b,64,1,1)!=1) return 2;
  a[4]^=1; b[37]^=1;
  if (mismatches(a,64,0,0)!=1 || mismatches(b,64,1,0)!=1) return 3;
  puts("AHB_VERIFY_TEST_OK");
  return 0;
}