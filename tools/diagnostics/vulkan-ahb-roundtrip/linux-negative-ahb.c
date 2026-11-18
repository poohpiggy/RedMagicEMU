#include <android/hardware_buffer.h>
#include <string.h>
/* Linux negative-control shim: cannot allocate or simulate a valid AHB. */
int AHardwareBuffer_isSupported(const AHardwareBuffer_Desc *d) { (void)d; return 0; }
int AHardwareBuffer_allocate(const AHardwareBuffer_Desc *d, AHardwareBuffer **b) { (void)d; *b=0; return -1; }
void AHardwareBuffer_describe(const AHardwareBuffer *b, AHardwareBuffer_Desc *d) { (void)b; memset(d,0,sizeof(*d)); }
int AHardwareBuffer_lock(AHardwareBuffer *b,uint64_t u,int32_t f,const ARect *r,void **p) { (void)b;(void)u;(void)f;(void)r;*p=0;return -1; }
int AHardwareBuffer_unlock(AHardwareBuffer *b,int32_t *f) { (void)b;(void)f;return -1; }
void AHardwareBuffer_release(AHardwareBuffer *b) { (void)b; }