# Magisk for RedMagicEMU

This build uses [Magisk v31.1](https://github.com/topjohnwu/Magisk/tree/v31.1) at commit `0972f67d97f9b6302b67c5171283330c74114b4a`. The upstream license is included alongside the patch. The Guest manager has its own package name and the daemon checks the certificate of that manager.

Clone the upstream tag with its submodules and apply `v31.1-redmagicemu.patch`. Follow the [upstream build instructions](https://topjohnwu.github.io/Magisk/build.html) to install the SDK, JDK and ONDK. Set `version=31.1`, `versionCode=31100` and `abiList=arm64-v8a` in a local Magisk build config. Set the signing fields to a dedicated Magisk signing key and keep the config and key outside Git. Use the same signing config for both builds.

```sh
git apply /path/to/RedMagicEMU/third_party/magisk/v31.1-redmagicemu.patch
python3 build.py -r -c /path/to/local-config.prop native
python3 build.py -r -c /path/to/local-config.prop app
```

Verify the manager APK signature and compute its SHA-256 before building the host APK. Pass the APK path and digest as `-PrmeMagiskApk` and `-PrmeMagiskApkSha256`. RedMagicEMU stages the validated Guest binaries and scripts before starting the daemon. The environment check uses `/data/adb/rme-magisk-preinit` in the instance through a runtime symlink. Physical boot image installation is unavailable in the adapted manager's home screen.

To roll back, shut down the Guest and turn off Magisk in its instance settings. The staged tools and daemon are removed at shutdown. Policy data and the instance-specific preinit directory persist across boots.
