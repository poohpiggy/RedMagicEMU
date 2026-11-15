# 指定主题 AOD 缓存试验

本记录先描述 2026-11-10 在一台设备的 clone 实例 `9ewdvb` 上进行的手动受控试验，文末记录后续的新实例自动适配及其验证。不能凭主题名推定其他输入可直接使用同一遮罩。

## 依据和重建

指定固件的主题原图经原厂 QNN 模型在电脑端计算前景；模型输出和源图片不得加入 Git。原厂前景最高点为 `(880,166)`。本次三张息屏缓存按固件布局与定位公式在电脑近似合成，采用 JPEG 编码，**不是原厂 Android Canvas 输出**。本地预计算材料位于被 Git 忽略的 `local-data/aod-oem-20261110/`，其中清单和 README 记录摘要、尺寸及限制。相关重建工具见 [`native/gueststubs/aod/tools`](../native/gueststubs/aod/tools/README.md)。

试验前通过 Guest SettingsProvider 确认：`one_shot_main_top_y` 不存在，`one_shot_has_main_image=0`；试验时分别写入 166、1 并读回。原有背景文件保持不动，其试验前 SHA-256 为 `353eb332e1407e849f0b4fb6591e2d2e7f2216549d6fda9067b6d77368ed5d4a`。电脑主题原图和 Guest 背景构图一致但 JPEG 字节不同；不应以主题名称代替实际输入和显示配置核验。

Guest 正常关闭后，在 clone 实例的 `root/data/user_de/0/com.android.systemui/files/` 中，原先不存在的 `cur_depth_foreground_wp.png` 新增为 SHA-256 `c3ddf8254c17cb9920970df423bc09ce33ec914e7926c61a9277083bd750ff9b`；其余文件如下：

| 文件 | 原 SHA-256 | 试验 SHA-256 |
| --- | --- | --- |
| `aod_one_shot_vertical.png` | `42d64873093a88364cbb6ec9b562bcf017b93f874b9635e8b8932799c97c5855` | `d8e7392e40699ea31dbad0c482aaa175210600c435767629f681f82c755ccb15` |
| `aod_one_shot_horizen_big.png` | `5e8ed290be4e2eee10b87b4e77a7651cc76bbd7f341b909019521a99e330818c` | `bf10aa079ee6f9ae2266db85689604ffd50b9472de31140cdec921b69410e4b2` |
| `aod_one_shot_horizen_small.png` | `8f3bd2799f299dede9d2aeb2d0250e2a63c87d7a187eb280e5b9c859c932368f` | `69008a48e88a04a8d0e3532d060e40dc8f37aad7148ed09585e67b37b0f57e61` |

三张原缓存的备份在同一目录，名称为 `.rme-3fc86f6a54db4b57a86924c8ea1aeb3e-` 加原文件名再加 `.bak`。完整事务记录在 clone 应用私有目录 `files/rme-aod-stage-4be1209045024eff929758cee3c7141f/transaction-3fc86f6a54db4b57a86924c8ea1aeb3e.log`。替换前后核验过摘要、600 权限、宿主属主、SELinux 标签、单硬链接及未列出 `user.aemu.own`；逐文件操作并非跨文件原子事务。

## 条件回滚

先核对当前实例和主题；使用运行中的 Guest SettingsProvider，仅当设置仍为本次写入的值时，将 `one_shot_has_main_image` 恢复为 0，并删除原先不存在的 `one_shot_main_top_y`，逐项读回。正常关闭 Guest 并确认相关进程退出。仅当每个目标文件的当前摘要仍等于上表中的试验摘要时，才用同目录核对过原摘要和元数据的对应备份恢复；前景原先不存在，仅在其摘要仍为本次前景摘要时删除。若用户已换主题或文件/设置已变化，停止并保留备份，不能覆盖后续修改。最后重启并核对。不要直接编辑或整体恢复二进制设置数据库，也不要修改 SELinux 策略。

先前另有名为 `aod_one_shot_vertical.png.rme-backup-a80805d5b4f84958aa4cffbb50265e12` 的旧备份，其标签与原文件不一致，不可直接用于恢复。本次结果仅在该实例实测静态息屏改善，用户随后确认动画正常；切主题后的再生成、其他样式及其他实例未验证。

## 正式版新实例的手动处理（2026-11-10）

正式版中新导入带 `ztecfg` 的实例 `3pafyq` 首次启动选择 `IP_MC_W_CN` 和 `default_theme_64`。日志依次记录背景保存、QNN 传输和初始化失败、空前景以及三张息屏缓存保存；原设置为 `one_shot_main_top_y` 不存在、`one_shot_has_main_image=0`，显示为 1216×2688、520 dpi。背景及三张缓存的处理前摘要与上文试验相同，前景文件不存在。首次息屏截图显示人物截到胸口。

