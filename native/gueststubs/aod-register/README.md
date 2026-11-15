Archived from the working tree on 2026-11-15. Original source, test, README and SHA-256 manifest are preserved in E:/RedMagicEMU-checkpoints/0.4.0-camera-20261115/before-aod-register-cleanup/.

# Experimental ART symbol lookup

This incomplete experiment is not wired into the emulator and must not be
enabled. It does not register JNI methods or provide a Guest AOD fix.

Isolated tests showed that `RegisterNatives` can fail after changing only some
methods; recovery can also fail. Exact target identity, exception behavior,
zygote lifecycle and safe rollback have not been established. Do not use this
code in normal Guest startup.
