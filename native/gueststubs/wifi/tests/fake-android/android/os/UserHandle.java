package android.os;
public final class UserHandle {
    private final int id;
    public UserHandle(int id) { this.id = id; }
    public static UserHandle getUserHandleForUid(int uid) { return new UserHandle(uid / 100000); }
    @Override public boolean equals(Object other) { return other instanceof UserHandle && ((UserHandle) other).id == id; }
    @Override public int hashCode() { return id; }
}
