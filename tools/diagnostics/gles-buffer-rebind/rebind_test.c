/* Exercise the production buffer-binding encoder without a device or socket. */
#define main map_regression_main
#include "../gles-bridge-local-test/map_test.c"
#undef main

static int sent_binding(size_t before, GLenum target, GLuint name) {
    if (!check(stream.n == before + 16, "bind command was not sent")) return 0;
    return check(word(stream.buf + before) == IDX_glBindBuffer &&
                 word(stream.buf + before + 4) == 8 &&
                 word(stream.buf + before + 8) == target &&
                 word(stream.buf + before + 12) == name,
                 "bind command carries the requested target and name");
}

static int same_name_rebind(GLenum target, int vao) {
    struct ectx *a = context(NULL), *b = context(a);
    if (!a || !b) return 0;
    make_current_for_encoder(a);
    if (vao) e_glBindVertexArray(7);
    e_glBindBuffer(target, 11);

    /* The real GL counterpart deletes 11 in B, then creates another 11.
     * A's old object remains bound even though both objects have name 11. */
    make_current_for_encoder(b);
    GLuint name = 11;
    e_glDeleteBuffers(1, &name);
    e_glBindBuffer(target, name);

    make_current_for_encoder(a);
    size_t before = stream.n;
    e_glBindBuffer(target, name);
    return sent_binding(before, target, name);
}

static void attrib(int integer, GLuint index) {
    if (integer) e_glVertexAttribIPointer(index, 2, 0x1404, 8, (void *)(uintptr_t)4);
    else e_glVertexAttribPointer(index, 2, 0x1406, 0, 8, (void *)(uintptr_t)4);
}

static int sent_attrib(size_t before, int integer, GLuint index) {
    uint32_t payload = integer ? 24 : 28;
    return check(stream.n == before + 8 + payload &&
                 word(stream.buf + before) == (integer ? IDX_glVertexAttribIPointer : IDX_glVertexAttribPointer) &&
                 word(stream.buf + before + 4) == payload &&
                 word(stream.buf + before + 8) == index,
                 "attribute pointer command was not sent");
}

static int attrib_rebind(int integer) {
    struct ectx *a = context(NULL), *b = context(a);
    if (!a || !b) return 0;
    const GLuint indices[] = {0, 7, 15, 16};
    make_current_for_encoder(a);
    e_glBindBuffer(ARRAY_BUFFER, 11);
    for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        size_t before = stream.n;
        attrib(integer, indices[i]);
        if (!sent_attrib(before, integer, indices[i])) return 0;
    }
    size_t before = stream.n;
    attrib(integer, 7);
    if (!check(stream.n == before, "unchanged attribute should retain caching without a bind")) return 0;
    e_glBindBuffer(0x8893, 11);
    before = stream.n;
    attrib(integer, 7);
    if (!check(stream.n == before, "element buffer bind must not invalidate attribute cache")) return 0;

    make_current_for_encoder(b);
    GLuint name = 11;
    e_glDeleteBuffers(1, &name);
    e_glBindBuffer(ARRAY_BUFFER, name);
    make_current_for_encoder(a);
    e_glBindBuffer(ARRAY_BUFFER, name);
    for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        before = stream.n;
        attrib(integer, indices[i]);
        if (!sent_attrib(before, integer, indices[i])) return 0;
    }
    before = stream.n;
    attrib(integer, 7);
    return check(stream.n == before, "attribute caching should resume after reissue");
}

int main(int argc, char **argv) {
    if (argc != 2 || !setup()) return 2;
    if (!strcmp(argv[1], "attrib-float") || !strcmp(argv[1], "attrib-int")) {
        int ok = attrib_rebind(!strcmp(argv[1], "attrib-int"));
        teardown();
        if (ok) puts("ATTRIB_FORWARD_OK");
        return ok ? 0 : 1;
    }
    GLenum target = !strcmp(argv[1], "array") ? 0x8892 : 0x8893;
    int vao = !strcmp(argv[1], "element-vao");
    if (strcmp(argv[1], "array") && strcmp(argv[1], "element") && !vao) return 2;
    int ok = same_name_rebind(target, vao);
    teardown();
    if (ok) puts("BIND_FORWARD_OK");
    return ok ? 0 : 1;
}
