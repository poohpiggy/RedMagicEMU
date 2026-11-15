package android.content;
import java.util.HashMap;
import java.util.Map;
import android.net.NetworkInfo;
import android.net.wifi.SupplicantState;
public class Intent {
    public final String action;
    public final Map<String, Object> extras = new HashMap<String, Object>();
    public Intent(String action) { this.action = action; }
    public Intent putExtra(String key, boolean value) { extras.put(key,value); return this; }
    public Intent putExtra(String key, int value) { extras.put(key, value); return this; }
    public Intent putExtra(String key, NetworkInfo value) { extras.put(key, value); return this; }
    public Intent putExtra(String key, android.os.Parcelable value) { extras.put(key, value); return this; }
}
