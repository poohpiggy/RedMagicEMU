/*
 * MPEG-4 Part 2 / H.263 encoding for the guest's OMX video encoder (VideoEncoderFeed.kt), with AOSP's PacketVideo
 * encoder. The phone's MediaCodec encoders cannot take the guest's sizes: c2.android.mpeg4.encoder limits the size
 * to 176x144 and the H.263 one to 176x144 / 352x288 (C2SoftMpeg4Enc.cpp IntfImpl), vendors rarely ship either.
 * Settings follow C2SoftMpeg4Enc::initEncParams: VBR_1, VBV delay 5 s, time resolution 1000, I/P quant 15/12,
 * scene detection, AC prediction, search range 16, an intra frame every second.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include "mp4enc_api.h"

struct enc {
    VideoEncControls ctl;
    int w, h, h263, sync;
    uint8_t *yuv;   /* I420 */
};

/* The encoder picks the lowest level that fits (SetProfile_BufferSize) up to the one given here: Simple @ L5 */
extern "C" JNIEXPORT jlong JNICALL Java_app_aemu_core_M4vEncoder_nInit(JNIEnv *, jclass, jint coding, jint w, jint h,
                                                                       jint fps, jint bitrate) {
    if (w <= 0 || h <= 0 || (w & 15) || (h & 15) || fps <= 0) return 0;
    struct enc *e = (struct enc *)calloc(1, sizeof *e);
    if (!e) return 0;
    e->w = w; e->h = h; e->h263 = coding == 3;
    e->yuv = (uint8_t *)malloc((size_t)w * h * 3 / 2);
    VideoEncOptions o;
    memset(&o, 0, sizeof o);
    if (PVGetDefaultEncOption(&o, 0)) {
        o.encMode = e->h263 ? H263_MODE : COMBINE_MODE_WITH_ERR_RES;
        o.packetSize = 32;
        o.rvlcEnable = PV_OFF;
        o.numLayers = 1;
        o.timeIncRes = 1000;
        o.tickPerSrc = 1000 / fps;
        o.encWidth[0] = w; o.encHeight[0] = h;
        o.encFrameRate[0] = (float)fps;
        o.bitRate[0] = bitrate > 0 ? bitrate : 384000;
        o.iQuant[0] = 15; o.pQuant[0] = 12; o.quantType[0] = 0;
        o.rcType = VBR_1;
        o.vbvDelay = 5.0f;
        o.noFrameSkipped = PV_ON;
        o.intraPeriod = fps;   /* one second */
        o.numIntraMB = 0;
        o.sceneDetect = PV_ON;
        o.searchRange = 16;
        o.mv8x8Enable = PV_OFF;
        o.gobHeaderInterval = 0;
        o.useACPred = PV_TRUE;
        o.intraDCVlcTh = 0;
        o.profile_level = e->h263 ? CORE_PROFILE_LEVEL2 : SIMPLE_PROFILE_LEVEL5;
        if (PVInitVideoEncoder(&e->ctl, &o)) { e->sync = 1; return (jlong)(intptr_t)e; }
    }
    free(e->yuv); free(e);
    return 0;
}

/* the VOL header (MPEG-4's codec config); H.263 has none */
extern "C" JNIEXPORT jbyteArray JNICALL Java_app_aemu_core_M4vEncoder_nHeader(JNIEnv *env, jclass, jlong h) {
    struct enc *e = (struct enc *)(intptr_t)h;
    if (!e || e->h263) return nullptr;
    uint8_t buf[256];
    Int n = sizeof buf;
    if (!PVGetVolHeader(&e->ctl, buf, &n, 0)) return nullptr;
    jbyteArray a = env->NewByteArray(n);
    env->SetByteArrayRegion(a, 0, n, (const jbyte *)buf);
    return a;
}

/* one NV21 frame → the bitstream in out; returns its size (0: skipped by rate control, -1: error), bit 30 = sync */
extern "C" JNIEXPORT jint JNICALL Java_app_aemu_core_M4vEncoder_nEncode(JNIEnv *env, jclass, jlong h, jbyteArray in,
                                                                        jlong tsUs, jbyteArray out) {
    struct enc *e = (struct enc *)(intptr_t)h;
    if (!e) return -1;
    int w = e->w, ht = e->h, ys = w * ht;
    if (env->GetArrayLength(in) < ys * 3 / 2) return -1;
    env->GetByteArrayRegion(in, 0, ys, (jbyte *)e->yuv);
    jbyte *src = env->GetByteArrayElements(in, nullptr);
    const uint8_t *vu = (const uint8_t *)src + ys;
    uint8_t *u = e->yuv + ys, *v = u + ys / 4;
    for (int i = 0; i < ys / 4; i++) { v[i] = vu[2 * i]; u[i] = vu[2 * i + 1]; }   /* NV21: V then U */
    env->ReleaseByteArrayElements(in, src, JNI_ABORT);
    VideoEncFrameIO vin, vout;
    memset(&vin, 0, sizeof vin); memset(&vout, 0, sizeof vout);
    vin.yChan = e->yuv; vin.uChan = u; vin.vChan = v;
    vin.height = ht; vin.pitch = w;
    vin.timestamp = (ULong)((tsUs + 500) / 1000);   /* ms, as C2SoftMpeg4Enc::process */
    jsize cap = env->GetArrayLength(out);
    jbyte *ob = env->GetByteArrayElements(out, nullptr);
    Int size = cap, layer = 0;
    ULong next = 0;
    int ok = PVEncodeVideoFrame(&e->ctl, &vin, &vout, &next, (UChar *)ob, &size, &layer);
    MP4HintTrack hint;
    int sync = ok && PVGetHintTrack(&e->ctl, &hint) && hint.CodeType == 0;
    env->ReleaseByteArrayElements(out, ob, 0);
    if (!ok) return -1;
    if (layer < 0) return 0;
    return size | (sync ? 1 << 30 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_app_aemu_core_M4vEncoder_nClose(JNIEnv *, jclass, jlong h) {
    struct enc *e = (struct enc *)(intptr_t)h;
    if (!e) return;
    PVCleanUpVideoEncoder(&e->ctl);
    free(e->yuv); free(e);
}
