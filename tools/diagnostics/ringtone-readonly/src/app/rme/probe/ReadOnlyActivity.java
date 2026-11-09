package app.rme.probe;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;
import android.provider.Settings;
import android.widget.TextView;

/** Isolated read-only diagnostic. No settings write or permission request. */
public final class ReadOnlyActivity extends Activity {
  @Override protected void onCreate(Bundle state) {
    super.onCreate(state);
    StringBuilder s = new StringBuilder();
    s.append("target=").append(getApplicationInfo().targetSdkVersion).append('\n');
    s.append("user=").append((Process.myUid() / 100000)).append('\n');
    s.append("write_permission=").append(checkSelfPermission(android.Manifest.permission.WRITE_SETTINGS) == PackageManager.PERMISSION_GRANTED).append('\n');
    try { s.append("can_write=").append(Settings.System.canWrite(this)).append('\n'); }
    catch (Exception e) { s.append("can_write_error=").append(e.getClass().getSimpleName()).append('\n'); }
    check(s, "ringtone", RingtoneManager.TYPE_RINGTONE);
    check(s, "notification", RingtoneManager.TYPE_NOTIFICATION);
    check(s, "alarm", RingtoneManager.TYPE_ALARM);
    TextView result = new TextView(this);
    result.setText(s.toString()); result.setTextSize(18);
    result.setGravity(android.view.Gravity.CENTER_VERTICAL);
    setContentView(result);
  }
  private void check(StringBuilder s, String label, int type) {
    try {
      Uri uri = RingtoneManager.getActualDefaultRingtoneUri(this, type);
      s.append(label).append("_uri_present=").append(uri != null).append('\n');
      if (uri != null) {
        String mime = getContentResolver().getType(uri);
        s.append(label).append("_mime_audio=").append(mime != null && mime.startsWith("audio/")).append('\n');
      }
    } catch (Exception e) { s.append(label).append("_error=").append(e.getClass().getSimpleName()).append('\n'); }
  }
}
