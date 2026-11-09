package app.rme.probe;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.WindowInsets;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Offline diagnostic only; the results still need visual and touch verification. */
public final class WebViewActivity extends Activity {
    private static final String TAG = "RmeWebViewProbe";
    private static final String HTML = "<!doctype html><html><head>"
            + "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            + "<style>body{font:20px sans-serif;padding:16px}button{font:20px sans-serif;padding:16px}</style>"
            + "</head><body><h2>STATIC CONTENT OK</h2><p id='js'>JAVASCRIPT WAITING</p>"
            + "<button id='button' onclick=\"document.getElementById('click').textContent='BUTTON INTERACTION OK';document.title='BUTTON_OK'\">Tap here</button>"
            + "<p id='click'>BUTTON WAITING</p><script>"
            + "document.getElementById('js').textContent='JAVASCRIPT OK';document.title='JS_OK';"
            + "</script></body></html>";

    private TextView status;
    private TextView bottomStatus;
    private boolean javascriptOk;
    private boolean buttonOk;
    private boolean failed;

    private void report(String value) {
        status.setText(value);
        bottomStatus.setText("NATIVE CONTROL: " + value);
        Log.i(TAG, value);
    }

    private void logBounds(String name, View view) {
        int[] location = new int[2];
        view.getLocationOnScreen(location);
        Rect visible = new Rect();
        boolean isVisible = view.getGlobalVisibleRect(visible);
        Log.i(TAG, name + " screen=" + location[0] + "," + location[1]
                + " size=" + view.getWidth() + "x" + view.getHeight()
                + " visible=" + isVisible + " rect=" + visible.toShortString());
    }

    private void logLayout(LinearLayout layout, View content, String name) {
        layout.postDelayed(() -> {
            logBounds("root", layout);
            Log.i(TAG, "root-padding=" + layout.getPaddingLeft() + "," + layout.getPaddingTop()
                    + "," + layout.getPaddingRight() + "," + layout.getPaddingBottom());
            logBounds("top-status", status);
            logBounds(name, content);
            logBounds("bottom-status", bottomStatus);
            WindowInsets insets = layout.getRootWindowInsets();
            if (insets != null) {
                Log.i(TAG, "system-window-insets=" + insets.getSystemWindowInsetLeft()
                        + "," + insets.getSystemWindowInsetTop() + ","
                        + insets.getSystemWindowInsetRight() + "," + insets.getSystemWindowInsetBottom());
            }
        }, 2000);
    }

    private void fail(String reason) {
        failed = true;
        report("FAIL: " + reason);
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        if (!getIntent().getBooleanExtra("legacy_layout", false)) {
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
        }
        status = new TextView(this);
        status.setTextSize(18);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(Color.rgb(0, 70, 130));
        layout.addView(status);
        bottomStatus = new TextView(this);
        bottomStatus.setTextSize(18);
        bottomStatus.setTextColor(Color.WHITE);
        bottomStatus.setBackgroundColor(Color.rgb(100, 30, 90));
        setContentView(layout);
        layout.requestApplyInsets();
        report("WAITING: WebView startup");
        if (getIntent().getBooleanExtra("native_only", false)) {
            LinearLayout content = new LinearLayout(this);
            content.setOrientation(LinearLayout.VERTICAL);
            int padding = (int) (16 * getResources().getDisplayMetrics().density);
            content.setPadding(padding, padding, padding, padding);
            TextView heading = new TextView(this);
            heading.setText("NATIVE CONTENT OK");
            heading.setTextSize(24);
            content.addView(heading);
            Button button = new Button(this);
            button.setText("Native tap here");
            button.setOnClickListener(v -> report("NATIVE BUTTON OK"));
            content.addView(button);
            layout.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
            layout.addView(bottomStatus);
            report("NATIVE CONTROL: tap the native button");
            logLayout(layout, content, "native-content");
            return;
        }
        try {
            WebView web = new WebView(this);
            layout.addView(web, new LinearLayout.LayoutParams(-1, 0, 1));
            layout.addView(bottomStatus);
            logLayout(layout, web, "webview");
            web.getSettings().setJavaScriptEnabled(true);
            web.getSettings().setBlockNetworkLoads(true);
            web.getSettings().setAllowFileAccess(false);
            web.getSettings().setAllowContentAccess(false);
            web.setWebViewClient(new WebViewClient() {
                @Override public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                    if (request.isForMainFrame()) fail("page load error " + error.getErrorCode());
                }
                @Override public boolean onRenderProcessGone(WebView view, RenderProcessGoneDetail detail) {
                    fail("WebView renderer exited");
                    return true;
                }
            });
            web.setWebChromeClient(new WebChromeClient() {
                @Override public void onReceivedTitle(WebView view, String title) {
                    if (failed) return;
                    if ("JS_OK".equals(title)) {
                        javascriptOk = true;
                        report("JS OK: tap the page button");
                    } else if ("BUTTON_OK".equals(title) && javascriptOk) {
                        buttonOk = true;
                        report("JS + BUTTON OK: check page text visually");
                    }
                }
            });
            web.loadDataWithBaseURL(null, HTML, "text/html", "UTF-8", null);
            status.postDelayed(() -> {
                if (!javascriptOk && !failed) fail("JavaScript did not finish within 30 seconds");
                else if (!buttonOk && !failed) report("JS OK: waiting for you to tap the page button");
            }, 30000);
        } catch (RuntimeException | LinkageError e) {
            Log.e(TAG, "WebView initialization failed", e);
            fail("WebView initialization " + e.getClass().getSimpleName());
        }
    }
}
