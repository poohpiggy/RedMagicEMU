/*
 * camera.default.so — camera HAL of AEmulator, camera device API 3.3 (arm64, Android 12+: the camera service there
 * no longer takes a HAL 1 device; a vendor camera application also needs a HAL 3 device for its vendor tags).
 *
 * The host side is the one of camera_hal.c: one unix socket connection per opened camera, /dev/socket/aemu_cam,
 * text commands ("open <id>", "start", "stop", "shot [left top width height]", "close"), frames back as
 *     int32 type, int32 width, int32 height, int32 length, then <length> bytes
 *     type 1: RGBA_8888 (640x480) followed by NV21; type 2: JPEG of a picture.
 *     type 5: uint64 sensor timestamp and uint32 RGBA/YV12 slot for "video 1".
 *     type 6: empty payload with the shared memory descriptor.
 *
 * Streams: IMPLEMENTATION_DEFINED becomes RGBA_8888 (what the software GL of the guest samples), YCbCr_420_888 is
 * NV21 in one block (the layout the engine's gralloc reports from lock_ycbcr), BLOB is the host's JPEG. A stream of
 * another size than the host's frame is filled by nearest point scaling.
 *
 * Requests are answered by one worker thread in order: it waits for the next host frame (or for the JPEG when the
 * request has a BLOB buffer), fills the buffers and returns the settings as the result with the 3A states
 * converged. libcamera_metadata.so (already loaded by the camera provider) builds the metadata.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include "camera_tags_aosp.h"

extern long read(int fd, void *buf, unsigned long n);
extern long write(int fd, const void *buf, unsigned long n);
extern int close(int fd);
extern void *malloc(unsigned long n);
extern void *calloc(unsigned long n, unsigned long size);
extern void free(void *p);
extern int usleep(unsigned long us);
extern void *dlopen(const char *name, int flags);
extern void *dlsym(void *h, const char *name);
extern int pthread_create(long *t, const void *attr, void *(*fn)(void *), void *arg);
extern int pthread_join(long t, void **ret);
extern int pthread_mutex_lock(void *m);
extern int pthread_mutex_unlock(void *m);
struct timespec64 { long sec, nsec; };
extern int clock_gettime(int clock, struct timespec64 *ts);

#define EXPORT __attribute__((visibility("default")))
#define W 640
#define H 480
#define VIDEO_W 1920
#define VIDEO_H 1080
#define VIDEO_RGBA_BYTES (VIDEO_W * VIDEO_H * 4)
#define VIDEO_BYTES (VIDEO_RGBA_BYTES + VIDEO_W * VIDEO_H * 3 / 2)
#define VIDEO_QUEUE 4
#define NUBIA_VIDEO_60_MODE 0x8004
#define MAX_ZOOM 4
#define JPEG_MAX (1 << 20)
#define JPEG_PIXEL_LIMIT (64 * 1024 * 1024)

struct hw_module;
struct hw_device;
struct hw_module_methods { int (*open)(const struct hw_module *m, const char *id, struct hw_device **dev); };
struct hw_module {
    uint32_t tag;
    uint16_t module_api_version, hal_api_version;
    const char *id, *name, *author;
    struct hw_module_methods *methods;
    void *dso;
    uint64_t reserved[25];
};
struct hw_device {
    uint32_t tag, version;
    struct hw_module *module;
    uint64_t reserved[12];
    int (*close)(struct hw_device *dev);
};
#define HARDWARE_MODULE_TAG 0x484d4f44
#define HARDWARE_DEVICE_TAG 0x48574454

struct camera_info {
    int facing, orientation;
    uint32_t device_version; const void *static_characteristics;
    int resource_cost; char **conflicting_devices; size_t conflicting_devices_length;
};
struct camera_module {
    struct hw_module common;
    int (*get_number_of_cameras)(void);
    int (*get_camera_info)(int id, struct camera_info *info);
    void *set_callbacks, *get_vendor_tag_ops, *open_legacy, *set_torch_mode, *init, *reserved[5];
};

struct gralloc_module {
    struct hw_module common;
    int (*registerBuffer)(const struct gralloc_module *m, const void *handle);
    int (*unregisterBuffer)(const struct gralloc_module *m, const void *handle);
    int (*lock)(const struct gralloc_module *m, const void *handle, int usage, int l, int t, int w, int h, void **vaddr);
    int (*unlock)(const struct gralloc_module *m, const void *handle);
};

/* hardware/camera3.h, device API 3.3 */
struct stream {
    int type; uint32_t width, height; int format; uint32_t usage, max_buffers;
    void *priv; int data_space, rotation; void *reserved[7];
};
struct stream_config { uint32_t num; struct stream **streams; uint32_t mode; };
struct stream_buffer { struct stream *stream; const void **buffer; int status, acquire_fence, release_fence; };
struct capture_request {
    uint32_t frame; const void *settings; struct stream_buffer *input;
    uint32_t num_out; const struct stream_buffer *out;
};
struct capture_result {
    uint32_t frame; const void *result; uint32_t num_out; const struct stream_buffer *out;
    const struct stream_buffer *input; uint32_t partial;
};
struct notify_msg {
    int type;
    union {
        struct { uint32_t frame; struct stream *stream; int code; } error;
        struct { uint32_t frame; uint64_t timestamp; } shutter;
        uint8_t generic[32];
    } m;
};
struct callback_ops {
    void (*result)(const struct callback_ops *cb, const struct capture_result *r);
    void (*notify)(const struct callback_ops *cb, const struct notify_msg *m);
};
struct camera3;
struct camera3_ops {
    int (*initialize)(struct camera3 *d, const struct callback_ops *cb);
    int (*configure_streams)(struct camera3 *d, struct stream_config *c);
    void *register_stream_buffers;
    const void *(*default_settings)(struct camera3 *d, int type);
    int (*process_capture_request)(struct camera3 *d, struct capture_request *r);
    void *get_metadata_vendor_tag_ops;
    void (*dump)(struct camera3 *d, int fd);
    int (*flush)(struct camera3 *d);
    void *reserved[8];
};

#define FORMAT_RGBA_8888 1
#define FORMAT_BLOB 0x21
#define FORMAT_IMPLEMENTATION_DEFINED 0x22
#define FORMAT_YCBCR_420_888 0x23
#define FORMAT_YV12 0x32315659
#define USAGE_SW_WRITE_OFTEN 0x30
#define USAGE_VIDEO_ENCODER 0x10000
#define MAX_BUFS 6
#define QUEUE 16

struct request { uint32_t frame; int nbuf; struct stream_buffer bufs[MAX_BUFS]; const void *handles[MAX_BUFS]; void *settings; };
struct still_size { int32_t width, height; };
struct video_frame { unsigned slot; uint64_t timestamp; };

struct camera3 {
    struct hw_device common;
    struct camera3_ops *ops;
    void *priv;
    /* ours */
    int id, fd;
    volatile int running;
    long reader, worker;
    const struct callback_ops *cb;
    uint64_t lock[8];               /* pthread_mutex_t, zero is its initial state */
    unsigned char *frame;           /* the last host frame: RGBA then NV21 */
    volatile unsigned frame_seq;
    unsigned char *jpeg; volatile unsigned jpeg_len, jpeg_seq;
    struct still_size still_size;
    int video_enabled, video_stream, pull_video;
    unsigned char *video_pixels;
    struct video_frame video_frames[VIDEO_QUEUE];
    unsigned video_head, video_tail;
    uint64_t video_timestamp;
    struct request queue[QUEUE];
    volatile unsigned head, tail;   /* worker takes at head, requests are put at tail */
    void *last;                     /* the settings of the last request that had any */
    void *templates[7];
};

