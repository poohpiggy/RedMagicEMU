# Experimental AOD saliency adapter

This directory is an offline experiment. It is not wired into Guest startup and
does not supply a model, ONNX Runtime, or a modified firmware APK. The adapter
must not be enabled solely on the strength of these tests.

`aod_jni.c` exports the six methods found in the target firmware's
`com.zte.saliency.SaliencyApi`. The ORT backend requires an explicit
`RME_AOD_MODEL_PATH` build definition; without one initialization fails. The
integration must authenticate the model and runtime assets, choose a private
rebuildable installation location, and preserve dependency license notices.
Errors log only bounded categories, never image contents or model paths.

The model adapter produces 512×512 logits. The postprocessor follows the
statically inspected target `saliencyProcess`, `remainMaxAreaRegion`,
`fillLargestComponentHole` and `getHighestPoint` sequence: connected-component
selection, threshold and hole handling, linear resize, elliptical erosion,
Gaussian blur and the first nonzero pixel on the highest row. The output array
is B,G,R,A as consumed by the target's `saveRgbaAsPng`. The resize rounding is
derived from OpenCV; see `OPENCV_RESIZE_LICENSE.txt`.

The adapter supports at most 4,194,304 model-mask pixels and 16,777,216 output
pixels. These are implementation limits, not firmware claims. Failed or empty
inference is reported as empty, and a validated nonoverlapping output buffer is
cleared. The backend is serialized and result state is per thread; init/release
invalidate prior results. The observed SystemUI call sites use BackgroundThread,
but some run and highest-point operations are separate queued tasks. The OEM
interface has no request ID, so this adapter cannot associate two interleaved
requests on the same thread. Original firmware behavior and real SystemUI
effects under this race have not been established.

Tests in `tests/` use a fake backend for error, state and concurrency cases.
Run them on a POSIX host with `sh tests/run_host_tests.sh /path/to/ndk/jni.h`,
or set `ANDROID_NDK_HOME` and omit the argument. `CC` and `CFLAGS` can select
the compiler and sanitizers. These tests do not run the actual model.
The mask preparation and finishing stages have additionally been compared on
synthetic masks against desktop OpenCV 5, not the target's OpenCV build. The
actual model, Android guest path, zygote lifecycle, target namespace, visual
quality, memory limit and other guest applications require separate validation.
