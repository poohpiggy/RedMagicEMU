/* Exercise answer() with real callbacks and controlled host-frame arrival. */
#include <stdio.h>
#include <fcntl.h>
#include "../camera_hal3.c"

extern int pipe(int fds[2]);
extern int fcntl(int fd, int cmd, ...);

static unsigned char preview_output[W * H * 4];
static unsigned char jpeg_output[JPEG_MAX];
static int preview_handle[8], jpeg_handle[8];
static int notified_error, notified_shutter, results, bad_result, lock_calls;
static int fake_metadata, fake_result_metadata;
static int32_t requested_crop[4];
static int32_t reported_crop[4];
static void *fake_md_alloc(size_t entries, size_t bytes) { (void)entries; (void)bytes; return &fake_result_metadata; }
static int fake_md_add(void *m, uint32_t tag, const void *p, size_t n) { (void)m; (void)tag; (void)p; (void)n; return 0; }
static void fake_md_free(void *m) { (void)m; }
static size_t fake_md_count(const void *m) { (void)m; return 0; }
static int fake_md_append(void *dst, const void *src) {
    (void)dst;
    if (src == &fake_metadata) copy(reported_crop, requested_crop, sizeof(reported_crop));
    return 0;
}
static int fake_md_find(const void *m, uint32_t tag, struct ro_entry *entry) {
    if (tag != SCALER_CROP_REGION || (m != &fake_metadata && m != &fake_result_metadata)) return -1;
    *entry = (struct ro_entry){ .count = 4, .data = m == &fake_metadata ? requested_crop : reported_crop };
    return 0;
}
static int fake_md_update(void *m, size_t index, const void *data, size_t count, void *entry) {
    (void)index; (void)entry;
    if (m != &fake_result_metadata || count != 4) return -1;
    copy(reported_crop, data, sizeof(reported_crop));
    return 0;
}

static int fake_lock(const struct gralloc_module *g, const void *handle, int usage,
                     int l, int t, int w, int h, void **out) {
    (void)g; (void)usage; (void)l; (void)t; (void)w; (void)h;
    lock_calls++;
    *out = handle == jpeg_handle ? (void *)jpeg_output : (void *)preview_output;
    return 0;
}
static int fake_unlock(const struct gralloc_module *g, const void *h) { (void)g; (void)h; return 0; }
static const struct gralloc_module fake_gralloc = { .lock = fake_lock, .unlock = fake_unlock };

static void notify(const struct callback_ops *cb, const struct notify_msg *n) {
    (void)cb;
    if (n->type == 1 && n->m.error.code == 2 && n->m.error.frame == 73) notified_error++;
    else if (n->type == 2) notified_shutter++;
}
static void result(const struct callback_ops *cb, const struct capture_result *r) {
    (void)cb; results++;
    if (r->frame != 73) bad_result++;
    if (notified_error) {
        if (r->result != 0 || r->partial != 0) bad_result++;
        for (unsigned i = 0; i < r->num_out; i++) {
            if (r->out[i].status != 1 || r->out[i].acquire_fence != -1 || r->out[i].release_fence < 0)
                bad_result++;
        }
    } else {
        for (unsigned i = 0; i < r->num_out; i++) if (r->out[i].status != 0) bad_result++;
    }
}
static const struct callback_ops callbacks = { .result = result, .notify = notify };

struct arrival { struct camera3 *d; int preview, jpeg, preview_before_jpeg; };
static void *deliver(void *arg) {
    struct arrival *a = arg;
    usleep(20000);
    if (a->preview && a->preview_before_jpeg) {
        pthread_mutex_lock(a->d->lock);
        a->d->frame[0] = 42;
        a->d->frame[(120 * W + 160) * 4] = 99;
        a->d->frame[W * H * 4 + 120 * W + 160] = 88;
        a->d->frame[W * H * 5 + 60 * W + 160] = 77;
        a->d->frame[W * H * 5 + 60 * W + 161] = 66;
        a->d->frame_seq++;
        pthread_mutex_unlock(a->d->lock);
        usleep(30000);
    }
    if (a->jpeg) {
        pthread_mutex_lock(a->d->lock);
        a->d->jpeg = malloc(3);
        a->d->jpeg[0] = 0xff; a->d->jpeg[1] = 0xd8; a->d->jpeg[2] = 0xd9;
        a->d->jpeg_len = 3;
        a->d->jpeg_seq++;
        pthread_mutex_unlock(a->d->lock);
    }
    if (a->preview && !a->preview_before_jpeg) {
        usleep(30000);
        pthread_mutex_lock(a->d->lock);
        a->d->frame[0] = 42;
        a->d->frame_seq++;
        pthread_mutex_unlock(a->d->lock);
    }
    return 0;
}

