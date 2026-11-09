# Offline fake Guest vibration transport check

This test runs two self-created Linux processes. A fake Guest sends short on/off commands to the actual host-side frame receiver over a local `SOCK_SEQPACKET` socket, and the receiver records them in a fake sink. No Android service, vibrator HAL, host motor, or real Guest runs. One-byte acknowledgements are test-only synchronization and are not part of the product protocol.

The check covers ordered on/off, bounded duration, sender-side stop, an absent post-stop message, and a closed transport. The receiver uses kernel credentials and pidfd for this test process. None of this validates a Guest UID, Android permission, AppOps, framework policy, actual HAL, Android native runtime, or hardware. The sender is test-only and is not compiled into an APK or Guest artifact.

```sh
gcc -std=gnu11 -Wall -Wextra -Werror \
  native/gueststubs/vibrator/tests/fake_bridge/fake_guest.c \
  native/gueststubs/vibrator/tests/fake_bridge/integration_test.c \
  native/hostjni/vibration_host_gate.c \
  native/gueststubs/radio-bridge/gate.c \
  native/gueststubs/radio-bridge/receive.c \
  -o /tmp/rme_vibrator_fake_cross_process
/tmp/rme_vibrator_fake_cross_process
```

The test exits with status 77 when pidfd is unavailable; that result is a skip, not a pass. A watchdog prevents a permanently stuck test.
