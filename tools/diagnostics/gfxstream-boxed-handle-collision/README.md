# gfxstream boxed-handle reverse-map cleanup

The fixed d7a `BoxedHandleManager` keeps a reverse map indexed only by a
driver handle's raw value. When a later box has the same raw value, deleting
or updating an earlier box unconditionally removes the later box's reverse
entry. Delayed removal also deletes the box without clearing its own reverse
entry. These paths matter when adding swapchain images to the host's existing
boxed-handle bookkeeping.

`fix.patch` only removes a reverse entry when it still names the box being
removed or updated, including the delayed-removal path. It retains the original
lock and callback order. It **does not solve collisions**: the map still has
one slot per raw value and cannot represent independent types, devices or
multiple live boxes with identical values. In particular, it is not sufficient
to register swapchain images, ensure isolation, restore older reverse entries,
implement snapshots or render a Guest frame. It is a narrow offline candidate,
not integrated into the renderer.

`run.py` extracts the actual d7a manager methods and compiles them with a fake
in-memory entity store and fake single-thread locks. The original exits with a
failure; the patched methods pass removal, update and delayed-removal cases.
This does not test the real `HybridEntityManager`, concurrency or GPU code.
The complete patched `VkDecoderGlobalState.cpp` also compiles as an Android
ARM64 object with the existing Android compile database; it is not linked or
run. The local source copy includes existing TRACE instrumentation.

`type-key.patch`, applied **after** `fix.patch`, adds the actual boxed handle's
type tag to reverse-map keys and passes the expected tag from all four
`unboxed_to_boxed` macro paths. The same raw value can then be reversed
independently for two different Vulkan object types. A behavioral test using
the real `HybridEntityManager` and real locks fails on the cleanup-only patch
and passes with both patches, including `addFixed`, update and delayed removal.
The full patched host source also compiles to an Android ARM64 object.

This second candidate still has only one slot per `(raw value, type)` and
cannot distinguish same-type collisions between devices or multiple live
objects. `mImageInfo` itself remains keyed only by raw `VkImage`. The patch
does not validate Guest handle types during unboxing, associate an object
with an authorized client, implement snapshot recovery, or make swapchain
images usable. It has not been linked, installed or exercised on a GPU.

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
python3 tools/diagnostics/gfxstream-boxed-handle-collision/make_patch.py "$SOURCE" /var/tmp/rme-boxed-collision-rebuilt.patch
cmp /var/tmp/rme-boxed-collision-rebuilt.patch tools/diagnostics/gfxstream-boxed-handle-collision/fix.patch
python3 tools/diagnostics/gfxstream-boxed-handle-collision/run.py "$SOURCE" # expected failure; needs host C++17 compiler
python3 tools/diagnostics/gfxstream-boxed-handle-collision/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-boxed-handle-collision/fix.patch
python3 tools/diagnostics/gfxstream-instance-android-surface/compile_android.py "$SOURCE" /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-boxed-collision-android-new --patch tools/diagnostics/gfxstream-boxed-handle-collision/fix.patch
python3 tools/diagnostics/gfxstream-boxed-handle-collision/make_type_patch.py "$SOURCE" /var/tmp/rme-boxed-type-rebuilt.patch
cmp /var/tmp/rme-boxed-type-rebuilt.patch tools/diagnostics/gfxstream-boxed-handle-collision/type-key.patch
ROOT=/var/tmp/rme-gfxstream-build.hTnuwsWV
export CXX="$ROOT/bin/cxx"
export CXXFLAGS="-I$ROOT/aemu/base/include"
export LDFLAGS="-L$ROOT/prefix/lib -Wl,-rpath,$ROOT/prefix/lib -laemu-base -pthread"
python3 tools/diagnostics/gfxstream-boxed-handle-collision/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-boxed-handle-collision/fix.patch --expect-typed --real-store # expected failure
python3 tools/diagnostics/gfxstream-boxed-handle-collision/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-boxed-handle-collision/fix.patch --type-patch tools/diagnostics/gfxstream-boxed-handle-collision/type-key.patch --real-store
python3 tools/diagnostics/gfxstream-instance-android-surface/compile_android.py "$SOURCE" /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-boxed-type-android-new --patch tools/diagnostics/gfxstream-boxed-handle-collision/fix.patch --patch tools/diagnostics/gfxstream-boxed-handle-collision/type-key.patch
```