static int run_case(int old, int blob, int fresh_preview, int preview_before_jpeg) {
    struct camera3 d = {0};
    struct stream prev = { .width = W, .height = H, .format = FORMAT_RGBA_8888 };
    struct stream jpg = { .width = W, .height = H, .format = FORMAT_BLOB };
    const void *prev_h = preview_handle, *jpg_h = jpeg_handle;
    struct request req = { .frame = 73, .nbuf = blob ? 2 : 1 };
    int p[2], q[2], failed = 0;
    if (pipe(p) || (blob && pipe(q))) return 1;
    req.bufs[0] = (struct stream_buffer){ .stream = &prev, .buffer = &prev_h, .acquire_fence = p[0] };
    if (blob) req.bufs[1] = (struct stream_buffer){ .stream = &jpg, .buffer = &jpg_h, .acquire_fence = q[0] };
    d.running = 1; d.fd = -1; d.cb = &callbacks;
    if (old || blob) {
        d.frame = calloc(1, W * H * 4 + W * H * 3 / 2);
        d.frame[0] = 77; d.frame_seq = 1;
    }
    jpeg_handle[6] = JPEG_MAX;
    notified_error = notified_shutter = results = bad_result = lock_calls = 0;
    g_gralloc = &fake_gralloc;
    int success_expected = fresh_preview && !preview_before_jpeg;
    struct arrival a = { &d, fresh_preview, blob, preview_before_jpeg };
    long sender = 0;
    if ((blob || fresh_preview) && pthread_create(&sender, 0, deliver, &a)) return 1;
    /* A successful request needs no acquire fences for this particular check. */
    if (success_expected) {
        close(req.bufs[0].acquire_fence); req.bufs[0].acquire_fence = -1;
        if (blob) { close(req.bufs[1].acquire_fence); req.bufs[1].acquire_fence = -1; }
    }
    answer(&d, &req);
    if (blob || fresh_preview) pthread_join(sender, 0);
    if (success_expected) {
        if (notified_error || notified_shutter != 1 || results != 1 || bad_result ||
            lock_calls != (blob ? 2 : 1) || preview_output[0] != 42 || (blob && jpeg_output[0] != 0xff)) failed = 1;
    } else {
        if (notified_error != 1 || notified_shutter || results != 1 || bad_result || lock_calls ||
            req.bufs[0].release_fence != p[0] || fcntl(p[0], F_GETFD) < 0 ||
            (blob && (req.bufs[1].release_fence != q[0] || fcntl(q[0], F_GETFD) < 0))) failed = 1;
    }
    close(p[0]); close(p[1]); if (blob) { close(q[0]); close(q[1]); }
    free(d.frame); free(d.jpeg);
    if (failed) fprintf(stderr, "case old=%d blob=%d fresh=%d before=%d failed (error=%d shutter=%d results=%d bad=%d locks=%d)\n",
                        old, blob, fresh_preview, preview_before_jpeg, notified_error, notified_shutter, results, bad_result, lock_calls);
    return failed;
}

static int queue_lifetime_case(void) {
    struct camera3 d = {0};
    struct stream prev = { .width = W, .height = H, .format = FORMAT_RGBA_8888 };
    const void *handle = preview_handle;
    struct stream_buffer out = { .stream = &prev, .buffer = &handle, .acquire_fence = -1 };
    struct capture_request req = { .frame = 74, .num_out = 1, .out = &out };
    d.last = &fake_metadata;
    if (d_request(&d, &req)) return 1;
    handle = jpeg_handle; /* framework's transient request memory is reused */
    struct request *r = &d.queue[0];
    if (!r->bufs[0].buffer || *r->bufs[0].buffer != preview_handle) {
        fprintf(stderr, "queued request retained transient buffer pointer\n");
        return 1;
    }
    return 0;
}

static int zoom_case(int format, int zoomed) {
    struct camera3 d = { .running = 1, .fd = -1, .cb = &callbacks };
    struct stream preview = { .width = W, .height = H, .format = format };
    const void *handle = preview_handle;
    struct request req = { .frame = 73, .nbuf = 1, .settings = &fake_metadata };
    req.bufs[0] = (struct stream_buffer){ .stream = &preview, .buffer = &handle, .acquire_fence = -1 };
    requested_crop[0] = 0;
    requested_crop[1] = 0;
    requested_crop[2] = zoomed ? 320 : 640;
    requested_crop[3] = zoomed ? 240 : 480;
    d.frame = calloc(1, W * H * 4 + W * H * 3 / 2);
    if (!d.frame) return 1;
    struct arrival a = { &d, 1, 0, 1 };
    long sender;
    notified_error = notified_shutter = results = bad_result = lock_calls = 0;
    if (pthread_create(&sender, 0, deliver, &a)) { free(d.frame); return 1; }
    answer(&d, &req);
    pthread_join(sender, 0);
    free(d.frame);
    int expected = format == FORMAT_RGBA_8888 ? (zoomed ? 99 : 42) : (zoomed ? 88 : 0);
    if (notified_error || notified_shutter != 1 || results != 1 || bad_result ||
        preview_output[0] != expected || (format == FORMAT_YCBCR_420_888 && zoomed &&
        (preview_output[W * H] != 77 || preview_output[W * H + 1] != 66))) {
        fprintf(stderr, "crop format=%d zoomed=%d: first pixel %d, expected %d\n",
                format, zoomed, preview_output[0], expected);
        return 1;
    }
    if (reported_crop[0] != (zoomed ? 160 : 0) || reported_crop[1] != (zoomed ? 120 : 0)) {
        fprintf(stderr, "reported crop does not match the centered output\n");
        return 1;
    }
    return 0;
}

