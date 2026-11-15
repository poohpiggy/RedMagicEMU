package app.aemu.stub;

import android.os.Binder;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.system.Os;

/** Isolated cross-process identity check; uses a disposable non-wifi service name. */
public final class GuestBinderUidProbe {
    private static final String NAME = "redmagicemu.uid_probe.b62d8dab";
    private static Object manager(String method, Class<?>[] types, Object... args) throws Exception {
        return Class.forName("android.os.ServiceManager").getMethod(method, types).invoke(null, args);
    }
    private static IBinder find() throws Exception {
        return (IBinder) manager("checkService", new Class<?>[] { String.class }, NAME);
    }
    private static final class IdentityService extends Binder {
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
            if (code != IBinder.FIRST_CALL_TRANSACTION || reply == null) return false;
            reply.writeNoException();
            reply.writeInt(Binder.getCallingUid());
            return true;
        }
    }
    private static int query(IBinder binder) throws Exception {
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            if (!binder.transact(IBinder.FIRST_CALL_TRANSACTION, data, reply, 0)) throw new IllegalStateException("transaction unavailable");
            reply.readException();
            return reply.readInt();
        } finally { data.recycle(); reply.recycle(); }
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("server or client required");
        if ("server".equals(args[0])) {
            Looper.prepare();
            if (find() != null) throw new IllegalStateException("probe service already exists");
            manager("addService", new Class<?>[] { String.class, IBinder.class }, NAME, new IdentityService());
            System.out.println("GUEST_BINDER_UID_PROBE_READY");
            System.out.flush();
            Looper.loop();
        } else if ("client".equals(args[0])) {
            IBinder binder = null;
            for (int i = 0; i < 100 && binder == null; i++) { binder = find(); if (binder == null) Thread.sleep(100); }
            if (binder == null) throw new IllegalStateException("probe service unavailable");
            Os.setuid(12345);
            int ordinary = query(binder);
            Os.setuid(0);
            int elevated = query(binder);
            System.out.println("GUEST_BINDER_UID_PROBE ordinary=" + ordinary + " after_setuid_zero=" + elevated);
        } else throw new IllegalArgumentException("server or client required");
    }
}
