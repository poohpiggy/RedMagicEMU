# AOD CPU inference experiment

This directory is an **unwired experiment**, not an AOD fix or a replacement for the original firmware model. It implements packed RGB8 input validation, bilinear antialiased resize to 512×512, RGB ImageNet normalization to float32 NCHW, and an injectable inference callback. On success it returns 512×512 float logits; it does not generate an alpha mask, BGRA foreground, highest point, JNI implementation, callbacks, or a Guest service. Failures leave the caller's output unchanged. The ORT adapter validates model endpoint names, types, dimensions, runtime output, and finite values. An adapter instance must be used serially and may not be destroyed during an inference.

The preprocessing formula and model dimensions follow the published 512×512 model below. Resize follows the pass-order and fixed-point rounding in [Pillow 13.0.0 Resample.c](https://github.com/python-pillow/Pillow/blob/13.0.0/src/libImaging/Resample.c); the attribution and license are preserved in [PILLOW_RESIZE_LICENSE.txt](PILLOW_RESIZE_LICENSE.txt). Reference tests include selected pixels and FNV-1a digests of all resized channel bytes for 11 synthetic sizes; this does not establish equality for every image. Inference output depends on the selected external model and must not be represented as original firmware output.

## Local checks

Run `sh tests/run.sh` from this directory on Linux to exercise validation, error propagation, incomplete callback output, and preprocessing. To also run the ONNX Runtime integration against a 512×512 model and reject a different-shape model:

```sh
sh tests/run.sh --ort /path/to/ort-headers /path/to/libonnxruntime.so /path/to/model-512.onnx /path/to/model-wrong-shape.onnx
```

All four paths are explicit external inputs. The script compiles in a fresh temporary directory and deletes only its own build products. It does not download, copy, or add models, runtime binaries, firmware, or image data to the repository. The integration test loads the external shared library into the test process; use only files whose source and digest have been independently verified.

For a separate end-to-end test linking the experimental mask/postprocess code, provide a raw packed RGB8 input and a **new** output path outside the repository:

```sh
sh tests/run_e2e.sh /path/to/ort-headers /path/to/libonnxruntime.so /path/to/model-512.onnx /path/to/image.rgb WIDTH HEIGHT /path/to/new-output.bgra
```

The output contains image data and must be handled accordingly. This test runs inference twice and writes full-resolution BGRA only after its assertions pass; it exercises the experimental modules together, not the original firmware's QNN implementation.

## External components used for the offline investigation

- [Third-party BiRefNet lite 512 chunked ONNX model](https://huggingface.co/h143N/birefnet-lite-512-chunked/tree/d0b0c299a1d10b0a57b04d0f1f6088d5c0d6ebf2): `onnx/model.onnx`, 193,268,476 bytes, SHA-256 `16f6a4f726ff851eb6b50b56daf54c426c836a7372f44df21b2065af2d915d23`. Its [MIT license](https://huggingface.co/h143N/birefnet-lite-512-chunked/raw/d0b0c299a1d10b0a57b04d0f1f6088d5c0d6ebf2/LICENSE) names ZhengPeng and Studio Ludens V.O.F. This converted model is not verified to be the same model or weights as the target firmware's QNN cache.
- [ONNX Runtime 1.31.0](https://github.com/microsoft/onnxruntime/tree/v1.31.0): headers `include/onnxruntime/core/session/onnxruntime_c_api.h`, `onnxruntime_ep_c_api.h`, and `onnxruntime_error_code.h`. The [project license](https://github.com/microsoft/onnxruntime/blob/v1.31.0/LICENSE) is MIT; review and retain applicable [third-party notices](https://github.com/microsoft/onnxruntime/blob/v1.31.0/ThirdPartyNotices.txt) when redistributing a runtime. The Android AAR used in an independent device probe is `com.microsoft.onnxruntime:onnxruntime-android:1.31.0`, SHA-256 `72683787a80d0608a3121b967d8e1239aef48286826661c953211626690540a7`; its ARM64 `libonnxruntime.so` SHA-256 is `8005a741e57117b4d7b476417706c0e9af13cf1b89db47e60f0cbc56e1af91e0`. Neither the headers nor the AAR are stored here.

The original SystemUI JNI is loaded from its signed APK. This directory does not provide an interception mechanism or prove any Guest execution, visual match, Android resource usage, or failure recovery. The caller remains responsible for model provenance and integrity, memory limits, lifecycle and concurrency, privacy, and safe integration.
