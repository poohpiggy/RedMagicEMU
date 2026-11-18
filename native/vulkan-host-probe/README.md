# Host Vulkan Debug probe

The host log panel's Debug button runs the existing standalone
`tools/diagnostics/vulkan-compute/probe.c` in the existing `:vm` process through
the Debug-only native library. It runs only after a manual action, calls the
system Vulkan loader, and displays the driver-reported device name and type.
The computation checks all 64 output words; a reported CPU or virtual device is
not evidence of a physical GPU. This probe is not a Guest Vulkan bridge.

The existing diagnostic uses a 10-second fence wait, but individual driver
calls can still hang or crash the process. If queue completion is not known,
the probe preserves potentially in-flight Vulkan objects and refuses another
attempt until the process restarts. A failure is shown as a failure; the probe
does not modify Guest files or access hardware nodes itself.

Build only the Debug native library on Windows using the existing Android NDK:

```powershell
./native/vulkan-host-probe/build.ps1 -Ndk <path-to-ndk>
python native/vulkan-host-probe/tests/run.py --ndk <path-to-ndk>
```

The regression runs real Linux Vulkan through WSL lavapipe and checks normal
output, intentional mismatch, missing ICD, an injected fence failure and
refusal to retry after that failure. The injected failure follows an actual
completed wait; it does not simulate a real hung GPU. The Android binary is
cross-compiled only; driver availability, app namespace, and execution on a
device remain unverified.
