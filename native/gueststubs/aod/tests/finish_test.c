#include "../aod_postprocess.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Synthetic reference generated with OpenCV INTER_LINEAR, MORPH_ELLIPSE 5x5,
 * erode iterations=3, and GaussianBlur 5x5 BORDER_REFLECT_101. */
int main(void) {
    uint8_t in[63],out[143];
    const uint8_t expected[143]={
      0,0,0,0,0,0,0,0,0,2,9,22,28,
      0,0,0,0,0,0,0,0,0,1,6,14,18,
      0,0,0,0,0,0,0,0,0,0,2,4,5,
      0,0,0,0,0,0,0,0,0,0,0,0,0,
      0,0,0,0,0,0,0,0,0,0,0,0,0,
      0,0,0,0,0,0,0,0,0,0,0,0,0,
      3,2,1,0,0,0,0,0,0,0,0,0,0,
      20,16,8,3,1,0,0,0,0,0,0,0,0,
      69,58,36,18,7,2,0,0,0,0,0,0,0,
      138,122,85,48,20,6,1,0,0,0,0,0,0,
      172,155,113,66,29,9,1,0,0,0,0,0,0
    };
    memset(in,255,sizeof(in));
    for(int y=1;y<3;y++)for(int x=2;x<4;x++)in[y*9+x]=0;
    for(int y=4;y<7;y++)for(int x=7;x<9;x++)in[y*9+x]=0;
    assert(aod_finish_mask(in,sizeof(in),9,7,out,sizeof(out),13,11)==0);
    assert(memcmp(out,expected,sizeof(out))==0);
    uint8_t rounding_input[21],rounding_output[90];
    memset(rounding_input,255,sizeof(rounding_input));rounding_input[0]=rounding_input[1]=0;
    assert(aod_finish_mask(rounding_input,sizeof(rounding_input),7,3,rounding_output,sizeof(rounding_output),10,9)==0);
    assert(rounding_output[3*10+9]==125 && rounding_output[4*10+7]==55 && rounding_output[8*10+2]==75);
    uint8_t one=255; out[0]=42;
    assert(aod_finish_mask(&one,1,1,1,out,1,1,1)==0 && out[0]==255);
    assert(aod_finish_mask(in,sizeof(in),9,7,out,sizeof(out)-1,13,11)!=0);
    puts("AOD_FINISH_MASK_OK");
    return 0;
}
