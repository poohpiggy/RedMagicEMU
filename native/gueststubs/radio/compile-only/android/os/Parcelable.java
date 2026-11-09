package android.os;

/** Compile-only signature shim; never include in a Guest DEX or APK. */
public interface Parcelable {
    int CONTENTS_FILE_DESCRIPTOR = 1;
    int PARCELABLE_WRITE_RETURN_VALUE = 1;
    int PARCELABLE_STABILITY_VINTF = 1;
    int describeContents();
    void writeToParcel(Parcel dest, int flags);
    default int getStability() { return 0; }
    interface Creator<T> { T createFromParcel(Parcel source); T[] newArray(int size); }
    interface ClassLoaderCreator<T> extends Creator<T> { T createFromParcel(Parcel source, ClassLoader loader); }
}
