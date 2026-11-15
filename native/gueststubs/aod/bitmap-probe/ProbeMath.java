package app.redmagicemu.aodbitmapprobe;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Shared, host-testable checks for decoded opaque grayscale pixels. */
public final class ProbeMath {
    public static final class Result {
        public final String sha256;
        public final int opacityErrors;
        public final int channelErrors;
        private Result(String sha, int opacity, int channels) {
            sha256 = sha; opacityErrors = opacity; channelErrors = channels;
        }
    }

    public static Result inspect(int[] pixels, int width, int height) {
        if (pixels == null || width <= 0 || height <= 0 || (long) width * height != pixels.length)
            throw new IllegalArgumentException("invalid image dimensions");
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            int opacity = 0, channels = 0;
            for (int value : pixels) {
                int a = value >>> 24, r = (value >>> 16) & 255, g = (value >>> 8) & 255, b = value & 255;
                if (a != 255) ++opacity;
                if (r != g || r != b) ++channels;
                digest.update((byte) r);
            }
            StringBuilder hex = new StringBuilder(64);
            for (byte b : digest.digest()) hex.append(Character.forDigit((b >>> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
            return new Result(hex.toString(), opacity, channels);
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 unavailable", e);
        }
    }
    private ProbeMath() { }
}