static int video_frame_freshness(void) {
    struct camera3 camera = {
        .video_enabled = 1, .pull_video = 1, .fd = -1,
        .video_frames = {{0, 9000000}},
    };
    if (has_new_frame(&camera, 0, 0, 1, 0)) return 1;
    camera.video_tail = 1;
    if (!has_new_frame(&camera, 0, 0, 1, 0)) return 1;
    retire_video_frame(&camera);
    if (has_new_frame(&camera, 0, 0, 1, 0)) return 1;
    camera.pull_video = 0;
    return has_new_frame(&camera, 0, 0, 1, 0);
}

static int video_descriptor_header(void) {
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return 1;
    int32_t header[4] = {6, VIDEO_W, VIDEO_H, 0};
    int32_t received[4] = {0};
    unsigned char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec data = {header, sizeof(header)};
    struct msghdr message = {.msg_iov = &data, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr *item = CMSG_FIRSTHDR(&message);
    item->cmsg_level = SOL_SOCKET;
    item->cmsg_type = SCM_RIGHTS;
    item->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(item), &sockets[0], sizeof(int));
    int shared = -1;
    int bad = sendmsg(sockets[0], &message, 0) != (long)sizeof(header) ||
              frame_header(sockets[1], received, &shared) || shared < 0 ||
              memcmp(header, received, sizeof(header)) || fcntl(shared, F_GETFD) != FD_CLOEXEC;
    if (shared >= 0) close(shared);
    close(sockets[0]);
    close(sockets[1]);
    return bad;
}

static int video_encoder_uses_planar_yuv(void) {
    struct camera3 camera = {.video_enabled = 1, .fd = -1};
    struct stream preview = {.width = 1920, .height = 1080, .format = FORMAT_IMPLEMENTATION_DEFINED, .usage = 0x100};
    struct stream recorder = {.width = 1920, .height = 1080, .format = FORMAT_IMPLEMENTATION_DEFINED, .usage = 0x10003};
    struct stream *streams[] = {&preview, &recorder};
    struct stream_config config = {.num = 2, .streams = streams, .mode = 0x800c};
    if (d_configure(&camera, &config) || preview.format != FORMAT_RGBA_8888 ||
        recorder.format != 0x32315659 || !camera.video_stream) {
        fprintf(stderr, "rear 1080p encoder must receive YV12, with RGBA preview\n");
        return 1;
    }
    if (recorder.data_space != 0) {
        fprintf(stderr, "reused encoder stream must retain its requested dataspace\n");
        return 1;
    }
    camera.video_enabled = 0;
    recorder.format = FORMAT_IMPLEMENTATION_DEFINED;
    if (d_configure(&camera, &config) || recorder.format != FORMAT_RGBA_8888 || camera.video_stream) {
        fprintf(stderr, "baseline encoder stream format must stay unchanged\n");
        return 1;
    }
    return 0;
}

static int video_planar_crop(void) {
    const unsigned char yv12[24] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        40, 41, 42, 43, 60, 61, 62, 63,
    };
    const unsigned char nv21[24] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        40, 60, 41, 61, 42, 62, 43, 63,
    };
    unsigned char out[64];
    for (int interleaved = 0; interleaved <= 1; interleaved++) {
        memset(out, 255, sizeof(out));
        put_yv12(out, 2, 2, interleaved ? nv21 : yv12, 4, 4, (struct crop){2, 2, 2, 2}, interleaved);
        if (out[0] != 10 || out[1] != 11 || out[16] != 14 || out[17] != 15 ||
            out[32] != 43 || out[48] != 63 || out[2] != 255 || out[33] != 255 || out[49] != 255) {
            fprintf(stderr, "incorrect crop or chroma order in YV12 encoder buffer\n");
            return 1;
        }
    }
    return 0;
}

int main(void) {
    md_alloc = fake_md_alloc; md_add = fake_md_add; md_free = fake_md_free;
    md_entries = fake_md_count; md_data = fake_md_count; md_append = fake_md_append;
    md_find = fake_md_find;
    md_update = fake_md_update;
    g_gralloc = &fake_gralloc;
    if (zoom_case(FORMAT_RGBA_8888, 0) || zoom_case(FORMAT_RGBA_8888, 1) ||
        zoom_case(FORMAT_YCBCR_420_888, 0) || zoom_case(FORMAT_YCBCR_420_888, 1)) return 1;
    if (run_case(0, 0, 0, 0) || run_case(1, 0, 0, 0) || run_case(1, 0, 1, 0) ||
        run_case(1, 1, 0, 0) || run_case(1, 1, 1, 1) || run_case(1, 1, 1, 0) ||
        queue_lifetime_case() || video_frame_freshness() || video_descriptor_header() ||
        video_encoder_uses_planar_yuv() || video_planar_crop()) return 1;
    puts("no stale or synthetic preview frames: OK");
    return 0;
}
