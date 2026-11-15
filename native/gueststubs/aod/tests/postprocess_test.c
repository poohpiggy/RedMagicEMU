#include "../aod_postprocess.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t in[100] = {0}, out[100];
    for (int y=3;y<=7;y++) for (int x=3;x<=7;x++) in[y*10+x]=200;
    in[5*10+5]=0;          /* enclosed hole */
    in[2*10+3]=50;         /* connected low confidence survives final >40 */
    in[2*10+4]=40;         /* threshold is strict */
    in[9*10+9]=255;        /* separate tiny component is removed */
    assert(aod_prepare_mask(in,sizeof(in),10,10,out,sizeof(out))==0);
    assert(out[5*10+5]==255 && out[2*10+3]==255 && out[2*10+4]==0);
    assert(out[9*10+9]==0 && out[0]==0);
    uint8_t diag[81]={0}, diag_out[81];
    for (int y=1;y<=7;y++) for (int x=1;x<=7;x++) diag[y*9+x]=200;
    diag[1*9+1]=diag[2*9+2]=diag[3*9+3]=0;
    assert(aod_prepare_mask(diag,sizeof(diag),9,9,diag_out,sizeof(diag_out))==0);
    assert(diag_out[3*9+3]==0); /* connected to edge diagonally */
    memset(in,100,sizeof(in));
    memset(out,0xaa,sizeof(out));
    assert(aod_prepare_mask(in,sizeof(in),10,10,out,sizeof(out))!=0);
    for (int i=0;i<100;i++) assert(out[i]==0);
    memset(in,0,sizeof(in));
    assert(aod_prepare_mask(in,sizeof(in),10,10,out,sizeof(out))!=0);
    assert(aod_prepare_mask(in,sizeof(in)-1,10,10,out,sizeof(out))!=0);
    assert(aod_prepare_mask(in,sizeof(in),10,10,in,sizeof(in))!=0);
    puts("AOD_PREPARE_MASK_OK");
    return 0;
}
