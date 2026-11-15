# Fixed theme alpha resource

`app/src/main/assets/engines/redmagic/aod_theme64_alpha.png` contains only the
8-bit grayscale alpha mask for the investigated theme. It was added following
the user's explicit choice. It contains no RGB channels or PNG metadata. It
does not add the original wallpaper, firmware model, raw inference output, or
SDK to this repository. The mask still represents the theme's silhouette; it
is not a general-purpose model or a guarantee of anonymity.

Provenance of the input used for this specific mask:

- Theme: `system/etc/custom_config/themes/IP_MC_W_CN/default_theme_64.zmtp`,
  entry `lockscreen/wallpaper1.jpg`; source JPEG SHA-256
  `ce26c1077f1ea969c651c286ade99251025a14aefbf7f067703c18402223a3a7`.
- Firmware model: `system/etc/custom_config/app/systemUI/depth/wallpaperMattingCache.bin`;
  SHA-256 `d2f72574ac8f68144e2be04f168ad1eafe45ef043a279388f0b2fd36d6cb695f`.
  Its output was computed once on the computer with QAIRT 2.37.0; the
  640×640 float output SHA-256 is
  `dd192e50c893be795c9c74fc6f97f8c18914c63ca49606c900f4fcfcbc3cae4`.
- The offline postprocessor derives the alpha from that output and the theme
  image using the inspected OEM processing sequence. Equivalence with the
  firmware's actual Android/OpenCV execution is not established. The source
  alpha is 1216×2688 bytes, SHA-256
  `019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a`.
  Existing OpenCV attribution remains in `../OPENCV_RESIZE_LICENSE.txt`.
- The checked-in grayscale PNG is 61,320 bytes, SHA-256
  `1aafc69b321aa880471e7f16bedba07f644608f678c512d5b732564f7a314849`.
  Other zlib versions may encode the same pixels differently; the raw alpha
  digest and decoded pixel equality identify the mask independently of encoding.

QAIRT's license governs use of the SDK; it does not establish redistribution
rights for the firmware or theme. The SDK and its notices are not bundled here.
Preserve all applicable notices and licenses when using those components.

To rebuild, provide the user's already-computed alpha file explicitly and a
**new** output path:

```sh
python native/gueststubs/aod/tools/prepare_alpha_resource.py \
  --alpha /private/path/alpha.raw --output /private/path/new-alpha.png
```

The tool checks the exact source length and SHA-256, emits only IHDR/IDAT/IEND
chunks, verifies the PNG structure and every decoded pixel, and refuses to
overwrite an existing file. To verify an existing resource without changing it:

```sh
python native/gueststubs/aod/tools/prepare_alpha_resource.py \
  --alpha /private/path/alpha.raw \
  --check app/src/main/assets/engines/redmagic/aod_theme64_alpha.png
```

Tests run with:

```sh
python -B -m unittest discover -s native/gueststubs/aod/tools/tests -p 'test_prepare_alpha_resource.py'
```

For the verified target, import preparation decodes this resource and stages a
checked raw mask in the new instance's runtime copy. The proxy still checks the
actual JNI input; it is not a general segmentation model. Guest evidence,
scope, and rollback are recorded in [the AOD record](../../../../docs/AOD_THEME_CACHE.md).
