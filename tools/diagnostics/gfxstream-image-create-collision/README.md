# Image creation collision reproduction

This is a failing CPU-only reproduction, not a fix. With the isolated
swapchain image registration candidate applied, the real `on_vkCreateImage`
method can overwrite a registered swapchain image's `ImageInfo` when another
device returns the same raw image value. Run `run.py` against the pinned local
d7a source to reproduce exit status 1. The fake driver does not access a GPU.

Same-device raw collisions also need a design that distinguishes the newly
created image from swapchain-owned images and respects `vkDestroyImage` lifetime
rules. The reproduction has no production integration.
