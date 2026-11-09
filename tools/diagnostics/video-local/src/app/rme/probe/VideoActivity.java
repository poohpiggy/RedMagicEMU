package app.rme.probe;

import android.app.Activity;
import android.content.res.AssetFileDescriptor;
import android.graphics.Insets;
import android.media.MediaPlayer;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Offline test: view the moving picture and listen for the tone. */
public final class VideoActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "RmeVideoProbe";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private SurfaceView surface;
    private MediaPlayer player;
    private boolean prepared;
    private boolean failed;
    private int generation;

    private void report(String text) { status.setText(text); Log.i(TAG, text); }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            } else {
                view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        status = new TextView(this);
        status.setTextSize(20);
        layout.addView(status);
        surface = new SurfaceView(this);
        layout.addView(surface, new LinearLayout.LayoutParams(-1, 0, 1));
        Button replay = new Button(this);
        replay.setText("REPLAY");
        replay.setOnClickListener(v -> startPlayback());
        layout.addView(replay);
        setContentView(layout);
        layout.requestApplyInsets();
        surface.getHolder().addCallback(this);
        report("WAITING FOR SURFACE");
    }

    private void releasePlayer() {
        ++generation;
        prepared = false;
        if (player != null) { player.release(); player = null; }
    }

    private void startPlayback() {
        releasePlayer();
        failed = false;
        final int current = generation;
        if (!surface.getHolder().getSurface().isValid()) { report("FAIL: surface unavailable"); return; }
        try {
            player = new MediaPlayer();
            player.setDisplay(surface.getHolder());
            player.setOnErrorListener((mp, what, extra) -> {
                failed = true;
                report("FAIL: media error what=" + what + " extra=" + extra);
                return true;
            });
            player.setOnPreparedListener(mp -> {
                if (current != generation) return;
                prepared = true;
                report("PREPARED duration=" + mp.getDuration() + "ms video=" + mp.getVideoWidth() + "x" + mp.getVideoHeight());
                mp.start();
                tick(current);
            });
            player.setOnCompletionListener(mp -> {
                if (current == generation && !failed) report("COMPLETED duration=" + mp.getDuration() + "ms");
            });
            try (AssetFileDescriptor asset = getAssets().openFd("probe.mp4")) {
                player.setDataSource(asset.getFileDescriptor(), asset.getStartOffset(), asset.getLength());
            }
            report("PREPARING");
            player.prepareAsync();
            handler.postDelayed(() -> {
                if (current == generation && !prepared && !failed) { failed = true; report("FAIL: prepare timeout (30s)"); }
            }, 30000);
        } catch (Exception e) {
            failed = true;
            report("FAIL: " + e.getClass().getSimpleName() + ": " + e.getMessage());
        }
    }

    private void tick(int current) {
        if (current != generation || failed || player == null) return;
        if (player.isPlaying()) {
            report("PLAYING " + player.getCurrentPosition() + " / " + player.getDuration() + "ms");
            handler.postDelayed(() -> tick(current), 1000);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder holder) { startPlayback(); }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { }
    @Override public void surfaceDestroyed(SurfaceHolder holder) { releasePlayer(); }
    @Override protected void onDestroy() { releasePlayer(); super.onDestroy(); }
}
