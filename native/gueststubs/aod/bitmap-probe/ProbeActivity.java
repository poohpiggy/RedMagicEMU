package app.redmagicemu.aodbitmapprobe;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Bundle;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.zip.CRC32;
import java.util.zip.DeflaterOutputStream;

public final class ProbeActivity extends Activity {
    private static final String EXPECTED = "019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a";
    private static final int[] FIXTURE = { 0x001740be, 0x01c8781e, 0x80b4642d, 0xff7b22fa };
    private final StringBuilder report = new StringBuilder();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        new Thread(() -> {
            try {
                inspectMask();
                inspectFixture();
                report.append("RESULT=PASS\n");
            } catch (Throwable e) {
                report.append("RESULT=FAIL ").append(e.getClass().getName()).append(": ")
                    .append(String.valueOf(e.getMessage())).append('\n');
            }
            try (java.io.FileOutputStream out = openFileOutput("report.txt", MODE_PRIVATE)) {
                out.write(report.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                out.getFD().sync();
            } catch (Exception e) { Log.e("RmeAodBitmapProbe", "report_write_failed", e); }
            Log.i("RmeAodBitmapProbe", "probe_finished");
            runOnUiThread(this::finish);
        }, "aod-bitmap-probe").start();
    }

    private static BitmapFactory.Options options(boolean premultiplied) {
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inScaled = false;
        opts.inPremultiplied = premultiplied;
        opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
        return opts;
    }

    private void inspectMask() throws Exception {
        Bitmap mask;
        try (java.io.InputStream in = getAssets().open("mask.png")) {
            mask = BitmapFactory.decodeStream(in, null, options(false));
        }
        if (mask == null || mask.getWidth() != 1216 || mask.getHeight() != 2688 || mask.getConfig() != Bitmap.Config.ARGB_8888)
            throw new IllegalStateException("mask decode shape/config mismatch");
        int[] pixels = new int[1216 * 2688];
        mask.getPixels(pixels, 0, 1216, 0, 0, 1216, 2688);
        ProbeMath.Result r = ProbeMath.inspect(pixels, 1216, 2688);
        report.append("MASK_SHA256=").append(r.sha256).append('\n')
            .append("MASK_OPACITY_ERRORS=").append(r.opacityErrors).append('\n')
            .append("MASK_CHANNEL_ERRORS=").append(r.channelErrors).append('\n');
        mask.recycle();
        if (r.opacityErrors != 0 || r.channelErrors != 0 || !EXPECTED.equals(r.sha256))
            throw new IllegalStateException("mask channel or digest mismatch");
    }

    private void inspectFixture() throws Exception {
        byte[] png = fixturePng();
        Bitmap straight = BitmapFactory.decodeByteArray(png, 0, png.length, options(false));
        Bitmap premult = BitmapFactory.decodeByteArray(png, 0, png.length, options(true));
        if (straight == null || premult == null || straight.getConfig() != Bitmap.Config.ARGB_8888 ||
            premult.getConfig() != Bitmap.Config.ARGB_8888 || straight.isPremultiplied() || !premult.isPremultiplied())
            throw new IllegalStateException("fixture decode config mismatch");
        int[] plain = new int[4], fromPremult = new int[4];
        straight.getPixels(plain, 0, 4, 0, 0, 4, 1);
        premult.getPixels(fromPremult, 0, 4, 0, 0, 4, 1);
        ByteBuffer rawStraight = ByteBuffer.allocate(straight.getByteCount());
        ByteBuffer rawPremult = ByteBuffer.allocate(premult.getByteCount());
        straight.copyPixelsToBuffer(rawStraight);
        premult.copyPixelsToBuffer(rawPremult);
        report.append("FIXTURE_EXPECTED=").append(Arrays.toString(FIXTURE)).append('\n')
            .append("FIXTURE_STRAIGHT_GETPIXELS=").append(Arrays.toString(plain)).append('\n')
            .append("FIXTURE_PREMULT_GETPIXELS=").append(Arrays.toString(fromPremult)).append('\n')
            .append("FIXTURE_STRAIGHT_RAW=").append(hex(rawStraight.array())).append('\n')
            .append("FIXTURE_PREMULT_RAW=").append(hex(rawPremult.array())).append('\n');
        straight.recycle(); premult.recycle();
        if (!Arrays.equals(plain, FIXTURE)) throw new IllegalStateException("straight fixture colors changed");
        for (int i = 0; i < FIXTURE.length; ++i)
            if ((fromPremult[i] >>> 24) != (FIXTURE[i] >>> 24)) throw new IllegalStateException("fixture alpha changed");
        if (fromPremult[3] != FIXTURE[3]) throw new IllegalStateException("opaque fixture color changed");
    }

    private static String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) result.append(Character.forDigit((b >>> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
        return result.toString();
    }

    private static void chunk(DataOutputStream out, String type, byte[] data) throws Exception {
        byte[] name = type.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        CRC32 crc = new CRC32(); crc.update(name); crc.update(data);
        out.writeInt(data.length); out.write(name); out.write(data); out.writeInt((int)crc.getValue());
    }

    /** Self-generated RGBA PNG: no personal image, GPU or Bitmap encoder involved. */
    private static byte[] fixturePng() throws Exception {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        DataOutputStream png = new DataOutputStream(output);
        png.writeLong(0x89504e470d0a1a0aL);
        ByteArrayOutputStream header = new ByteArrayOutputStream();
        DataOutputStream h = new DataOutputStream(header);
        h.writeInt(4); h.writeInt(1); h.writeByte(8); h.writeByte(6); h.writeByte(0); h.writeByte(0); h.writeByte(0);
        chunk(png, "IHDR", header.toByteArray());
        ByteArrayOutputStream compressed = new ByteArrayOutputStream();
        try (DeflaterOutputStream z = new DeflaterOutputStream(compressed)) {
            z.write(0);
            for (int p : FIXTURE) { z.write(p >>> 16); z.write(p >>> 8); z.write(p); z.write(p >>> 24); }
        }
        chunk(png, "IDAT", compressed.toByteArray());
        chunk(png, "IEND", new byte[0]);
        return output.toByteArray();
    }
}