/* ---- metadata (libcamera_metadata.so) ---- */
struct ro_entry { size_t index; uint32_t tag; uint8_t type; size_t count; const void *data; };
static void *(*md_alloc)(size_t entries, size_t data);
static int (*md_add)(void *m, uint32_t tag, const void *data, size_t count);
static int (*md_update)(void *m, size_t index, const void *data, size_t count, void *entry);
static int (*md_append)(void *dst, const void *src);
static size_t (*md_entries)(const void *m);
static size_t (*md_data)(const void *m);
static void (*md_free)(void *m);
static int (*md_find)(const void *m, uint32_t tag, struct ro_entry *e);
static int md_ready(void) {
    if (md_alloc) return 1;
    void *h = dlopen("libcamera_metadata.so", 2);
    if (!h) return 0;
    md_add = dlsym(h, "add_camera_metadata_entry");
    md_update = dlsym(h, "update_camera_metadata_entry");
    md_append = dlsym(h, "append_camera_metadata");
    md_entries = dlsym(h, "get_camera_metadata_entry_count");
    md_data = dlsym(h, "get_camera_metadata_data_count");
    md_free = dlsym(h, "free_camera_metadata");
    md_find = dlsym(h, "find_camera_metadata_ro_entry");
    void *a = dlsym(h, "allocate_camera_metadata");
    if (!md_add || !md_update || !md_append || !md_entries || !md_data || !md_free || !md_find || !a) return 0;
    md_alloc = a;
    return 1;
}

struct ent { uint32_t tag; uint32_t count; const void *data; };
#define B(tag, ...) { tag, sizeof((const uint8_t[]){__VA_ARGS__}), (const uint8_t[]){__VA_ARGS__} }
#define I(tag, ...) { tag, sizeof((const int32_t[]){__VA_ARGS__}) / 4, (const int32_t[]){__VA_ARGS__} }
#define F(tag, ...) { tag, sizeof((const float[]){__VA_ARGS__}) / 4, (const float[]){__VA_ARGS__} }
#define L(tag, ...) { tag, sizeof((const int64_t[]){__VA_ARGS__}) / 8, (const int64_t[]){__VA_ARGS__} }
#define R(tag, num, den) { tag, 1, (const int32_t[]){num, den} }

/* the sizes offered: the host's own and two more that camera applications look for (16:9, a small one) */
#define SIZES(fmt) fmt, 640, 480, 0, fmt, 1280, 720, 0, fmt, 320, 240, 0
#define DURATIONS(fmt, ns) fmt, 640, 480, ns, fmt, 1280, 720, ns, fmt, 320, 240, ns

