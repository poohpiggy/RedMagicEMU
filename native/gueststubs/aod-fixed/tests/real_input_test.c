/* Optional offline test: image and alpha data are supplied outside the repo. */
#include <dlfcn.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define P "Java_com_zte_saliency_SaliencyApi_"
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "CHECK_FAILED line %d: %s\n", __LINE__, #x);             \
      return 4;                                                                \
    }                                                                          \
  } while (0)
#define N ((size_t)1216 * 2688)
struct arr {
  int n;
  unsigned char *b;
  jint xy[2];
};
static int exception;
static jboolean pending(JNIEnv *e) {
  (void)e;
  return exception ? JNI_TRUE : JNI_FALSE;
}
static jsize len(JNIEnv *e, jarray a) {
  (void)e;
  return ((struct arr *)a)->n;
}
static void getb(JNIEnv *e, jbyteArray a, jsize off, jsize n, jbyte *b) {
  (void)e;
  memcpy(b, ((struct arr *)a)->b + off, n);
}
static void setb(JNIEnv *e, jbyteArray a, jsize off, jsize n, const jbyte *b) {
  (void)e;
  memcpy(((struct arr *)a)->b + off, b, n);
}
static void seti(JNIEnv *e, jintArray a, jsize off, jsize n, const jint *b) {
  (void)e;
  memcpy(((struct arr *)a)->xy + off, b, n * sizeof(jint));
}
static jboolean same(JNIEnv *e, jobject a, jobject b) {
  (void)e;
  return a == b;
}
static const struct JNINativeInterface vt = {.ExceptionCheck = pending,
                                             .GetArrayLength = len,
                                             .GetByteArrayRegion = getb,
                                             .SetByteArrayRegion = setb,
                                             .SetIntArrayRegion = seti,
                                             .IsSameObject = same};
int main(int argc, char **argv) {
  CHECK(argc == 5);
  int invalid = !strcmp(argv[3], "invalid");
  JNIEnv env = &vt;
  JNIEnv *e = &env;
  jclass c = (jclass)1;
  void *h = dlopen(argv[4], RTLD_NOW | RTLD_LOCAL);
  CHECK(h);
  void (*init)(JNIEnv *, jclass, jstring, jstring, jstring) =
      dlsym(h, P "saliencyMapInit");
  jint (*status)(JNIEnv *, jclass) = dlsym(h, P "saliencyInitStatus");
  void (*run)(JNIEnv *, jclass, jbyteArray, jint, jint, jbyteArray) =
      dlsym(h, P "saliencyMapRunning");
  jboolean (*alpha)(JNIEnv *, jclass) = dlsym(h, P "saliencyIsAlphaNull");
  void (*highest)(JNIEnv *, jclass, jintArray) =
      dlsym(h, P "saliencyMapHighestPointGet");
  void (*release)(JNIEnv *, jclass) = dlsym(h, P "saliencyMapRelease");
  CHECK(init && status && run && alpha && highest && release);
  struct arr in = {.n = N * 3, .b = malloc(N * 3)},
             out = {.n = N * 4, .b = malloc(N * 4)},
             xy = {.n = 2, .xy = {777, 777}};
  unsigned char *mask = malloc(N);
  CHECK(in.b && out.b && mask);
  FILE *f = fopen(argv[1], "rb");
  CHECK(f && fread(in.b, 1, N * 3, f) == N * 3 && fgetc(f) == EOF);
  fclose(f);
  f = fopen(argv[2], "rb");
  CHECK(f && fread(mask, 1, N, f) == N && fgetc(f) == EOF);
  fclose(f);
  memset(out.b, 9, N * 4);
  CHECK(status(e, c) == 0);
  init(e, c, (jstring)1, (jstring)1, (jstring)1);
  if (invalid) {
    CHECK(status(e, c) == -1);
    run(e, c, (jbyteArray)&in, 1216, 2688, (jbyteArray)&out);
    CHECK(alpha(e, c) == JNI_TRUE && out.b[0] == 9);
    puts("REAL_MASK_INVALID_REJECTED");
    return 0;
  }
  CHECK(status(e, c) == 1);
  run(e, c, (jbyteArray)&in, 1216, 2688, (jbyteArray)&out);
  CHECK(alpha(e, c) == JNI_FALSE);
  for (size_t i = 0; i < N; i++)
    CHECK(out.b[i * 4] == in.b[i * 3 + 2] &&
          out.b[i * 4 + 1] == in.b[i * 3 + 1] &&
          out.b[i * 4 + 2] == in.b[i * 3] && out.b[i * 4 + 3] == mask[i]);
  highest(e, c, (jintArray)&xy);
  CHECK(xy.xy[0] == 880 && xy.xy[1] == 166);
  in.b[0] ^= 1;
  out.b[0] = 9;
  run(e, c, (jbyteArray)&in, 1216, 2688, (jbyteArray)&out);
  CHECK(alpha(e, c) == JNI_TRUE && out.b[0] == 9);
  xy.xy[0] = 777;
  highest(e, c, (jintArray)&xy);
  CHECK(xy.xy[0] == 880 && xy.xy[1] == 166);
  release(e, c);
  CHECK(status(e, c) == 0);
  in.b[0] ^= 1;
  init(e, c, (jstring)1, (jstring)1, (jstring)1);
  CHECK(status(e, c) == 1);
  run(e, c, (jbyteArray)&in, 1216, 2688, (jbyteArray)&out);
  CHECK(alpha(e, c) == JNI_FALSE);
  release(e, c);
  CHECK(status(e, c) == 0);
  puts("REAL_DIGEST_ALL_PIXELS_OK");
  return 0;
}
