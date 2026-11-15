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
static int fake_metadata;
static void *fake_md_alloc(size_t entries, size_t bytes) { (void)entries; (void)bytes; return &fake_metadata; }
static int fake_md_add(void *m, uint32_t tag, const void *p, size_t n) { (void)m; (void)tag; (void)p; (void)n; return 0; }
static void fake_md_free(void *m) { (void)m; }

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

int main(void) {
    md_alloc = fake_md_alloc; md_add = fake_md_add; md_free = fake_md_free;
    if (run_case(0, 0, 0, 0) || run_case(1, 0, 0, 0) || run_case(1, 0, 1, 0) ||
        run_case(1, 1, 0, 0) || run_case(1, 1, 1, 1) || run_case(1, 1, 1, 0) ||
        queue_lifetime_case()) return 1;
    puts("no stale or synthetic preview frames: OK");
    return 0;
}