static const struct ent STATIC[] = {
    B(COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, 0, 1),
    B(CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, 0, 3),
    B(CONTROL_AE_AVAILABLE_MODES, 1),
    I(CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, 15, 30, 30, 30),
    I(CONTROL_AE_COMPENSATION_RANGE, 0, 0),
    R(CONTROL_AE_COMPENSATION_STEP, 1, 1),
    B(CONTROL_AF_AVAILABLE_MODES, 0),
    B(CONTROL_AVAILABLE_EFFECTS, 0),
    B(CONTROL_AVAILABLE_SCENE_MODES, 0),
    B(CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, 0),
    B(CONTROL_AWB_AVAILABLE_MODES, 1),
    I(CONTROL_MAX_REGIONS, 0, 0, 0),
    B(CONTROL_SCENE_MODE_OVERRIDES, 1, 1, 0),
    B(CONTROL_AE_LOCK_AVAILABLE, 0),
    B(CONTROL_AWB_LOCK_AVAILABLE, 0),
    B(CONTROL_AVAILABLE_MODES, 1),
    /* Nubia Camera dereferences this even without RAW. A fixed unit gain is a
     * compatibility value; the API normally specifies null without RAW. */
    I(CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE, 100, 100),
    B(EDGE_AVAILABLE_EDGE_MODES, 0, 1),
    B(FLASH_INFO_AVAILABLE, 0),
    B(HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, 1),
    I(JPEG_AVAILABLE_THUMBNAIL_SIZES, 0, 0, 160, 120),
    I(JPEG_MAX_SIZE, JPEG_MAX),
    F(LENS_INFO_AVAILABLE_APERTURES, 2.8f),
    F(LENS_INFO_AVAILABLE_FILTER_DENSITIES, 0.0f),
    F(LENS_INFO_AVAILABLE_FOCAL_LENGTHS, 3.5f),
    B(LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, 0),
    F(LENS_INFO_HYPERFOCAL_DISTANCE, 0.0f),
    F(LENS_INFO_MINIMUM_FOCUS_DISTANCE, 0.0f),
    I(LENS_INFO_SHADING_MAP_SIZE, 1, 1),
    B(LENS_INFO_FOCUS_DISTANCE_CALIBRATION, 0),
    B(NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, 0, 1),
    I(REQUEST_MAX_NUM_OUTPUT_STREAMS, 0, 3, 1),
    I(REQUEST_MAX_NUM_INPUT_STREAMS, 0),
    B(REQUEST_PIPELINE_MAX_DEPTH, 4),
    I(REQUEST_PARTIAL_RESULT_COUNT, 1),
    B(REQUEST_AVAILABLE_CAPABILITIES, 0),
    /* No session parameters are supported. Keep the empty entry present for
     * clients that do not handle a null session-key list. */
    { REQUEST_AVAILABLE_SESSION_KEYS, 0, (const int32_t[]){0} },
    F(SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, MAX_ZOOM),
    I(SCALER_AVAILABLE_STREAM_CONFIGURATIONS, SIZES(FORMAT_IMPLEMENTATION_DEFINED), SIZES(FORMAT_YCBCR_420_888),
      FORMAT_BLOB, 640, 480, 0),
    L(SCALER_AVAILABLE_MIN_FRAME_DURATIONS, DURATIONS(FORMAT_IMPLEMENTATION_DEFINED, 33333333),
      DURATIONS(FORMAT_YCBCR_420_888, 33333333), FORMAT_BLOB, 640, 480, 33333333),
    L(SCALER_AVAILABLE_STALL_DURATIONS, DURATIONS(FORMAT_IMPLEMENTATION_DEFINED, 0),
      DURATIONS(FORMAT_YCBCR_420_888, 0), FORMAT_BLOB, 640, 480, 100000000),
    B(SCALER_CROPPING_TYPE, 0),
    I(SENSOR_INFO_ACTIVE_ARRAY_SIZE, 0, 0, W, H),
    I(SENSOR_INFO_SENSITIVITY_RANGE, 100, 100),
    L(SENSOR_INFO_EXPOSURE_TIME_RANGE, 1000000, 33333333),
    L(SENSOR_INFO_MAX_FRAME_DURATION, 66666666),
    F(SENSOR_INFO_PHYSICAL_SIZE, 3.2f, 2.4f),
    I(SENSOR_INFO_PIXEL_ARRAY_SIZE, W, H),
    B(SENSOR_INFO_TIMESTAMP_SOURCE, 0),
    I(SENSOR_AVAILABLE_TEST_PATTERN_MODES, 0),
    I(SENSOR_MAX_ANALOG_SENSITIVITY, 100),
    B(SHADING_AVAILABLE_MODES, 0, 1),
    B(STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, 0),
    I(STATISTICS_INFO_MAX_FACE_COUNT, 0),
    B(STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES, 0),
    B(STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES, 0),
    B(TONEMAP_AVAILABLE_TONE_MAP_MODES, 1, 2),
    I(SYNC_MAX_LATENCY, -1),
    B(INFO_SUPPORTED_HARDWARE_LEVEL, 0),    /* LIMITED */
    I(REQUEST_AVAILABLE_REQUEST_KEYS,
      COLOR_CORRECTION_ABERRATION_MODE, CONTROL_AE_ANTIBANDING_MODE, CONTROL_AE_EXPOSURE_COMPENSATION, CONTROL_AE_LOCK,
      CONTROL_AE_MODE, CONTROL_AE_TARGET_FPS_RANGE, CONTROL_AE_PRECAPTURE_TRIGGER, CONTROL_AF_MODE, CONTROL_AF_TRIGGER,
      CONTROL_AWB_LOCK, CONTROL_AWB_MODE, CONTROL_CAPTURE_INTENT, CONTROL_EFFECT_MODE, CONTROL_MODE, CONTROL_SCENE_MODE,
      CONTROL_VIDEO_STABILIZATION_MODE, EDGE_MODE, FLASH_MODE, HOT_PIXEL_MODE, JPEG_ORIENTATION, JPEG_QUALITY,
      JPEG_THUMBNAIL_QUALITY, JPEG_THUMBNAIL_SIZE, LENS_APERTURE, LENS_FILTER_DENSITY, LENS_FOCAL_LENGTH,
      LENS_FOCUS_DISTANCE, LENS_OPTICAL_STABILIZATION_MODE, NOISE_REDUCTION_MODE, SCALER_CROP_REGION, SHADING_MODE,
      STATISTICS_FACE_DETECT_MODE, STATISTICS_LENS_SHADING_MAP_MODE, TONEMAP_MODE),
    I(REQUEST_AVAILABLE_RESULT_KEYS,
      COLOR_CORRECTION_ABERRATION_MODE, CONTROL_AE_ANTIBANDING_MODE, CONTROL_AE_EXPOSURE_COMPENSATION, CONTROL_AE_LOCK,
      CONTROL_AE_MODE, CONTROL_AE_TARGET_FPS_RANGE, CONTROL_AE_PRECAPTURE_TRIGGER, CONTROL_AF_MODE, CONTROL_AF_TRIGGER,
      CONTROL_AWB_LOCK, CONTROL_AWB_MODE, CONTROL_CAPTURE_INTENT, CONTROL_EFFECT_MODE, CONTROL_MODE, CONTROL_SCENE_MODE,
      CONTROL_VIDEO_STABILIZATION_MODE, EDGE_MODE, FLASH_MODE, HOT_PIXEL_MODE, JPEG_ORIENTATION, JPEG_QUALITY,
      JPEG_THUMBNAIL_QUALITY, JPEG_THUMBNAIL_SIZE, LENS_APERTURE, LENS_FILTER_DENSITY, LENS_FOCAL_LENGTH,
      LENS_FOCUS_DISTANCE, LENS_OPTICAL_STABILIZATION_MODE, NOISE_REDUCTION_MODE, SCALER_CROP_REGION, SHADING_MODE,
      STATISTICS_FACE_DETECT_MODE, STATISTICS_LENS_SHADING_MAP_MODE, TONEMAP_MODE,
      CONTROL_AE_STATE, CONTROL_AF_STATE, CONTROL_AWB_STATE, FLASH_STATE, LENS_STATE, REQUEST_PIPELINE_DEPTH,
      SENSOR_TIMESTAMP, SENSOR_ROLLING_SHUTTER_SKEW, SENSOR_EXPOSURE_TIME, SENSOR_SENSITIVITY,
      CONTROL_POST_RAW_SENSITIVITY_BOOST, STATISTICS_SCENE_FLICKER),
    I(REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
      COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, CONTROL_AE_AVAILABLE_MODES,
      CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, CONTROL_AE_COMPENSATION_RANGE, CONTROL_AE_COMPENSATION_STEP,
      CONTROL_AF_AVAILABLE_MODES, CONTROL_AVAILABLE_EFFECTS, CONTROL_AVAILABLE_SCENE_MODES,
      CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, CONTROL_AWB_AVAILABLE_MODES, CONTROL_MAX_REGIONS,
      CONTROL_AE_LOCK_AVAILABLE, CONTROL_AWB_LOCK_AVAILABLE, CONTROL_AVAILABLE_MODES,
      CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE, EDGE_AVAILABLE_EDGE_MODES,
      FLASH_INFO_AVAILABLE, HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, JPEG_AVAILABLE_THUMBNAIL_SIZES, JPEG_MAX_SIZE,
      LENS_FACING, LENS_INFO_AVAILABLE_APERTURES, LENS_INFO_AVAILABLE_FILTER_DENSITIES,
      LENS_INFO_AVAILABLE_FOCAL_LENGTHS, LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, LENS_INFO_HYPERFOCAL_DISTANCE,
      LENS_INFO_MINIMUM_FOCUS_DISTANCE, LENS_INFO_SHADING_MAP_SIZE, LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
      NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, REQUEST_MAX_NUM_OUTPUT_STREAMS, REQUEST_MAX_NUM_INPUT_STREAMS,
      REQUEST_PIPELINE_MAX_DEPTH, REQUEST_PARTIAL_RESULT_COUNT, REQUEST_AVAILABLE_CAPABILITIES,
      REQUEST_AVAILABLE_REQUEST_KEYS, REQUEST_AVAILABLE_RESULT_KEYS, REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
      REQUEST_AVAILABLE_SESSION_KEYS,
      SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, SCALER_AVAILABLE_STREAM_CONFIGURATIONS, SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
      SCALER_AVAILABLE_STALL_DURATIONS, SCALER_CROPPING_TYPE, SENSOR_INFO_ACTIVE_ARRAY_SIZE,
      SENSOR_INFO_SENSITIVITY_RANGE, SENSOR_INFO_EXPOSURE_TIME_RANGE, SENSOR_INFO_MAX_FRAME_DURATION,
      SENSOR_INFO_PHYSICAL_SIZE, SENSOR_INFO_PIXEL_ARRAY_SIZE, SENSOR_INFO_TIMESTAMP_SOURCE, SENSOR_ORIENTATION,
      SENSOR_AVAILABLE_TEST_PATTERN_MODES, SENSOR_MAX_ANALOG_SENSITIVITY, SHADING_AVAILABLE_MODES,
      STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, STATISTICS_INFO_MAX_FACE_COUNT,
      STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES, STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
      TONEMAP_AVAILABLE_TONE_MAP_MODES, SYNC_MAX_LATENCY, INFO_SUPPORTED_HARDWARE_LEVEL),
};

