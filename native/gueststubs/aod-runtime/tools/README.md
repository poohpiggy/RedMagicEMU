# Offline AOD resource preparation

`prepare.py` prepares **external, experimental** resources for later local builds. It does not download anything, alter an APK, install files into a Guest, enable the loader, or prove AOD compatibility. The output directory must not exist and must be outside this checkout; no model, runtime binary, firmware, theme image, or user data is added to Git.

Obtain these files separately, then explicitly supply their local paths:

- [BiRefNet lite 512 chunked ONNX](https://huggingface.co/h143N/birefnet-lite-512-chunked/resolve/d0b0c299a1d10b0a57b04d0f1f6088d5c0d6ebf2/onnx/model.onnx), 193,268,476 bytes, SHA-256 `16f6a4f726ff851eb6b50b56daf54c426c836a7372f44df21b2065af2d915d23`.
- [ONNX Runtime Android 1.31.0 AAR](https://repo.maven.apache.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.31.0/onnxruntime-android-1.31.0.aar), 55,138,854 bytes, SHA-256 `72683787a80d0608a3121b967d8e1239aef48286826661c953211626690540a7`.

Example:

```sh
python3 prepare.py --model /local/model.onnx --aar /local/onnxruntime-android-1.31.0.aar --output /outside-checkout/new-aod-resources
```

The script streams both inputs into a private staging directory, checks their fixed lengths and SHA-256 values, extracts only the unique `jni/arm64-v8a/libonnxruntime.so` member, and checks its fixed length and SHA-256. It then creates the new output directory and publishes the verified model, ARM64 runtime, original model and ORT licenses, ORT third-party notices, and `SOURCES.json`. It never replaces an existing output. Staging and output must be on a filesystem that supports hard links. `SOURCES.json` is published last; do not consume a directory without it. Publication is not a crash-durable transaction: an abrupt process or machine termination can leave an incomplete directory without that marker.

On a handled failure or interruption, the script attempts to remove only links it created and its empty output directory. It preserves unexpected contents and reports incomplete cleanup. It does not delete the explicitly supplied inputs. To roll back a successful preparation, after ensuring no consumer uses it, remove only the explicitly selected output directory and files created there; existing app data and firmware are unrelated. No automatic deletion is performed.

The model's [MIT license](licenses/MODEL_LICENSE.txt) is the exact license from the pinned model revision. [ORT's MIT license](licenses/ORT_LICENSE.txt) and [third-party notices](licenses/ORT_THIRD_PARTY_NOTICES.txt) come from ONNX Runtime tag `v1.31.0`. Their SHA-256 values are checked before preparation and recorded in the output manifest. Before redistributing a packaged runtime or model, verify the applicable third-party obligations against the exact artifact and intended distribution.

Run `python3 -m unittest discover -s tests -v` in this directory for synthetic success, integrity failure, duplicate archive member, existing output, interrupted publication, and no-overwrite checks. These tests do not download dependencies or use real firmware.
