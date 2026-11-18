# Experimental Unity COPY_WRITE mapping

This is a narrow EMU bridge experiment for the previously observed Antutu Lite
Unity workload. It activates only for process
`com.antutu.benchmark.full.lite:unity`, target `GL_COPY_WRITE_BUFFER` and access
flags `0x36` (write, invalidate range, explicit flush, unsynchronized) or
`0x6` (write, invalidate range). The `0x6` mirror is written back at unmap;
the `0x36` mirror is written only on explicit flush. Other
processes retain the previous mapping implementation. This is not a general
GLES buffer mapping implementation or evidence that the benchmark now passes.

The selected path keeps at most eight mirrors per share group, each at most
64 MiB. Mirrors start zeroed. Deleting a matching name retires the mirror and
suppresses ambiguous writes; a retired allocation and its slot remain until
the last shared context is destroyed. This preserves memory lifetime in the
tested cases but repeated deletions or context destruction can exhaust the
eight slots and cause later maps to fail. After deleting the current COPY_WRITE
name, the experiment refuses a new map until a later bind; it does not confirm
whether the host accepted that bind.

The host still does not keep a real GL mapping or acknowledge map writes.
Consequently this path cannot establish host-side bounds and access validation,
correct GL errors, `GL_BUFFER_MAPPED`, cross-target operations, cross-context
map/unmap semantics, successful GPU writes, or recovery from disconnects.
Host failures can remain unobserved at unmap. The general mapping and
shared-context bugs outside this exact experiment remain. Do not describe this
as a complete fix or use this test as proof of device rendering.

The runner compiles the complete production encoder with the existing memory
transport substitute and uses the real process-name gate. It checks selected
and unselected processes, simultaneous `0x6` maps, mixed `0x6`/`0x36` maps,
nonzero-offset writeback and explicit byte writes, pointer
lifetime across the tested deletion and context destruction sequences,
range checks, zero initialization and deletion before mapping. The substitute
does not execute a real host GL driver or emulate the Android application.

```sh
sh tools/diagnostics/gles-unity-map-experiment/run.sh
RME_SANITIZE=address sh tools/diagnostics/gles-unity-map-experiment/run.sh
sh tools/diagnostics/gles-unity-map-experiment/log-run.sh
```

For the selected process and target, rejected maps emit an `aemu-gl` message
starting `unity_map_reject`, with the reason, buffer name, offset, length and
flags. Each reason is logged at most once per context. The log runner captures
the actual Android log call and tests all 16 failure reasons, exact fields and
duplicate suppression, including injected allocation failures. This only
records failures known to the guest-side encoder; a successful map log is not
emitted and host-side write failures can still go undetected.

No phone, device Guest, firmware or installed APK is modified by these tests.