/* a request template; CONTROL_CAPTURE_INTENT is added per template */
static const struct ent TEMPLATE[] = {
    B(COLOR_CORRECTION_ABERRATION_MODE, 1),
    B(CONTROL_AE_ANTIBANDING_MODE, 3),
    I(CONTROL_AE_EXPOSURE_COMPENSATION, 0),
    B(CONTROL_AE_LOCK, 0),
    B(CONTROL_AE_MODE, 1),
    I(CONTROL_AE_TARGET_FPS_RANGE, 15, 30),
    B(CONTROL_AE_PRECAPTURE_TRIGGER, 0),
    B(CONTROL_AF_MODE, 0),
    B(CONTROL_AF_TRIGGER, 0),
    B(CONTROL_AWB_LOCK, 0),
    B(CONTROL_AWB_MODE, 1),
    B(CONTROL_EFFECT_MODE, 0),
    B(CONTROL_MODE, 1),
    B(CONTROL_SCENE_MODE, 0),
    B(CONTROL_VIDEO_STABILIZATION_MODE, 0),
    B(EDGE_MODE, 1),
    B(FLASH_MODE, 0),
    B(HOT_PIXEL_MODE, 1),
    I(JPEG_ORIENTATION, 0),
    B(JPEG_QUALITY, 90),
    B(JPEG_THUMBNAIL_QUALITY, 90),
    I(JPEG_THUMBNAIL_SIZE, 0, 0),
    F(LENS_APERTURE, 2.8f),
    F(LENS_FILTER_DENSITY, 0.0f),
    F(LENS_FOCAL_LENGTH, 3.5f),
    F(LENS_FOCUS_DISTANCE, 0.0f),
    B(LENS_OPTICAL_STABILIZATION_MODE, 0),
    B(NOISE_REDUCTION_MODE, 1),
    I(SCALER_CROP_REGION, 0, 0, W, H),
    B(SHADING_MODE, 1),
    B(STATISTICS_FACE_DETECT_MODE, 0),
    B(STATISTICS_LENS_SHADING_MAP_MODE, 0),
    B(TONEMAP_MODE, 1),
};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void *md_build(const struct ent *e, unsigned n, unsigned spare) {
    if (!md_ready()) return 0;
    unsigned long data = 0;
    for (unsigned i = 0; i < n; i++) data += (unsigned long)e[i].count * 8 + 8;
    void *m = md_alloc(n + spare, data + spare * 16);
    for (unsigned i = 0; m && i < n; i++) md_add(m, e[i].tag, e[i].data, e[i].count);
    return m;
}

static uint32_t vendor_tag(const char *section, const char *name);
static struct still_size still_size_for(int id) {
    struct still_size sizes[2];
    FILE *caps = fopen("/dev/.aemu-camera-jpeg", "rb");
    if (caps) {
        size_t n = fread(sizes, sizeof(sizes[0]), COUNT(sizes), caps);
        fclose(caps);
        if (n == COUNT(sizes)) {
            struct still_size s = sizes[id];
            if (s.width > 0 && s.height > 0 && (int64_t)s.width * s.height <= JPEG_PIXEL_LIMIT)
                return s;
        }
    }
    return (struct still_size){W, H};
}

static int jpeg_capacity(struct still_size s) {
    int64_t bytes = (int64_t)s.width * s.height * 2 + 65536;
    return bytes > JPEG_MAX ? (int)bytes : JPEG_MAX;
}

static int rear_video_enabled(int id) {
    if (id != 0) return 0;
    FILE *file = fopen("/dev/.aemu-camera-video", "rb");
    if (!file) return 0;
    int enabled = fgetc(file) == '1';
    fclose(file);
    return enabled;
}

static void *build_static_info(int id, struct still_size still, int video) {
    struct ent entries[COUNT(STATIC)];
    const int32_t jpeg_max = jpeg_capacity(still);
    const int32_t fps_ranges[] = {15, 30, 30, 30, 50, 60, 60, 60};
    const int32_t formats[] = {
        SIZES(FORMAT_IMPLEMENTATION_DEFINED), SIZES(FORMAT_YCBCR_420_888),
        FORMAT_BLOB, still.width, still.height, 0,
        FORMAT_IMPLEMENTATION_DEFINED, VIDEO_W, VIDEO_H, 0,
    };
    const int64_t durations[] = {
        DURATIONS(FORMAT_IMPLEMENTATION_DEFINED, 33333333), DURATIONS(FORMAT_YCBCR_420_888, 33333333),
        FORMAT_BLOB, still.width, still.height, 33333333,
        FORMAT_IMPLEMENTATION_DEFINED, VIDEO_W, VIDEO_H, 16666666,
    };
    const int64_t stalls[] = {
        DURATIONS(FORMAT_IMPLEMENTATION_DEFINED, 0), DURATIONS(FORMAT_YCBCR_420_888, 0),
        FORMAT_BLOB, still.width, still.height, 100000000,
        FORMAT_IMPLEMENTATION_DEFINED, VIDEO_W, VIDEO_H, 0,
    };
    for (size_t i = 0; i < COUNT(STATIC); i++) {
        entries[i] = STATIC[i];
        switch (entries[i].tag) {
        case JPEG_MAX_SIZE: entries[i].data = &jpeg_max; break;
        case CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES:
            if (video) { entries[i].data = fps_ranges; entries[i].count = COUNT(fps_ranges); }
            break;
        case SCALER_AVAILABLE_STREAM_CONFIGURATIONS:
            entries[i].data = formats;
            if (video) entries[i].count = COUNT(formats);
            break;
        case SCALER_AVAILABLE_MIN_FRAME_DURATIONS:
            entries[i].data = durations;
            if (video) entries[i].count = COUNT(durations);
            break;
        case SCALER_AVAILABLE_STALL_DURATIONS:
            entries[i].data = stalls;
            if (video) entries[i].count = COUNT(stalls);
            break;
        }
    }
    void *m = md_build(entries, COUNT(entries), 4);
    if (!m) return 0;
    uint8_t facing = id ? 0 : 1;            /* LENS_FACING: 0 front, 1 back */
    int32_t orientation = id ? 270 : 90;    /* the phone's sensors are mounted sideways, frames come unrotated */
    md_add(m, LENS_FACING, &facing, 1);
    md_add(m, SENSOR_ORIENTATION, &orientation, 1);
    /* Xiaomi's camera application finds its cameras by role: 0 the main back one, 1 the front one */
    int32_t role = id;
    uint32_t tag = vendor_tag("com.xiaomi.cameraid.role", "cameraId");
    if (tag) md_add(m, tag, &role, 1);
    return m;
}

static void *g_static[2];
static const void *static_info(int id) {
    if (!g_static[id]) g_static[id] = build_static_info(id, still_size_for(id), rear_video_enabled(id));
    return g_static[id];
}

