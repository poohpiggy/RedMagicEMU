/* Test the full production GLES encoder with the existing in-memory host. */
#define main upstream_map_test_main
#include "../gles-bridge-local-test/map_test.c"
#undef main

static int shared_pointer_lifetime(void) {
    struct ectx *a = context(NULL), *b = context(a);
    if (!a || !b) return 0;
    make_current_for_encoder(a);
    e_glBindBuffer(COPY_WRITE, 3);
    unsigned char *p = map(COPY_WRITE, 0, 8);
    if (!check(p != NULL, "shared lifetime map")) return 0;
    make_current_for_encoder(b);
    e_glBindBuffer(COPY_WRITE, 3);
    if (!check(destroy_context(a), "destroy original shared context")) return 0;
    /* The other shared context still retains the object. */
    p[0] = 0x73;
    return check(p[0] == 0x73, "mapped storage survives original context");
}

static int deleted_name_lifetime(void) {
    struct ectx *a = context(NULL), *b = context(a);
    if (!a || !b) return 0;
    make_current_for_encoder(a);
    e_glBindBuffer(COPY_WRITE, 3);
    unsigned char *p = map(COPY_WRITE, 0, 8);
    if (!check(p != NULL, "map old object")) return 0;
    GLuint name = 3;
    make_current_for_encoder(b);
    e_glDeleteBuffers(1, &name);
    e_glBindBuffer(COPY_WRITE, name); /* Simulates the driver reusing the name. */
    make_current_for_encoder(a);
    e_glDeleteBuffers(1, &name); /* Deletes the new object, not A's old binding. */
    p[0] = 0x54;
    if (!check(p[0] == 0x54, "deleting a reused name must not free old storage")) return 0;
    size_t before = stream.n;
    e_glFlushMappedBufferRange(COPY_WRITE, 0, 1);
    if (!check(stream.n == before, "ambiguous deleted name must not write")) return 0;
    return check(map(COPY_WRITE, 0, 8) == NULL, "ambiguous deleted name must not remap");
}

static int ranges(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    e_glBindBuffer(COPY_WRITE, 3);
    if (!check(map(COPY_WRITE, -1, 8) == NULL, "negative map offset")) return 0;
    if (!check(map(COPY_WRITE, LONG_MAX - 2, 8) == NULL, "overflowing map offset")) return 0;
    if (!check(map(COPY_WRITE, 0, (64L << 20) + 1) == NULL, "map length limit")) return 0;
    if (!check(map(COPY_WRITE, 16, 8) != NULL, "valid map after invalid attempts")) return 0;
    size_t before = stream.n;
    e_glFlushMappedBufferRange(COPY_WRITE, -1, 2);
    e_glFlushMappedBufferRange(COPY_WRITE, LONG_MAX, 2);
    e_glFlushMappedBufferRange(COPY_WRITE, 7, 2);
    e_glFlushMappedBufferRange(COPY_WRITE, 0, -1);
    return check(stream.n == before, "invalid flush ranges must not write");
}

static int unwritten_bytes(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    e_glBindBuffer(COPY_WRITE, 3);
    if (!check(map(COPY_WRITE, 16, 8) != NULL, "map for unwritten flush")) return 0;
    size_t before = stream.n;
    e_glFlushMappedBufferRange(COPY_WRITE, 1, 4);
    if (!check(stream.n == before + 28 && word(stream.buf + before) == OP_MAP_WRITE,
               "unwritten flush command")) return 0;
    return check(word(stream.buf + before + 24) == 0, "unwritten bytes must be initialized");
}

static int delete_before_map(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    GLuint name = 3;
    e_glBindBuffer(COPY_WRITE, name);
    e_glDeleteBuffers(1, &name);
    if (!check(map(COPY_WRITE, 0, 8) == NULL, "deleted current binding is unknown")) return 0;
    e_glBindBuffer(COPY_WRITE, 4);
    return check(map(COPY_WRITE, 0, 8) != NULL, "rebind allows subsequent map");
}

