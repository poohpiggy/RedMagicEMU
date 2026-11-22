# API 36 视频硬解桥接

Guest Codec2 将无 DRM 的 H.264 和 HEVC 压缩帧发送到宿主 MediaCodec，经私有 Unix socket 取回 8 位 I420 帧。每次服务一个解码会话。宿主校验每帧实际裁剪后的尺寸，Guest 随解码器返回的尺寸更新 Codec2 输出格式。自适应会话的每一边均不得超过协商上限，最大 4096×2160。

常规 APK 使用 `android/` 内的桥接程序和两个 Guest 适配库。可用 `-PrmeVideoBridgeDir=<bundle>` 选择本地构建的组件。正常启动时通过 Android 36 的公开 Media NDK 确认宿主的两种硬解均支持自适应播放，再校验并替换指定 NX809J 固件运行树中的两个解码库。能力不足时由 Guest 原版解码器继续播放。原库在同目录保留为 `libcodec2_soft_{avc,hevc}dec.rme-original.so`，Guest 停机后自动恢复。解码桥协议更新时须配套构建宿主程序、两个 Guest 解码库及 `guest.idx`。

HEVC 的普通 Codec2 图形池使用 API 36 gralloc 的 NV12 布局，使柔性 YUV ByteBuffer 与 AVC 半平面输入匹配。Surface 队列沿用 YV12。更新 `hevc.so` 和 `guest.idx` 时要配套构建 `native/grallocwrap/build.sh 36` 生成的 Guest gralloc。

用 NDK 28.2.13676358 执行 `python native/videobridge/build_codec2.py --help` 查看构建参数。构建使用 AOSP `android16-release` 的 Codec2 源码树和平台头文件，以及本地从目标固件媒体 APEX 提取的链接库。编译输入、固件和测试 APK 存放在本地。

- `--codec2` 对应 `frameworks/av/media/codec2`。
- `--headers` 下的 `foundation`、`utils`、`libutils`、`base`、`cutils`、`hardware`、`nativewindow`、`arect`、`system` 和 `log` 分别对应 AOSP 的 Stagefright Foundation、Binder libutils、libutils、libbase、libcutils、libhardware、nativewindow、arect、libsystem 和 liblog 头文件。
- `--guest-libraries` 指向该固件运行树 `apex/com.android.media.swcodec/lib64` 的本地链接副本。

用 `python3 native/videobridge/tests/decode_frame_test.py` 核对固定尺寸帧 CRC、同一会话 720p→1080p→720p 的帧数据和尺寸，以及越界尺寸的拒绝行为。此测试需 Linux 或 WSL 中的 C 编译器。

AVC 编码桥使用 `--camera-encoder` 构建。Guest 调用 AVC 编码器时，1080p30 等已准入配置使用启动探针验证过的宿主硬件编码器。检测到相机 1080p60 能力时优先探测 60 fps，无法打开该档位时另测 30 fps。实际硬编由宿主日志记录编码器名称和输入输出帧数。

正常停止 Guest 后恢复两个原版解码库和 AVC 编码库。使用同签名的上一版 APK 覆盖可回滚。Codec2 适配器沿用原有组件名和能力声明。
