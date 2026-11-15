package app.aemu.stub;

import android.system.ErrnoException;
import android.system.Os;

/** Runs only in an isolated guest process; it does not register a service or access user data. */
public final class GuestSetUidProbe {
    public static void main(String[] args) throws Exception {
        int initial = Os.geteuid();
        Os.setuid(12345);
        int ordinary = Os.geteuid();
        boolean restored;
        try { Os.setuid(0); restored = Os.geteuid() == 0; }
        catch (ErrnoException denied) { restored = false; }
        System.out.println("GUEST_UID_PROBE initial=" + initial + " dropped=" + ordinary
                + " unprivileged_restored_root=" + restored);
    }
}