/* ---- helpers ---- */
static unsigned long slen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
static void copy(void *d, const void *s, unsigned long n) {
    unsigned char *a = d; const unsigned char *b = s;
    while (n >= 8) { *(uint64_t *)a = *(const uint64_t *)b; a += 8; b += 8; n -= 8; }
    while (n--) *a++ = *b++;
}
static const struct gralloc_module *g_gralloc;
static const struct gralloc_module *gralloc(void) {
    if (!g_gralloc) {
        void *h = dlopen("libhardware.so", 0);
        int (*get)(const char *, const struct hw_module **) = h ? dlsym(h, "hw_get_module") : 0;
        const struct hw_module *m = 0;
        if (get && get("gralloc", &m) == 0) g_gralloc = (const struct gralloc_module *)m;
    }
    return g_gralloc;
}
static int host_connect(void) {
    struct { uint16_t family; char path[108]; } a;
    static const char P[] = "/dev/socket/aemu_cam";
    for (unsigned i = 0; i < sizeof(a.path); i++) a.path[i] = i < sizeof(P) ? P[i] : 0;
    a.family = 1; /* AF_UNIX */
    int fd = socket(1, 1 /* SOCK_STREAM */, 0);
    if (fd < 0) return -1;
    if (connect(fd, (const struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return -1; }
    return fd;
}
static void say(struct camera3 *d, const char *cmd) { if (d->fd >= 0) write(d->fd, cmd, slen(cmd)); }
static int read_full(int fd, void *buf, unsigned long n) {
    char *p = buf;
    while (n) { long k = read(fd, p, n); if (k <= 0) return -1; p += k; n -= (unsigned long)k; }
    return 0;
}
static int skip(int fd, unsigned long len) {
    unsigned char t[512];
    while (len) { unsigned long k = len < sizeof(t) ? len : sizeof(t); if (read_full(fd, t, k) < 0) return -1; len -= k; }
    return 0;
}
static uint64_t now_ns(void) {
    struct timespec64 ts = { 0, 0 };
    clock_gettime(1 /* CLOCK_MONOTONIC */, &ts);
    return (uint64_t)ts.sec * 1000000000ull + (uint64_t)ts.nsec;
}

struct crop { int left, top, width, height; };

static struct crop request_crop(const void *settings) {
    struct ro_entry entry;
    if (!settings || md_find(settings, SCALER_CROP_REGION, &entry) != 0 || entry.count != 4)
        return (struct crop){0, 0, W, H};

    /* CameraService maps CONTROL_ZOOM_RATIO to the center-only crop region. */
    const int32_t *crop = entry.data;
    int width = crop[2], height = crop[3];
    if (width <= 0 || height <= 0) return (struct crop){0, 0, W, H};
    if (width < W / MAX_ZOOM) width = W / MAX_ZOOM;
    if (height < H / MAX_ZOOM) height = H / MAX_ZOOM;
    if (width > W) width = W;
    if (height > H) height = H;
    return (struct crop){(W - width) / 2, (H - height) / 2, width, height};
}

static void put_rgba(unsigned char *dst, int dw, int dh, const unsigned char *src,
                     int sw, int sh, struct crop crop) {
    if (dw == sw && dh == sh && crop.width == sw && crop.height == sh) {
        memcpy(dst, src, (size_t)sw * sh * 4);
        return;
    }
    if (dw < 1 || dh < 1 || dw > VIDEO_W) return;
    int columns[VIDEO_W];
    for (int x = 0; x < dw; x++) columns[x] = x * crop.width / dw;
    for (int y = 0; y < dh; y++) {
        const uint32_t *row = (const uint32_t *)src + (crop.top + y * crop.height / dh) * sw + crop.left;
        uint32_t *o = (uint32_t *)(dst + (unsigned long)y * dw * 4);
        for (int x = 0; x < dw; x++) o[x] = row[columns[x]];
    }
}
static void put_nv21(unsigned char *dst, int dw, int dh, const unsigned char *src, struct crop crop) {
    if (dw == W && dh == H && crop.width == W && crop.height == H) {
        copy(dst, src, (unsigned long)W * H * 3 / 2);
        return;
    }
    for (int y = 0; y < dh; y++) {
        const unsigned char *row = src + (crop.top + y * crop.height / dh) * W + crop.left;
        unsigned char *o = dst + (unsigned long)y * dw;
        for (int x = 0; x < dw; x++) o[x] = row[x * crop.width / dw];
    }
    const unsigned char *suv = src + W * H;
    unsigned char *duv = dst + (unsigned long)dw * dh;
    for (int y = 0; y < dh; y += 2) {
        const unsigned char *row = suv + ((crop.top + y * crop.height / dh) / 2) * W;
        unsigned char *o = duv + (unsigned long)(y / 2) * dw;
        for (int x = 0; x < dw; x += 2) {
            int sx = (crop.left + x * crop.width / dw) & ~1;
            o[x] = row[sx];
            o[x + 1] = row[sx + 1];
        }
    }
}

static void put_plane(unsigned char *dst, int stride, int dw, int dh, const unsigned char *src,
                      int source_stride, int step, struct crop crop) {
    src += crop.top * source_stride + crop.left * step;
    if (crop.width == dw && crop.height == dh && step == 1 && stride == dw && source_stride == dw) {
        memcpy(dst, src, (size_t)dw * dh);
        return;
    }
    for (int y = 0; y < dh; y++) {
        const unsigned char *row = src + (y * crop.height / dh) * source_stride;
        unsigned char *output = dst + y * stride;
        if (step == 1 && crop.width == dw) memcpy(output, row, (size_t)dw);
        else for (int x = 0; x < dw; x++) output[x] = row[(x * crop.width / dw) * step];
    }
}

static void put_yv12(unsigned char *dst, int dw, int dh, const unsigned char *src,
                     int sw, int sh, struct crop crop, int nv21) {
    const int y_stride = (dw + 15) & ~15;
    const int c_stride = (y_stride / 2 + 15) & ~15;
    const unsigned char *v = src + sw * sh;
    const unsigned char *u = v + (nv21 ? 1 : sw * sh / 4);
    unsigned char *out_v = dst + y_stride * dh;
    unsigned char *out_u = out_v + c_stride * (dh / 2);
    put_plane(dst, y_stride, dw, dh, src, sw, 1, crop);
    const struct crop chroma = {crop.left / 2, crop.top / 2, crop.width / 2, crop.height / 2};
    const int step = nv21 ? 2 : 1;
    put_plane(out_v, c_stride, dw / 2, dh / 2, v, sw / 2 * step, step, chroma);
    put_plane(out_u, c_stride, dw / 2, dh / 2, u, sw / 2 * step, step, chroma);
}

/* ---- host frames ---- */
static int frame_header(int fd, int32_t hdr[4], int *shared) {
    unsigned char control[CMSG_SPACE(sizeof(int))];
    struct iovec data = {hdr, sizeof(int32_t) * 4};
    struct msghdr message = {.msg_iov = &data, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof(control)};
    long received = recvmsg(fd, &message, MSG_WAITALL | MSG_CMSG_CLOEXEC);
    if (received <= 0) return -1;
    struct cmsghdr *item = CMSG_FIRSTHDR(&message);
    if (item && item->cmsg_level == SOL_SOCKET && item->cmsg_type == SCM_RIGHTS &&
        item->cmsg_len == CMSG_LEN(sizeof(int))) memcpy(shared, CMSG_DATA(item), sizeof(int));
    if (received == (long)data.iov_len) return 0;
    return read_full(fd, (char *)hdr + received, data.iov_len - (size_t)received);
}

static void release_slot(struct camera3 *d, unsigned slot) {
    char command[20];
    snprintf(command, sizeof(command), "release %u\n", slot);
    say(d, command);
}

static void *reader(void *arg) {
    struct camera3 *d = arg;
    unsigned long flen = W * H * 4 + W * H * 3 / 2;
    unsigned char *back = malloc(flen);
    while (d->running && back) {
        int32_t hdr[4];
        int shared = -1;
        int rc = d->video_enabled ? frame_header(d->fd, hdr, &shared) : read_full(d->fd, hdr, sizeof(hdr));
        if (rc < 0) { if (shared >= 0) close(shared); break; }
        unsigned long len = (unsigned long)hdr[3];
        if (shared >= 0) {
            if (hdr[0] == 6 && hdr[1] == VIDEO_W && hdr[2] == VIDEO_H && len == 0 && !d->video_pixels) {
                void *region = mmap(0, (size_t)VIDEO_QUEUE * VIDEO_BYTES, PROT_READ, MAP_SHARED, shared, 0);
                if (region != MAP_FAILED) d->video_pixels = region;
            }
            close(shared);
        }
        if (hdr[0] == 1 && hdr[1] == W && hdr[2] == H && len == flen) {
            if (read_full(d->fd, back, len) < 0) break;
            pthread_mutex_lock(d->lock);
            unsigned char *t = d->frame; d->frame = back; back = t;
            d->frame_seq++;
            pthread_mutex_unlock(d->lock);
            if (!back) back = malloc(flen);     /* the first frame: there was no older one to take over */
        } else if (hdr[0] == 5 && d->video_pixels && hdr[1] == VIDEO_W && hdr[2] == VIDEO_H && len == 12) {
            uint64_t timestamp;
            uint32_t slot;
            if (read_full(d->fd, &timestamp, sizeof(timestamp)) < 0 || read_full(d->fd, &slot, sizeof(slot)) < 0) break;
            if (slot >= VIDEO_QUEUE) continue;
            pthread_mutex_lock(d->lock);
            if (timestamp > d->video_timestamp && d->video_tail - d->video_head < VIDEO_QUEUE) {
                d->video_frames[d->video_tail++ % VIDEO_QUEUE] = (struct video_frame){slot, timestamp};
                d->video_timestamp = timestamp;
            } else release_slot(d, slot);
            pthread_mutex_unlock(d->lock);
        } else if (hdr[0] == 2 && hdr[1] == d->still_size.width && hdr[2] == d->still_size.height &&
                   len > 0 && len <= (unsigned long)jpeg_capacity(d->still_size) - 8) {
            unsigned char *j = malloc(len);
            if (!j) { if (skip(d->fd, len) < 0) break; continue; }
            if (read_full(d->fd, j, len) < 0) { free(j); break; }
            pthread_mutex_lock(d->lock);
            free(d->jpeg); d->jpeg = j; d->jpeg_len = (unsigned)len;
            d->jpeg_seq++;
            pthread_mutex_unlock(d->lock);
        } else if (skip(d->fd, len) < 0) break;
    }
    free(back);
    return 0;
}

/* ---- requests ---- */
static void *result_metadata(struct camera3 *d, uint64_t ts) {
    const void *s = d->last ? d->last : d->templates[1];
    void *m = md_alloc((s ? md_entries(s) : 0) + 13, (s ? md_data(s) : 0) + 160);
    if (!m) return 0;
    if (s) md_append(m, s);
    static const uint8_t converged = 2, zero = 0, depth = 1;
    static const int64_t skew = 0;
    /* Nominal values of this virtual camera, not measurements from the host.
     * Exposure is one 30 fps frame; ISO and boost match the advertised ranges. */
    const int64_t virtual_exposure_ns = d->pull_video ? 16666666 : 33333333;
    static const int32_t virtual_iso = 100, virtual_boost = 100;
    int64_t t = (int64_t)ts;
    md_add(m, SENSOR_TIMESTAMP, &t, 1);
    md_add(m, REQUEST_PIPELINE_DEPTH, &depth, 1);
    md_add(m, CONTROL_AE_STATE, &converged, 1);
    md_add(m, CONTROL_AF_STATE, &zero, 1);
    md_add(m, CONTROL_AWB_STATE, &converged, 1);
    md_add(m, FLASH_STATE, &zero, 1);
    md_add(m, LENS_STATE, &zero, 1);
    md_add(m, STATISTICS_SCENE_FLICKER, &zero, 1);
    md_add(m, SENSOR_ROLLING_SHUTTER_SKEW, &skew, 1);
    md_add(m, SENSOR_EXPOSURE_TIME, &virtual_exposure_ns, 1);
    md_add(m, SENSOR_SENSITIVITY, &virtual_iso, 1);
    md_add(m, CONTROL_POST_RAW_SENSITIVITY_BOOST, &virtual_boost, 1);
    return m;
}

/* Both image pointers and their sequence numbers are published under lock. */
static int has_new_frame(struct camera3 *d, unsigned fseq, unsigned jseq, int preview, int blob) {
    pthread_mutex_lock(d->lock);
    int fresh_preview = d->pull_video ? d->video_head != d->video_tail
                                     : d->frame && d->frame_seq != fseq;
    int ready = (!preview || fresh_preview) &&
                (!blob || (d->jpeg && d->jpeg_seq != jseq));
    pthread_mutex_unlock(d->lock);
    return ready;
}

static void fail_no_frame(struct camera3 *d, struct request *r) {
    struct notify_msg n = {0};
    n.type = 1; /* CAMERA3_MSG_ERROR */
    n.m.error.frame = r->frame;
    n.m.error.code = 2; /* CAMERA3_MSG_ERROR_REQUEST */
    d->cb->notify(d->cb, &n);
    for (int i = 0; i < r->nbuf; i++) {
        struct stream_buffer *b = &r->bufs[i];
        b->status = 1; /* CAMERA3_BUFFER_STATUS_ERROR */
        /* No write or fence wait occurred; framework must wait before reuse. */
        b->release_fence = b->acquire_fence;
        b->acquire_fence = -1;
    }
    struct capture_result res = { r->frame, 0, (uint32_t)r->nbuf, r->bufs, 0, 0 };
    d->cb->result(d->cb, &res);
}

static void retire_video_frame(struct camera3 *d) {
    unsigned slot = d->video_frames[d->video_head++ % VIDEO_QUEUE].slot;
    release_slot(d, slot);
}

static void select_video(struct camera3 *d, int selected) {
    if (selected == d->pull_video) return;
    pthread_mutex_lock(d->lock);
    while (d->video_head != d->video_tail) retire_video_frame(d);
    pthread_mutex_unlock(d->lock);
    d->pull_video = selected;
    say(d, selected ? "video 1\n" : "video 0\n");
}

static void answer(struct camera3 *d, struct request *r) {
    const struct gralloc_module *g = gralloc();
    int blob = 0, preview = 0;
    for (int i = 0; i < r->nbuf; i++) {
        if (r->bufs[i].stream->format == FORMAT_BLOB) blob = 1;
        else preview = 1;
    }
    if (r->settings) { if (d->last) md_free(d->last); d->last = r->settings; r->settings = 0; }
    struct crop crop = request_crop(d->last);
    struct ro_entry fps;
    const int fast = d->video_stream && d->last &&
                     md_find(d->last, CONTROL_AE_TARGET_FPS_RANGE, &fps) == 0 && fps.count == 2 &&
                     ((const int32_t *)fps.data)[1] == 60;
    select_video(d, fast);

    /* the picture: the host compresses the next frame; otherwise the next preview frame (a third of a second at
     * most: nothing comes while the phone's camera permission is being asked) */
    pthread_mutex_lock(d->lock);
    unsigned jseq = d->jpeg_seq, fseq = d->frame_seq;
    pthread_mutex_unlock(d->lock);
    if (blob) {
        char cmd[48];
        if (crop.width == W && crop.height == H) say(d, "shot\n");
        else {
            snprintf(cmd, sizeof(cmd), "shot %d %d %d %d\n", crop.left, crop.top, crop.width, crop.height);
            say(d, cmd);
        }
        for (int i = 0; i < 1500 && d->running && !has_new_frame(d, fseq, jseq, 0, 1); i++) usleep(2000);
        /* The host stops preview for JPEG. Frames received before that JPEG
         * cannot satisfy a preview output of this mixed request. */
        pthread_mutex_lock(d->lock);
        fseq = d->frame_seq;
        while (d->video_head != d->video_tail) retire_video_frame(d);
        pthread_mutex_unlock(d->lock);
        say(d, "start\n");      /* a picture stops the host's preview */
    }
    if (preview && (!blob || has_new_frame(d, fseq, jseq, 0, 1)))
        for (int i = 0; i < (fast ? 1200 : 150) && d->running && !has_new_frame(d, fseq, jseq, 1, blob); i++) usleep(fast ? 1000 : 2000);
    if (!d->running || !has_new_frame(d, fseq, jseq, preview, blob)) {
        fail_no_frame(d, r);
        return;
    }

    pthread_mutex_lock(d->lock);
    const struct video_frame *video_frame = fast && preview ? &d->video_frames[d->video_head % VIDEO_QUEUE] : 0;
    uint64_t ts = video_frame ? video_frame->timestamp : now_ns();
    struct crop video_crop = { crop.left * VIDEO_W / W, crop.top * VIDEO_H / H,
                               crop.width * VIDEO_W / W, crop.height * VIDEO_H / H };
    for (int i = 0; i < r->nbuf; i++) {
        struct stream_buffer *b = &r->bufs[i];
        struct stream *s = b->stream;
        if (b->acquire_fence >= 0) close(b->acquire_fence);
        b->acquire_fence = -1; b->release_fence = -1;
        b->status = 1;      /* CAMERA3_BUFFER_STATUS_ERROR until filled */
        void *va = 0;
        if (!g || !b->buffer || g->lock(g, *b->buffer, USAGE_SW_WRITE_OFTEN, 0, 0, (int)s->width, (int)s->height, &va) != 0 || !va) continue;
        if (s->format == FORMAT_BLOB) {
            /* the JPEG, and at the very end of the buffer camera3_jpeg_blob { uint16 id = 0xff, uint32 size }.
             * There is one JPEG size, so CameraService uses the advertised JPEG_MAX_SIZE.
             * The stream dimensions describe the image, not the allocation. */
            /* (the engine's gralloc handle: fd, magic, flags, size - the block must hold that many bytes) */
            int capacity = jpeg_capacity(d->still_size);
            if (d->jpeg_seq != jseq && d->jpeg && d->jpeg_len + 8 <= (unsigned)capacity &&
                ((const int *)*b->buffer)[6] >= capacity) {
                copy(va, d->jpeg, d->jpeg_len);
                struct { uint16_t id; uint32_t size; } tail = { 0x00ff, d->jpeg_len };
                copy((char *)va + capacity - sizeof(tail), &tail, sizeof(tail));
                b->status = 0;
            }
        } else if (s->format == FORMAT_YV12 && (video_frame || d->frame)) {
            const unsigned char *pixels = video_frame
                ? d->video_pixels + video_frame->slot * VIDEO_BYTES + VIDEO_RGBA_BYTES
                : d->frame + W * H * 4;
            put_yv12(va, (int)s->width, (int)s->height, pixels,
                     video_frame ? VIDEO_W : W, video_frame ? VIDEO_H : H,
                     video_frame ? video_crop : crop, !video_frame);
            b->status = 0;
        } else if (video_frame) {
            if (s->format == FORMAT_RGBA_8888) {
                put_rgba(va, (int)s->width, (int)s->height,
                         d->video_pixels + video_frame->slot * VIDEO_BYTES, VIDEO_W, VIDEO_H, video_crop);
                b->status = 0;
            }
        } else if (d->frame) {
            if (s->format == FORMAT_YCBCR_420_888)
                put_nv21(va, (int)s->width, (int)s->height, d->frame + W * H * 4, crop);
            else put_rgba(va, (int)s->width, (int)s->height, d->frame, W, H, crop);
            b->status = 0;
        }
        g->unlock(g, *b->buffer);
    }
    if (video_frame) retire_video_frame(d);
    pthread_mutex_unlock(d->lock);

    struct notify_msg n = {0};
    n.type = 2;     /* CAMERA3_MSG_SHUTTER */
    n.m.shutter.frame = r->frame;
    n.m.shutter.timestamp = ts;
    d->cb->notify(d->cb, &n);

    void *meta = result_metadata(d, ts);
    if (meta) {
        const int32_t region[] = { crop.left, crop.top, crop.width, crop.height };
        struct ro_entry entry;
        if (md_find(meta, SCALER_CROP_REGION, &entry) == 0)
            md_update(meta, entry.index, region, COUNT(region), 0);
        else
            md_add(meta, SCALER_CROP_REGION, region, COUNT(region));
    }
    struct capture_result res = { r->frame, meta, (uint32_t)r->nbuf, r->bufs, 0, 1 };
    d->cb->result(d->cb, &res);
    if (meta) md_free(meta);
}

static void *worker(void *arg) {
    struct camera3 *d = arg;
    while (d->running) {
        if (d->head == d->tail) { usleep(1000); continue; }
        struct request *r = &d->queue[d->head % QUEUE];
        answer(d, r);
        d->head++;
    }
    return 0;
}

static int d_initialize(struct camera3 *d, const struct callback_ops *cb) { d->cb = cb; return 0; }

static int d_configure(struct camera3 *d, struct stream_config *c) {
    if (!c || !c->num) return -22;
    for (int i = 0; i < 500 && d->head != d->tail; i++) usleep(2000);
    int video_stream = c->mode == NUBIA_VIDEO_60_MODE;
    for (uint32_t i = 0; i < c->num; i++) {
        struct stream *s = c->streams[i];
        if (!s || s->type != 0 /* CAMERA3_STREAM_OUTPUT */) return -22;
        const int full_hd = s->width == VIDEO_W && s->height == VIDEO_H;
        if (s->format == FORMAT_IMPLEMENTATION_DEFINED) {
            const int encoder = d->video_enabled && full_hd && (s->usage & USAGE_VIDEO_ENCODER);
            s->format = encoder ? FORMAT_YV12 : FORMAT_RGBA_8888;
        }
        if (s->format != FORMAT_RGBA_8888 && s->format != FORMAT_YCBCR_420_888 &&
            s->format != FORMAT_YV12 && s->format != FORMAT_BLOB) return -22;
        if ((s->format == FORMAT_YCBCR_420_888 || s->format == FORMAT_YV12) &&
            ((s->width | s->height) & 1)) return -22;
        if ((s->format == FORMAT_RGBA_8888 || s->format == FORMAT_YV12) && full_hd) video_stream = 1;
        s->usage |= USAGE_SW_WRITE_OFTEN;
        s->max_buffers = 4;
    }
    d->video_stream = d->video_enabled && video_stream;
    if (!d->video_stream) select_video(d, 0);
    say(d, "start\n");
    return 0;
}

static const void *d_template(struct camera3 *d, int type) {
    if (type < 1 || type > 6) return 0;
    if (!d->templates[type]) {
        void *m = md_build(TEMPLATE, COUNT(TEMPLATE), 4);
        if (!m) return 0;
        uint8_t intent = (uint8_t)type;     /* CONTROL_CAPTURE_INTENT values are the template numbers */
        md_add(m, CONTROL_CAPTURE_INTENT, &intent, 1);
        d->templates[type] = m;
    }
    return d->templates[type];
}

static int d_request(struct camera3 *d, struct capture_request *q) {
    if (!q || !q->num_out || q->num_out > MAX_BUFS || !q->out || q->input) return -22;
    for (uint32_t i = 0; i < q->num_out; i++)
        if (!q->out[i].stream || !q->out[i].buffer || !*q->out[i].buffer) return -22;
    if (!q->settings && !d->last && !d_template(d, 1)) return -22;
    for (int i = 0; i < 2000 && d->tail - d->head >= QUEUE - 1; i++) usleep(2000);
    if (d->tail - d->head >= QUEUE - 1) return -19;
    struct request *r = &d->queue[d->tail % QUEUE];
    r->frame = q->frame;
    r->nbuf = (int)q->num_out;
    for (int i = 0; i < r->nbuf; i++) {
        r->handles[i] = *q->out[i].buffer;
        r->bufs[i] = q->out[i];
        r->bufs[i].buffer = &r->handles[i];
    }
    r->settings = 0;
    if (q->settings) {
        r->settings = md_alloc(md_entries(q->settings) + 2, md_data(q->settings) + 16);
        if (r->settings) md_append(r->settings, q->settings);
    }
    d->tail++;
    return 0;
}

static void d_dump(struct camera3 *d, int fd) { (void)d; (void)fd; }
static int d_flush(struct camera3 *d) {
    for (int i = 0; i < 1000 && d->head != d->tail; i++) usleep(2000);
    return 0;
}

static struct camera3_ops ops = {
    .initialize = d_initialize, .configure_streams = d_configure, .default_settings = d_template,
    .process_capture_request = d_request, .dump = d_dump, .flush = d_flush,
};

static int dev_close(struct hw_device *h) {
    struct camera3 *d = (struct camera3 *)h;
    d->running = 0;
    say(d, "close\n");
    shutdown(d->fd, 2);     /* SHUT_RDWR wakes the reader blocked in read() */
    pthread_join(d->reader, 0);
    pthread_join(d->worker, 0);
    close(d->fd);
    for (unsigned i = d->head; i != d->tail; i++) if (d->queue[i % QUEUE].settings) md_free(d->queue[i % QUEUE].settings);
    for (int i = 0; i < 7; i++) if (d->templates[i]) md_free(d->templates[i]);
    if (d->last) md_free(d->last);
    free(d->frame); free(d->jpeg);
    if (d->video_pixels) munmap(d->video_pixels, (size_t)VIDEO_QUEUE * VIDEO_BYTES);
    free(d);
    return 0;
}

static int cam_open(const struct hw_module *m, const char *id, struct hw_device **out) {
    int n = (id && id[0] >= '0' && id[0] <= '9') ? id[0] - '0' : 0;
    if (n > 1 || (id && id[1])) return -22;
    if (!md_ready()) return -19;
    struct camera3 *d = calloc(1, sizeof(*d));
    if (!d) return -12;
    d->common.tag = HARDWARE_DEVICE_TAG;
    d->common.version = 0x303;      /* CAMERA_DEVICE_API_VERSION_3_3 */
    d->common.module = (struct hw_module *)m;
    d->common.close = dev_close;
    d->ops = &ops;
    d->id = n;
    d->still_size = still_size_for(n);
    d->video_enabled = rear_video_enabled(n);
    d->fd = host_connect();
    if (d->fd < 0) { free(d); return -19; /* -ENODEV: the host does not serve a camera */ }
    say(d, n ? "open 1\n" : "open 0\n");
    d->running = 1;
    if (pthread_create(&d->reader, 0, reader, d) != 0) { close(d->fd); free(d); return -12; }
    if (pthread_create(&d->worker, 0, worker, d) != 0) {
        d->running = 0; shutdown(d->fd, 2); pthread_join(d->reader, 0); close(d->fd); free(d); return -12;
    }
    *out = &d->common;
    return 0;
}

static int cam_count(void) { return 2; }
static int cam_info(int id, struct camera_info *info) {
    if (id < 0 || id > 1) return -22;
    info->facing = id;                      /* 0 back, 1 front */
    info->orientation = id ? 270 : 90;
    info->device_version = 0x303;
    info->static_characteristics = static_info(id);
    if (!info->static_characteristics) return -19;
    info->resource_cost = 50;               /* see camera_hal.c */
    info->conflicting_devices = 0;
    info->conflicting_devices_length = 0;
    return 0;
}
static int mod_set_callbacks(const void *cb) { (void)cb; return 0; }
static int mod_set_torch(const char *id, int on) { (void)id; (void)on; return -38; }   /* ENOSYS: no flash unit */

static struct hw_module_methods methods = { cam_open };

#ifdef CAM_VENDOR_TAGS
/* A vendor camera app sets its vendor's request keys without asking whether they exist, and a key the HAL does not
 * name is an exception there: the tags are declared (kernel's own table), nothing is done with them */
#include CAM_VENDOR_TAGS
#define NVTAGS ((int)(sizeof g_vtags / sizeof g_vtags[0]))
#define VTAG0 0x80000000u
struct vendor_tag_ops {
    int (*get_tag_count)(const struct vendor_tag_ops *o);
    void (*get_all_tags)(const struct vendor_tag_ops *o, uint32_t *tags);
    const char *(*get_section_name)(const struct vendor_tag_ops *o, uint32_t tag);
    const char *(*get_tag_name)(const struct vendor_tag_ops *o, uint32_t tag);
    int (*get_tag_type)(const struct vendor_tag_ops *o, uint32_t tag);
    void *reserved[8];
};
static int same(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
/* Android resolves the longest section prefix first. This unused Xiaomi key
 * has the same full name as another section and cannot be resolved by name. */
static int vt_visible(uint32_t tag) {
    uint32_t i = tag - VTAG0;
    return i < (uint32_t)NVTAGS &&
        !(same(g_vtags[i].section, "xiaomi.hdr") && same(g_vtags[i].name, "hdrChecker"));
}
static int vt_count(const struct vendor_tag_ops *o) {
    (void)o; int n = 0;
    for (int i = 0; i < NVTAGS; i++) if (vt_visible(VTAG0 + (uint32_t)i)) n++;
    return n;
}
static void vt_all(const struct vendor_tag_ops *o, uint32_t *tags) {
    (void)o; int n = 0;
    for (int i = 0; i < NVTAGS; i++) if (vt_visible(VTAG0 + (uint32_t)i)) tags[n++] = VTAG0 + (uint32_t)i;
}
static const char *vt_section(const struct vendor_tag_ops *o, uint32_t tag) { (void)o; return vt_visible(tag) ? g_vtags[tag - VTAG0].section : 0; }
static const char *vt_name(const struct vendor_tag_ops *o, uint32_t tag) { (void)o; return vt_visible(tag) ? g_vtags[tag - VTAG0].name : 0; }
static int vt_type(const struct vendor_tag_ops *o, uint32_t tag) { (void)o; return vt_visible(tag) ? g_vtags[tag - VTAG0].type : -1; }
static uint32_t vendor_tag(const char *section, const char *name) {
    for (int i = 0; i < NVTAGS; i++) if (vt_visible(VTAG0 + (uint32_t)i) && same(g_vtags[i].section, section) && same(g_vtags[i].name, name)) return VTAG0 + (uint32_t)i;
    return 0;
}
static void mod_vendor_tag_ops(struct vendor_tag_ops *o) {
    o->get_tag_count = vt_count; o->get_all_tags = vt_all; o->get_section_name = vt_section;
    o->get_tag_name = vt_name; o->get_tag_type = vt_type;
}
#else
static uint32_t vendor_tag(const char *section, const char *name) { (void)section; (void)name; return 0; }
#endif

EXPORT struct camera_module HMI = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = 0x0204,   /* CAMERA_MODULE_API_VERSION_2_4 */
        .hal_api_version = 0,
        .id = "camera",
        .name = "AEmulator camera (HAL 3)",
        .author = "AEmulator",
        .methods = &methods,
    },
    .get_number_of_cameras = cam_count,
    .get_camera_info = cam_info,
    .set_callbacks = (void *)mod_set_callbacks,
    .set_torch_mode = (void *)mod_set_torch,
#ifdef CAM_VENDOR_TAGS
    .get_vendor_tag_ops = (void *)mod_vendor_tag_ops,
#endif
};
