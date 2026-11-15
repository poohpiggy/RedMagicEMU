package android.os;
public class Binder implements IBinder {
    public static int getCallingUid() { return 12345; }
    public static int getCallingPid() { return 222; }
    public static long clearCallingIdentity() { return 1; }
    public static void restoreCallingIdentity(long token) { }
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException { return false; }
}
