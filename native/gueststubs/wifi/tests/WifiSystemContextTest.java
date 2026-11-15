package app.aemu.stub;

import android.app.ActivityThread;
import android.content.Context;

public final class WifiSystemContextTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        ActivityThread.current = null;
        ActivityThread.systemMainCalls = 0;
        Context first = WifiSystemContext.obtain();
        check(first != null && ActivityThread.systemMainCalls == 1, "create when missing");
        check(WifiSystemContext.obtain() == first && ActivityThread.systemMainCalls == 1, "reuse existing thread");
        ActivityThread.current.context = null;
        boolean rejected = false;
        try { WifiSystemContext.obtain(); }
        catch (IllegalStateException expected) { rejected = true; }
        check(rejected, "reject null context");
        System.out.println("WifiSystemContextTest passed");
    }
}
