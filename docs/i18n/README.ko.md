# RedMagicEMU

RedMagicEMU는 [AEmulator](https://github.com/uxazu/AEmulator)를 기반으로 독립적으로 유지 관리되는 비공식 프로젝트입니다. AEmulator는 원래의 [SENSES 에뮬레이터](https://github.com/Istratii-tech/SENSES)를 기반으로 합니다. REDMAGIC 공식 제품이 아니며 REDMAGIC과 제휴 관계가 없습니다.

이 프로젝트는 원본 REDMAGIC NX809J Android 16 / API 36 / ARM64 펌웨어를 게스트로 대상으로 합니다. 이 제한은 게스트 펌웨어에 적용되며 호스트 기기 모델은 제한하지 않습니다. 해당 펌웨어의 지원 및 동작은 아직 실제 기기에서 검증되지 않았습니다. [진행 상황](../PROGRESS.md)과 [기준 기록](../BASELINE.md)을 참조하세요.

## 빌드

Android 프로젝트에는 Android SDK 36과 NDK r28이 필요합니다. 기준 빌드에는 JDK 21을 사용했습니다.

```shell
./gradlew :app:assembleStandardDebug
```

로컬 개발에는 application ID `app.redmagicemu`를 사용합니다. 업스트림 앱과 별도의 Android 설치이므로 비공개 데이터는 자동으로 이전되지 않습니다. 이전 방법이 검증될 때까지 기존 앱과 데이터를 보관하세요.

## 기여 및 라이선스

RedMagicEMU는 [uxazu](https://github.com/uxazu)의 AEmulator를 기반으로 합니다. AEmulator는 [원작자](https://t.me/istratii_tech1)의 HTC Desire HD 및 HTC One M7 에뮬레이터에서 발전했습니다. 이들의 작업과 기존 저작권 및 라이선스 고지를 존중합니다.

이 프로젝트는 [GPL-3.0](../../LICENSE)에 따라 배포됩니다. Android, REDMAGIC, 기타 상표 및 펌웨어는 각 소유자의 자산입니다.
