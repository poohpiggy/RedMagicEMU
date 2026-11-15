package android.os;
public class Parcel {
    private final String token;
    private String packageName;
    private boolean enabled;
    public String text;
    public int number;
    public boolean success;
    public boolean hasExtraData;
    public Object typedObject;
    public Parcel() { this(null); }
    public Parcel(String token) { this.token = token; }
    public Parcel(String token, String packageName, boolean enabled) {
        this.token = token;
        this.packageName = packageName;
        this.enabled = enabled;
    }
    public String readString() { return packageName; }
    public boolean readBoolean() { return enabled; }
    public void enforceInterface(String expected) { if (!expected.equals(token)) throw new SecurityException(); }
    public void enforceNoDataAvail() { if (hasExtraData) throw new IllegalArgumentException(); }
    public void writeString(String value) { text = value; }
    public void writeNoException() { success = true; }
    public void writeInt(int value) { number = value; }
    public void writeTypedObject(android.net.Network value, int flags) { typedObject = value; }
}