Guest 壁纸管理器的 `wallpaper_lock_orig` 是 1216×2688 PNG，SHA-256 为 `df61b7cb7456a75e4078a30c6b38047938cb48823deea505df4486844d6ab150`；临时 Guest Android `BitmapFactory` 探针取得的 RGB 摘要是 `67ccd417f209d9d2904013b0158456997dd0db8f52f7e6806ed50540f421f636`，与原厂主题源图的已记录解码结果一致。该探针已清理，未导出壁纸；它没有捕获原厂 JNI 的实际输入，因此不能单独证明 JNI 输入完全相同。

在运行中的 Guest 内设置并读回 `one_shot_main_top_y=166`、`one_shot_has_main_image=1`；正常关闭 Guest 并确认进程退出后，在同一运行副本的 SystemUI files 目录备份三张缓存，再安装上表所列的四个试验文件。背景未改变。新文件和备份摘要、600 权限、属主、单链接及 SELinux 标签均经独立核对；事务日志在正式版应用私有目录 `files/rme-aod-3pafyq-2e47af87c2e64115a73e4ab81ef6d420/transaction-corrected.log`。替换和校验记录已到 `COMMITTED`，脚本尾部两个多余 CR 空行导致 shell 退出码为 1，随后独立复核了所有目标和备份。逐文件替换不是整体原子事务。

三份备份位于上述 SystemUI files 目录，名称为 `.rme-2e47af87c2e64115a73e4ab81ef6d420-` 加原文件名再加 `.bak`，各自的原始摘要见上表。回滚时仅在当前实例、主题、设置及文件摘要仍分别等于本次写入值的条件下，按上文“条件回滚”恢复设置和缓存；若已变化则停止并保留备份。重启后已读回 166/1，背景和四个目标摘要保留；先前自动截图极暗，随后清晰截图显示人物头部已位于圆形顶部。此实例的动画和切换主题后表现尚未确认。该处理仍是手动的，未接入 EMU 自动修复。

## 新实例自动适配（2026-11-10）

指定固件的新实例导入时，EMU 在未发布的运行副本中验证固件指纹、ztecfg 选择的 `IP_MC_W_CN` / `default_theme_64`、原厂 SystemUI APK 和模型摘要，再准备下列文件；原厂 APK 和原始固件不被修改。使用的灰度遮罩作为项目资源，其来源和摘要见 [`README-alpha-resource.md`](../native/gueststubs/aod/tools/README-alpha-resource.md)。

| 新实例运行副本路径，相对于 Guest root | 内容及 SHA-256 |
| --- | --- |
| `system_ext/priv-app/SystemUI_MFV/lib/arm64/libzte_saliency.so` | EMU 源码构建的有限代理，`1cd4020a07a0f34929a501ca9b56ac0a9fca9ea6ce97ca93ff3fd6e29a48a936` |
| `system_ext/priv-app/SystemUI_MFV/lib/arm64/rme_aod_mask.raw` | 授权 PNG 解码得到的单通道遮罩，`019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a` |

代理先保留原厂初始化和可用时的原厂实现；原厂不可用时，仅对尺寸及 RGB 摘要严格匹配的指定图片返回预计算前景。其他输入不使用该遮罩，失败按已检查接口返回空结果，不声称 QNN HTP 可用。安装器拒绝覆盖已有 `lib` 目录，目标或资产校验不符时记录不可用；取消或无法确认清理时不发布半成品。准备过程依赖应用私有导入租约内的合作式独占，不宣称防御同 UID 非合作进程并发改动。

正式版 `0.4.0-beta` / code 8 的一次全新导入实例 `uxbbdn` 中，SystemUI 实际映射了 EMU 代理以及 APK 内原厂 JNI 库和 `libzcv.so`。未手工写入 AOD 文件或设置，原厂组件自行生成前景和三个缓存，设置读回 `one_shot_has_main_image=1`、`one_shot_main_top_y=166`；截图确认人物头部位置正确，唤醒动画正常，切换主题再切回后仍正常。日志另记录一次原厂唤醒动画从起点运行至终点且未取消。该结果限于这一主题和宿主；其他壁纸、其他宿主及长期稳定性未整体验证。

如需撤销此实例的代理，先正常关闭 Guest 并确认相关进程退出；确认仍是目标实例后，仅在上表两个文件仍分别具有所列摘要且无后续修改时删除这两个运行副本文件，再仅删除已空的 `arm64` 和 `lib` 目录。若文件或目录已变化，停止并保留，不覆盖实例数据。重新启动后恢复由原厂 APK 加载库；已由原厂 SystemUI 生成的缓存和设置不会因删除代理自动回滚。若需禁止新导入实例再准备代理，应同时撤回 EMU 的对应导入接线和资源并重新构建；原厂 APK 始终保持不变。
