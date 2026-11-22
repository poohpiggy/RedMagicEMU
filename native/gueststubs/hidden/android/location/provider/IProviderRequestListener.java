package android.location.provider;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;

/** Compile-time signature. Android 16 supplies the AIDL implementation at runtime. */
public interface IProviderRequestListener extends IInterface {
    void onProviderRequestChanged(String provider, ProviderRequest request);

    abstract class Stub extends Binder implements IProviderRequestListener {
        @Override
        public IBinder asBinder() {
            return this;
        }
    }
}
