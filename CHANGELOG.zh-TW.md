# 更新日誌

[简体中文](CHANGELOG.md) · **繁體中文** · [English](CHANGELOG.en.md)

## RedMagicEMU 0.7.1-beta

- 修正部分主機手機匯入韌體時出現 `Invalid instance directory` 的問題，支援主機 Android 為應用程式資料目錄提供的路徑別名。
- 匯入前檢查應用程式資料目錄和 `files/images`，發現異常時指出具體目錄。

[0.7.1-beta 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.7.1-beta)

## RedMagicEMU 0.7.0-beta

- 執行個體設定新增預設關閉的 Magisk Root。開啟後重新啟動 Guest 即可使用內建的 Magisk for RedMagicEMU 31.1，設定和 preinit 資料隨執行個體儲存。K80 實測通過一般應用程式授權、撤銷，以及關閉 Root 後的啟動。
- 定位和方向資料統一由感測器開關控制。Guest 依需求接收主機定位、即時 GNSS 結果、目前 LTE 服務小區，以及未校準的陀螺儀和磁力計資料。K80 上高德收到位置更新並進入跟隨模式。
- AVC 和 HEVC 硬體解碼橋支援播放時切換解析度。K80 分別跑通硬體解碼橋的 720p → 1080p → 720p 切換，以及 B 站用戶端切換畫質後續播。
- 相機按主機能力選高畫質串流。1920×1080 和 1280×720 的寬螢幕串流可裁切成 4:3 取景，保留原始像素。數位變焦繼續使用中心裁切。
- 修正照片內容和浮水印方向，並修復 1080p60 檔位的取景與成片方向。常規錄影按輸出比例裁切，K80 上 720p30 和 1080p30 的取景、成片均恢復正常。
- 關閉 Vulkan 橋接並重新啟動 Guest 後撤下 Vulkan 能力宣告，GLES 繼續可用。重新開啟後恢復宣告。
- 修正 Adreno 共用緩衝區的快取記憶體類型。K80 完成 Geekbench 6 Vulkan 跑分，得分 16,178，並通過視窗呈現回歸。
- 沿用 0.6.9 的定位、電源和 Vulkan 最佳化，清理舊的 Guest 啟動屬性覆蓋。
- 配套的 NX809J 11.0.21mr 韌體提供 7 個下載分卷及合併驗證說明。底包署名為 **by 酷安@某贼 2025.6.29**，RedMagicEMU 負責 Guest 匯入相容調整。[韌體下載](https://github.com/poohpiggy/RedMagicEMU-NX809J-VM/releases/tag/11.0.21mr)

[0.7.0-beta 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.7.0-beta)

## RedMagicEMU 0.6.9-beta (unreleased)

- 定位由常駐訂閱改為依需求啟停。Guest 前景應用程式發起 GPS 請求時向主機取位置，離開應用程式或退到主機背景後釋放監聽。K80 高德收到 11 次位置更新。
- 開機等待電池服務超時時自動恢復一次服務，K80 冷啟動和 Guest 重新啟動均約 41 秒進入系統。虛擬 Wi-Fi 和乙太網路下停止無效的蜂窩資料重試，LTE 模式交由 Android 按策略退避。
- 復用交換鏈回讀命令，把 GPU 等待與回讀放到獨立線程，並最佳化主機 Vulkan 命令解碼。K80 的組合測試包跑完安兔兔 12.0.3，總分 1,507,188，GPU 233,389。

## RedMagicEMU 0.6.8-beta (unreleased)

### 圖形橋與硬體編碼

- 依據主機實際映射識別共用緩衝區的資料 FD，合併 DMA-BUF 請求。
- 接入主機 AVC 硬體編碼，K80 跑通安兔兔 1080p30 轉碼。統一 NV12 布局，修復成片綠紫偏色。

### 轉碼收尾與候選對比

- 修復異步轉碼結束時媒體服務的 Binder 崩潰。K80 的獨立對照連續三輪完整轉碼 462 幀。
- 保留雙向 Vulkan 管道。K80 兩輪整跑中，單向包總分 1,397,041、GPU 125,613，雙向包總分 1,414,315、GPU 157,925。

## RedMagicEMU 0.6.7-beta (unreleased)

- 修復後攝數位變焦和切換錄影模式閃退。按主機鏡頭公開的 JPEG 尺寸拍照，K80 後攝 4096×3072、前置相機 2592×1944。
- 打通後攝一般與專業 1080p60 錄影，校正畫面與錄音時間戳。專業模式接上 ISO、快門、手動對焦、EV、白平衡色溫和色調。
- 兼容後攝可同時儲存同次曝光的 DNG 與 JPEG。K80 實拍並解碼兩種檔案，一般錄影實測平均 60.10 fps。

## RedMagicEMU 0.6.6-beta

- 修復原廠相機因 GLES 外部紋理預覽導致的閃退。
- 接入 Vulkan Android 視窗和交換鏈，讓繪制命令直達主機算繪線程。K80 跑通安兔兔標準版 GPU 場景。
- 對支援相應解碼器的 Android 16 主機接入 H.264 和 HEVC 硬體解碼，修復媒體 Binder 檔案描述元泄漏。
- 匯入首頁匯入進度、執行個體設定和資料重置。浮動選單增加螢幕常亮。

[0.6.6-beta 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.6-beta)

## RedMagicEMU 0.6.5-beta (unreleased)

- 首頁執行個體卡片顯示匯入進度。大 ZIP 可直接從主機儲存空間讀取，解包與驗證並行推進，保留項目、大小、CRC、分割區結構和指紋檢查。
- K80 匯入 12.41 GiB 韌體約用 75 秒，首次啟動另計約 103 秒。

## RedMagicEMU 0.6.4-beta (unreleased)

- 改版首頁和關于頁，首頁集中展示執行個體、目標韌體和常用操作。
- 執行個體記憶體提供 4、6、8、12、16 GB 五檔，導航鍵和垂直同步跟隨執行個體設定，下次啟動時套用儲存值。

## RedMagicEMU 0.6.3-beta (unreleased)

- 在執行個體設定中加入 Guest 資料重置，保留韌體、執行個體選項和共用檔案，運行中禁止重置。
- 相容調整原廠 `/cache` 相對連結。K80 的隔離執行個體重置後重新進入原廠歡迎頁。

## RedMagicEMU 0.6.2-beta

- 常規 APK 自動接入主機 Vulkan 和原生 OpenCL。支援 FP16、線性緩衝區異步讀回和非阻塞上傳。
- K80 的 Adreno 750 跑通 Geekbench 6 Vulkan 和 OpenCL。性能浮窗將主機進程和驅動記憶體納入顯示。

[0.6.2-beta 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.2-beta)

## RedMagicEMU 0.6.1-beta (unreleased)

- 排查安兔兔 3D Lite 的 Unity 崩潰，處理 GLES 像素儲存空間、緩衝映射和重復綁定問題。隔離測試包在 K80 跑完兩個 Unity 場景。
- 搭建主機 Vulkan 服務、Guest 協議和計算路徑。視窗、交換鏈與呈現協議在此階段進入離線測試，後續版本接入運行環境。

## RedMagicEMU 0.6.0-beta

- 執行個體設定和浮動選單加入電池同步，支援跟隨主機或手動設定電量和充電狀態。
- 加速度計橋接擴展至陀螺儀、磁力計和主機位置。修正感測器標志與定位入口，恢復原廠指南針。
- 打通虛擬 LTE 資料，修復關閉虛擬 Wi-Fi 後仍可聯網的問題及相關啟動崩潰。恢復前後攝與一般拍照。
- 收斂為單一應用包 `app.redmagicemu`，提供美式英語、英式英語、簡體中文、繁體中文（台灣）和繁體中文（香港），其他系統語言改用美式英語。新執行個體采用 75% 預設布局。

[0.6.0-beta 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.0-beta)

## RedMagicEMU 0.5.0-beta (unreleased)

### 第一輪裁剪

- 圍繞指定 NX809J 韌體整理引擎、匯入鏈和功能依賴，移除廢棄原型。

### 第二輪裁剪

- 保留執行個體資料重置和已配對的圖形資源，刪去無實際入口的舊執行個體刪除路徑。最終選用約 41.6 MiB 的階段版本。

## RedMagicEMU 0.4.x (unreleased)

### RedMagicEMU 0.4.8-beta (unreleased)

- 驗證原廠相機取景、前後攝切換、瀏覽器調用、拍照和 DCIM 儲存。後續在 0.6.6 修復正式包的預覽閃退，在 0.6.7 擴展原生尺寸和錄影。

### RedMagicEMU 0.4.7-beta (unreleased)

- 快速關機時，正在進行的系統鈴聲索引會正常取消。
- Guest 增加 60、90、120、144 Hz 固定更新率檔位，並驗證四檔切換。

### RedMagicEMU 0.4.6-beta (unreleased)

- 第一輪梳理原廠 Sensors multi-HAL 的載入路徑，整理主機采樣時間和 Guest 事件接口。
- 第二輪打通三軸加速度計。Guest 系統和一般應用程式都收到隨手機晃動變化的讀數。

### RedMagicEMU 0.4.5-beta (unreleased)

- 移除容量滑塊，保留現有執行個體的儲存空間設定。

### RedMagicEMU 0.4.4-beta (unreleased)

- 第一輪建立開放熱點、狀態通知和乙太網路交接原型。
- 第二輪對齊 WLAN 設定頁、快捷面板和狀態列，修正 DNS 和啟動順序。退出飛行模式時恢復先前的 Wi-Fi 開關狀態，清理浮動選單入口。

### RedMagicEMU 0.4.3-beta (unreleased)

- 為模擬基帶補充 LTE 信號上報。Guest 狀態列顯示信號格，飛行模式往返後可恢復。

### RedMagicEMU 0.4.2-beta (unreleased)

- 為新執行個體自動準備指定鳴潮主題的螢幕待機兼容資料。實測人物前景位置、喚醒動畫和切換主題後恢復。

### RedMagicEMU 0.4.1-beta (unreleased)

- 相容調整 NX809J 的 `ztecfg`，恢復指定鳴潮主題的原廠桌布與圖示。
- 清理舊基帶入口，按執行個體生成虛擬 IMEI，系統界面顯示虛擬 IMEI SV。

## RedMagicEMU 0.3.0-beta

- **1A** 從原廠桌面啟動基線出發，檢查顯示、觸摸、文字輸入、共用儲存空間和 APK 安裝。
- **1B** 修復文字輸入路徑，接上模擬短信和來電，修正接聽、掛斷後第二次來電失敗。
- **1C** 通過乙太網路聯網和主機音訊播放回歸，補齊原廠鈴聲索引和預設鈴聲設定。
- **1D** 補齊錄音、媒體與應用回歸，修復普通振動橋。連續來電、計時器振動、網頁和影音場景進入本版。

[0.3.0-beta 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.3.0-beta)

## RedMagicEMU 0.2.0-alpha

- 修正正常啟動誤拉起充電服務的問題，處理目標韌體的電話服務依賴，Guest 首次進入原廠桌面。
- 實機跑通全新匯入、覆蓋安裝、Guest 重新啟動和主動關機，以及主機選單停止。

[0.2.0-alpha 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.2.0-alpha)

## RedMagicEMU 0.1.0-alpha

- **1A** 建立 RedMagicEMU 應用標識、圖示、項目說明和共用目錄。
- **1B** 建立指定 NX809J 韌體的 ZIP、分割區和指紋檢查，修正 ARM64 執行個體的 N64 引擎選擇。
- **1C** 首頁收斂為單執行個體，加入匯入取消、暫存清理和匯入中斷提示。

[0.1.0-alpha 發布說明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.1.0-alpha)

## S0 · 基線審計

- 以 AEmulator 為基線，梳理韌體匯入、Guest 啟動和主機橋接入口。設定 JDK 21、SDK 36 和 NDK r28，構建首份基線 APK。

## AEmulator 0.0.0.3（上游）

[上游更新日誌](CHANGELOG.md#aemulator-0003-upstream)
