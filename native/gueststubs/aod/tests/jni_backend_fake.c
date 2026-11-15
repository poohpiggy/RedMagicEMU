#define _POSIX_C_SOURCE 200809L
#include "../aod_backend.h"
#include <stdatomic.h>
#include <time.h>

atomic_int fake_max_active;
atomic_int fake_fail_open;
static atomic_int active;
int fake_active_count(void) { return atomic_load(&active); }
int aod_backend_open(void **out) {
    if (atomic_load(&fake_fail_open)) { *out=NULL;return -1; }
    *out=&active;return 0;
}
int aod_backend_run(void *ctx,const uint8_t *rgb,size_t n,int w,int h,float *logits,size_t cap) {
    (void)ctx;(void)n;(void)w;(void)h;
    if(rgb[0]==42)return -1;
    int nactive=atomic_fetch_add(&active,1)+1;
    int old=atomic_load(&fake_max_active);
    while(nactive>old && !atomic_compare_exchange_weak(&fake_max_active,&old,nactive)) {}
    struct timespec t={0,5000000};
    nanosleep(&t,NULL);
    for(size_t i=0;i<cap;i++)logits[i]=5.f;
    atomic_fetch_sub(&active,1);return 0;
}
void aod_backend_close(void *ctx) { (void)ctx; }
