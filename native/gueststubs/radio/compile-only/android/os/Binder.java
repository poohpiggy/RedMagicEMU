package android.os;

import java.io.FileDescriptor;

/** Compile-only signature shim; never include in a Guest DEX or APK. */
public class Binder implements IBinder {
    public Binder() {}
    public void markVintfStability() {}
    public void attachInterface(IInterface owner, String descriptor) {}
    public IInterface queryLocalInterface(String descriptor) { return null; }
    public String getInterfaceDescriptor() { return null; }
    public boolean pingBinder() { return false; }
    public boolean isBinderAlive() { return false; }
    public void dump(FileDescriptor fd, String[] args) {}
    public void dumpAsync(FileDescriptor fd, String[] args) {}
    public boolean transact(int code, Parcel data, Parcel reply, int flags) throws RemoteException { return false; }
    public void linkToDeath(IBinder.DeathRecipient recipient, int flags) {}
    public boolean unlinkToDeath(IBinder.DeathRecipient recipient, int flags) { return false; }
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException { return false; }
}
