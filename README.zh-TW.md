<p align="center">
  <img src="assets/redmagicemu-icon.svg" width="128" height="128" alt="RedMagicEMU">
</p>

<h1 align="center">RedMagicEMU</h1>

[简体中文](README.md) · **繁體中文** · [English](README.en.md)

RedMagicEMU 是一個獨立維護的 Android 韌體模擬器。目前支援匯入 REDMAGIC NX809J 的原廠 Android 16（API 36、ARM64）韌體，在 Android 主機上執行原廠系統。主機裝置品牌不限。

[下載 0.6.6-beta APK](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.6-beta)

## 0.6.6-beta

- 首頁透過執行個體卡片顯示韌體及執行狀態。匯入時會在卡片上顯示進度，縮短解包與驗證的等待時間。
- 可以個別重設執行個體的資料。執行個體設定提供記憶體、解析度及垂直同步選項，浮動選單可以讓螢幕保持開啟。
- Vulkan 支援 Android 視窗和交換鏈。修正安兔兔 GPU 場景啟動時閃退的問題，並減少主機與 Guest 之間的命令往返及畫面複製開銷。
- Android 16 主機支援對應的硬體解碼器時，Guest 可透過主機解碼 H.264 和 HEVC。修正媒體服務釋放 Binder 描述元時的資源洩漏。

## 已實作的功能

- **韌體匯入**。匯入原廠 ZIP，檢查封存檔項目、映像檔大小、CRC、分割區結構及韌體指紋。
- **圖形與媒體**。支援 GLES、Vulkan 視窗呈現及主機原生 OpenCL，並提供 H.264 與 HEVC 硬體解碼。
- **執行個體管理**。各執行個體各自儲存資料，支援重設資料、設定記憶體、調整畫面比例及設定 Guest 電池狀態。
- **裝置功能**。支援音訊播放和錄音、前後鏡頭切換及一般拍照、震動、加速度計、陀螺儀、磁力計，以及主機的位置資訊。支援指定原廠主題的永遠顯示功能。
- **網路與通訊**。支援乙太網路、虛擬 Wi-Fi、虛擬 LTE、模擬來電及簡訊。
- **操作與檔案**。支援主機與 Guest 之間的檔案分享、安裝 APK、Android 導覽鍵、浮動選單、螢幕擷取及保持螢幕開啟。
- **介面語言**。支援美式英語、英式英語、簡體中文、繁體中文（台灣）及繁體中文（香港）。

## 安裝與建置

下載並安裝 APK，自行準備對應的原廠韌體 ZIP，再從首頁匯入並啟動執行個體。

從原始碼建置需要 JDK 21、Android SDK 36 和 NDK r28。

```sh
./gradlew :app:assembleRelease
```

## 致謝與授權條款

RedMagicEMU 是非官方專案，基於 [AEmulator](https://github.com/uxazu/AEmulator) 及 [SENSES](https://github.com/Istratii-tech/SENSES) 的成果。

專案以 [GPL-3.0](LICENSE) 授權釋出。商標及原廠韌體歸各自的擁有者所有。
