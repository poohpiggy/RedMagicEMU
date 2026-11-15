package android.net.wifi;
public interface IWifiManager {
    abstract class Stub {
        public static final int TRANSACTION_getWifiEnabledState = 31;
        public static final int TRANSACTION_setWifiEnabled = 32;
        public static final int TRANSACTION_getCurrentNetwork = 33;
    }
}
