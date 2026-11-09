#include "../vibration_service_registration.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void rejected(const struct rme_vib_target_registration_input *in) {
    unsigned char parcel[200],original[200];memset(parcel,0xa5,sizeof parcel);memcpy(original,parcel,sizeof parcel);
    size_t length=7;uint64_t offset=9;
    assert(rme_vib_encode_target_add_service(in,parcel,sizeof parcel,&length,&offset)<0);
    assert(!memcmp(parcel,original,sizeof parcel)&&length==7&&offset==9);
}
int main(void) {
    struct rme_vib_target_registration_input in={
        .model="NX809J",.abi="arm64-v8a",.api=36,.n64=1,
        .experimental_enabled=0,
        .transaction={.interface_header={0,0,0,0,0,0,0,0,'T','S','Y','S'},
                      .binder_flags=0x113,.binder_ptr=1,.binder_cookie=2,.stability=63},
    };
    unsigned char parcel[200],untouched[200];memset(parcel,0xa5,sizeof parcel);memcpy(untouched,parcel,sizeof parcel);
    size_t length=7;uint64_t offset=9;
    /* Explicit opt-in is required; no production caller uses this entry. */
    assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);
    assert(!memcmp(parcel,untouched,sizeof parcel)&&length==7&&offset==9);
    /* Opt-in and identities below are synthetic. This does not claim a real
     * ServiceManager registration or hardware operation has worked. */
    in.experimental_enabled=1;
    in.model="OTHER";assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);in.model="NX809J";
    in.abi="armeabi-v7a";assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);in.abi="arm64-v8a";
    in.api=35;assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);in.api=36;
    in.n64=0;assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);in.n64=1;
    in.transaction.binder_flags=0x13;assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);in.transaction.binder_flags=0x113;
    in.transaction.stability=12;assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);in.transaction.stability=63;
    in.transaction.interface_header[8]='V';assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)<0);in.transaction.interface_header[8]='T';
    in.experimental_enabled=2;rejected(&in);in.experimental_enabled=1;
    in.n64=2;rejected(&in);in.n64=1;
    in.model=NULL;rejected(&in);in.model="NX809J";
    in.abi=NULL;rejected(&in);in.abi="arm64-v8a";
    in.transaction.binder_ptr=0;rejected(&in);in.transaction.binder_ptr=1;
    in.transaction.binder_cookie=0;rejected(&in);in.transaction.binder_cookie=2;
    assert(rme_vib_encode_target_add_service(&in,parcel,sizeof parcel,&length,&offset)==0);
    assert(length==200 && offset==164 && le32(parcel+168)==0x113 && le32(parcel+188)==63);
    puts("VIBRATION_TARGET_REGISTRATION_GATE_OK");
}
