# API 36 视频硬解桥接

Guest Codec2 将 H.264 和 HEVC 压缩帧发送到宿主 MediaCodec，经私有 Unix socket 取回 8 位 I420 帧。宿主通过 Android 36 的公开 Media NDK 枚举硬件解码器。缓冲区按会话重用，首轮覆盖固定分辨率、无 DRM 的视频，每次服务一个解码会话。

常规 APK 使用 `android/` 内的桥接程序和两个 Guest 适配库。可用 `-PrmeVideoBridgeDir=<bundle>` 选择本地构建的组件。正常启动时先确认宿主支持两种硬解，再校验并替换指定 NX809J 固件运行树中的两个解码库。原库在同目录保留为 `libcodec2_soft_{avc,hevc}dec.rme-original.so`，Guest 停机后自动恢复。

HEVC 的普通 Codec2 图形池使用 API 36 gralloc 的 NV12 布局，使柔性 YUV ByteBuffer 与 AVC 半平面输入匹配。Surface 队列沿用 YV12。更新 `hevc.so` 和 `guest.idx` 时要配套构建 `native/grallocwrap/build.sh 36` 生成的 Guest gralloc。

用 NDK 28.2.13676358 执行 `python native/videobridge/build_codec2.py --help` 查看构建参数。构建使用 AOSP `android16-release` 的 Codec2 源码树和平台头文件，以及本地从目标固件媒体 APEX 提取的链接库。编译输入、固件和测试 APK 存放在本地。

- `--codec2` 对应 `frameworks/av/media/codec2`。
- `--headers` 下的 `foundation`、`utils`、`libutils`、`base`、`cutils`、`hardware`、`nativewindow`、`arect`、`system` 和 `log` 分别对应 AOSP 的 Stagefright Foundation、Binder libutils、libutils、libbase、libcutils、libhardware、nativewindow、arect、libsystem 和 liblog 头文件。
- `--guest-libraries` 指向该固件运行树 `apex/com.android.media.swcodec/lib64` 的本地链接副本。

AVC 编码桥使用 `--camera-encoder` 构建。Guest 调用 AVC 编码器时，1080p30 等已准入配置使用启动探针验证过的宿主硬件编码器。检测到相机 1080p60 能力时优先探测 60 fps，无法打开该档位时另测 30 fps。实际硬编由宿主日志记录编码器名称和输入输出帧数。

正常停止 Guest 后恢复两个原版解码库和 AVC 编码库。使用同签名的上一版 APK 覆盖可回滚。Codec2 适配器沿用原有组件名和能力声明。
