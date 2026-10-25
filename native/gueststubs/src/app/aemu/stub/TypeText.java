package app.aemu.stub;

import android.content.ClipData;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Base64;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import java.lang.reflect.Method;

/**
 * Types any text into the focused field: TypeText <base64 of UTF-8 text>.
 *
 * "input text" builds key presses from the virtual keyboard map (KeyCharacterMap.getEvents), which returns null for
 * characters the map cannot type - Cyrillic, CJK, emoji - and the tool dies on the null array. A key event of
 * ACTION_MULTIPLE is refused by the input dispatcher ("Key event has invalid action code", injectInputEvent false).
 * So the text goes into the guest's clipboard (IClipboard.setPrimaryClip) and Ctrl+V is pressed: TextView pastes
 * on that shortcut (onKeyShortcut, 3.0+).
 */
public final class TypeText {
    public static void main(String[] args) throws Throwable {
        String text = new String(Base64.decode(args[0], Base64.DEFAULT), "UTF-8");
        setClip(text);
        long t = SystemClock.uptimeMillis();
        int ctrl = KeyEvent.META_CTRL_ON | KeyEvent.META_CTRL_LEFT_ON;
        inject(new KeyEvent(t, t, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_CTRL_LEFT, 0, ctrl, KeyCharacterMap.VIRTUAL_KEYBOARD, 0));
        inject(new KeyEvent(t, t, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_V, 0, ctrl, KeyCharacterMap.VIRTUAL_KEYBOARD, 0));
        inject(new KeyEvent(t, t, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_V, 0, ctrl, KeyCharacterMap.VIRTUAL_KEYBOARD, 0));
        inject(new KeyEvent(t, t, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_CTRL_LEFT, 0, 0, KeyCharacterMap.VIRTUAL_KEYBOARD, 0));
        System.exit(0);
    }

    /**
     * IClipboard.setPrimaryClip changes its arguments between releases: (ClipData) on 3.0-4.2, + callingPackage on
     * 4.3, + userId on 10, + more on 13+. The arguments are filled by type: the clip, the shell's package, 0.
     */
    private static void setClip(String text) throws Throwable {
        Class<?> sm = Class.forName("android.os.ServiceManager");
        IBinder b = (IBinder) sm.getMethod("getService", String.class).invoke(null, "clipboard");
        Object cb = Class.forName("android.content.IClipboard$Stub").getMethod("asInterface", IBinder.class).invoke(null, b);
        ClipData clip = ClipData.newPlainText("aemu", text);
        for (Method m : cb.getClass().getMethods()) {
            if (!m.getName().equals("setPrimaryClip")) continue;
            Class<?>[] p = m.getParameterTypes();
            Object[] a = new Object[p.length];
            for (int i = 0; i < p.length; i++)
                a[i] = p[i] == ClipData.class ? clip : p[i] == String.class ? "com.android.shell" : p[i] == int.class ? (Object) 0 : null;
            m.invoke(cb, a);
            return;
        }
        throw new IllegalStateException("no IClipboard.setPrimaryClip");
    }

    /** InputManager.injectInputEvent (hidden, 4.1+), mode 2 = wait until the window has handled it */
    private static void inject(KeyEvent e) throws Throwable {
        Class<?> im = Class.forName("android.hardware.input.InputManager");
        Object m = im.getMethod("getInstance").invoke(null);
        Object ok = im.getMethod("injectInputEvent", android.view.InputEvent.class, int.class).invoke(m, e, 2);
        if (!Boolean.TRUE.equals(ok)) System.err.println("not injected: " + e);
    }
}
