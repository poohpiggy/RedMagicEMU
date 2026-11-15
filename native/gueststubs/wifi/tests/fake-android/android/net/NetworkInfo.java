package android.net;
public class NetworkInfo {
    public enum DetailedState { CONNECTED, DISCONNECTED }
    public DetailedState state;
    public String extraInfo;
    public boolean available;
    public NetworkInfo(int type, int subtype, String typeName, String subtypeName) { }
    public void setDetailedState(DetailedState state, String reason, String extraInfo) {
        this.state = state;
        this.extraInfo = extraInfo;
    }
    public void setIsAvailable(boolean value) { available = value; }
}
