# Offline AOD preview

`render_preview.py` is an **approximate offline visualization**, not Android
Canvas/Skia, a cache generator for installation, or an emulator fix. It does
not communicate with or modify a Guest. The caller supplies all image data;
no firmware, theme, or model data is included here.

Requires Python 3 and Pillow. Supply a background and an RGBA foreground of
the same dimensions, a foreground highest-point **Y coordinate in source
pixels**, the target screen density in dpi, and a new output directory outside
this repository whose parent already exists:

```text
python render_preview.py --background /path/to/background.jpg \
  --foreground /path/to/foreground.png --highest-y 166 --density 520 \
  --output /path/to/new-preview-directory
```

The tool refuses an existing output directory; it never overwrites earlier
results. It creates `vertical.jpg`, `horizontal_big.jpg`, and
`horizontal_small.jpg`. At 520 dpi their dimensions are 442×507, 390×455 and
390×455 respectively. No device files should be replaced with these images.

The geometry is derived from the specified NX809J SystemUI's
`layout/aod_one_shot_static_image`, dimension resources and the decoded
`DozeFeature.writeStaticImage` and `TransparentBorderView.drawFinal` methods.
The latter clears a rounded shape in both masks and additionally clears a top
rectangle for the foreground mask. The tool assumes the source image dimensions
match the portrait display/View dimensions and that a foreground exists. It
uses Pillow's bilinear scaling and supersampled masks; Android decoding,
display insets, Canvas/Skia blending and antialiasing can differ. Dynamic AOD
behavior and hardware compatibility are not tested by this preview.

Run the tests from the repository root with Pillow installed:

```text
python -m unittest -v native/gueststubs/aod/tools/test_render_preview.py
```

For the locally analyzed theme with 1216×2688 inputs, highest Y 166, density
520 and Pillow 13.0.0, the three JPEG SHA-256 digests match the earlier
independent visual approximation:

```text
vertical.jpg         d8e7392e40699ea31dbad0c482aaa175210600c435767629f681f82c755ccb15
horizontal_big.jpg   bf10aa079ee6f9ae2266db85689604ffd50b9472de31140cdec921b69410e4b2
horizontal_small.jpg 69008a48e88a04a8d0e3532d060e40dc8f37aad7148ed09585e67b37b0f57e61
```

Those digests do not guarantee identical output across Pillow or JPEG library
versions, and the comparison does not establish Android-rendered equivalence.