static int check_writebacks(int mixed) {
    unsigned char objects[8][32] = {{0}};
    unsigned bound = 0;
    int writes = 0;
    for (size_t at = 0; at < stream.n;) {
        if (stream.n - at < 8) return check(0, "truncated writeback header");
        uint32_t op = word(stream.buf + at), n = word(stream.buf + at + 4);
        at += 8;
        if (n > stream.n - at) return check(0, "truncated writeback payload");
        const uint8_t *p = stream.buf + at;
        if (op == IDX_glBindBuffer && n == 8 && word(p) == COPY_WRITE) bound = word(p + 4);
        else if (op == OP_MAP_WRITE) {
            if (n < 16 || word(p) != COPY_WRITE || bound >= 8) return check(0, "invalid writeback target");
            uint64_t offset = qword(p + 4); uint32_t count = word(p + 12);
            if (offset > 32 || count > 32 - offset || n != 16 + ((count + 3u) & ~3u))
                return check(0, "invalid writeback range");
            memcpy(objects[bound] + offset, p + 16, count);
            ++writes;
        }
        at += n;
    }
    if (!check(writes == 2, "exactly two correctly scoped writes")) return 0;
    for (size_t i = 0; i < 32; ++i) {
        unsigned char want3 = mixed ? (i >= 17 && i < 21 ? 0xa1 : 0) : (i >= 16 && i < 24 ? 0xa1 : 0);
        unsigned char want4 = i >= 4 && i < 12 ? 0xb2 : 0;
        if (objects[3][i] != want3 || objects[4][i] != want4)
            return check(0, "writeback changed wrong buffer, offset or bytes");
    }
    return 1;
}

static int write_invalidate(int mixed) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    e_glBindBuffer(COPY_WRITE, 3);
    unsigned char *a = e_glMapBufferRange(COPY_WRITE, 16, 8, mixed ? 0x36 : 0x6);
    if (!check(a != NULL, "first write-invalidated map")) return 0;
    memset(a, 0xa1, 8);
    e_glBindBuffer(COPY_WRITE, 4);
    unsigned char *b = e_glMapBufferRange(COPY_WRITE, 4, 8, 0x6);
    if (!check(b != NULL && b != a, "second write-invalidated map")) return 0;
    memset(b, 0xb2, 8);
    size_t before = stream.n;
    e_glFlushMappedBufferRange(COPY_WRITE, 1, 3);
    if (!check(stream.n == before, "non-explicit map must not flush")) return 0;
    if (!check(e_glUnmapBuffer(COPY_WRITE), "unmap second buffer")) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    if (mixed) e_glFlushMappedBufferRange(COPY_WRITE, 1, 4);
    if (!check(e_glUnmapBuffer(COPY_WRITE), "unmap first buffer")) return 0;
    return check_writebacks(mixed);
}

static int selected(int expected) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    e_glBindBuffer(COPY_WRITE, 3); void *a = map(COPY_WRITE, 0, 8);
    e_glBindBuffer(COPY_WRITE, 4); void *b = map(COPY_WRITE, 0, 8);
    return check(a && (!!b == !!expected), "process selection or independent maps");
}

int main(int argc, char **argv) {
    if (argc != 2 || !setup()) return 2;
    int ok = 0;
    if (!strcmp(argv[1], "explicit-flush")) ok = explicit_flush();
    else if (!strcmp(argv[1], "shared-lifetime")) ok = shared_pointer_lifetime();
    else if (!strcmp(argv[1], "delete-reuse-lifetime")) ok = deleted_name_lifetime();
    else if (!strcmp(argv[1], "ranges")) ok = ranges();
    else if (!strcmp(argv[1], "unwritten")) ok = unwritten_bytes();
    else if (!strcmp(argv[1], "delete-before-map")) ok = delete_before_map();
    else if (!strcmp(argv[1], "write-invalidate")) ok = write_invalidate(0);
    else if (!strcmp(argv[1], "mixed-flags")) ok = write_invalidate(1);
    else if (!strcmp(argv[1], "selected")) ok = selected(1);
    else if (!strcmp(argv[1], "unselected")) ok = selected(0);
    else return 2;
    teardown();
    if (ok) puts("UNITY_MAP_EXPERIMENT_CASE_OK");
    return ok ? 0 : 1;
}
