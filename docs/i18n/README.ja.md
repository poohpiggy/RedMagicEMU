# RedMagicEMU

RedMagicEMU は [AEmulator](https://github.com/uxazu/AEmulator) を基に独立して保守される非公式プロジェクトです。AEmulator は元の [SENSES エミュレーター](https://github.com/Istratii-tech/SENSES) を基にしています。REDMAGIC の公式製品ではなく、REDMAGIC との提携もありません。

ゲストとして REDMAGIC NX809J の純正 Android 16 / API 36 / ARM64 ファームウェアを対象とします。この制限はゲストのファームウェアに適用され、ホスト端末の機種は制限しません。このファームウェアのサポートと動作はまだ実機で検証されていません。[進捗](../PROGRESS.md)と[基線](../BASELINE.md)を参照してください。

## ビルド

Android プロジェクトには Android SDK 36 と NDK r28 が必要です。基線ビルドには JDK 21 を使用しました。

```shell
./gradlew :app:assembleStandardDebug
```

ローカル開発では application ID `app.redmagicemu` を使用します。上流アプリとは別の Android アプリとしてインストールされ、プライベートデータは自動移行されません。移行方法が検証されるまで旧アプリとそのデータを残してください。

## 謝辞とライセンス

RedMagicEMU は [uxazu](https://github.com/uxazu) による AEmulator を基にしています。AEmulator は[原作者](https://t.me/istratii_tech1)による HTC Desire HD および HTC One M7 エミュレーターから発展しました。それらの成果と既存の著作権・ライセンス表示を尊重します。

このプロジェクトは [GPL-3.0](../../LICENSE) の下で配布されます。Android、REDMAGIC、その他の商標およびファームウェアはそれぞれの所有者に帰属します。
