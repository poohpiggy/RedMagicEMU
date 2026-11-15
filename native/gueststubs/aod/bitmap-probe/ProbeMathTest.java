import app.redmagicemu.aodbitmapprobe.ProbeMath;

public final class ProbeMathTest {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    private static void invalid(int[] p, int w, int h) {
        try { ProbeMath.inspect(p, w, h); throw new AssertionError("accepted invalid image"); }
        catch (IllegalArgumentException expected) { }
    }
    public static void main(String[] args) {
        ProbeMath.Result good = ProbeMath.inspect(new int[] { 0xff000000, 0xff404040,
            0xff808080, 0xffffffff }, 2, 2);
        check(good.opacityErrors == 0 && good.channelErrors == 0);
        check(good.sha256.equals("81f9456ee0bb909b7fb7c887c21c21f9fc45430e881dd5d92450609410fc3936"));
        ProbeMath.Result bad = ProbeMath.inspect(new int[] {0x00102030, 0xff010201}, 2, 1);
        check(bad.opacityErrors == 1 && bad.channelErrors == 2);
        invalid(new int[2], 1, 1);
        invalid(new int[0], Integer.MAX_VALUE, Integer.MAX_VALUE);
        invalid(new int[0], 0, 1);
        invalid(null, 1, 1);
        System.out.println("AOD_BITMAP_PROBE_MATH_OK");
    }
}
