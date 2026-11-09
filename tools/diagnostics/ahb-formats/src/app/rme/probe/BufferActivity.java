package app.rme.probe;
import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.graphics.Insets;
import android.view.WindowInsets;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.util.Log;
public final class BufferActivity extends Activity {
  private static final String TAG="RmeAHBProbe";
  private TextView status;
  private static native String probe(int mode);
  private static String name(int n) { switch(n) {case 0:return "YUV420_CPU_RW";case 1:return "YUV420_CPU_RW_GPU";case 2:return "YV12_CPU_RW";case 3:return "YV12_CPU_RW_GPU";default:return "INVALID";} }
  private void report(String s) {Log.i(TAG,s);runOnUiThread(()->status.setText(s));}
  @Override public void onCreate(Bundle state) {
    super.onCreate(state);LinearLayout layout=new LinearLayout(this);
    layout.setOnApplyWindowInsetsListener((view,insets)->{if(Build.VERSION.SDK_INT>=30){Insets i=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());view.setPadding(i.left,i.top,i.right,i.bottom);}else view.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());return insets;});
    status=new TextView(this);status.setTextSize(20);layout.addView(status);setContentView(layout);layout.requestApplyInsets();report("STARTING AHB FORMAT PROBE");
    final int selected=getIntent().getIntExtra("case",-1);
    new Thread(()->{try{System.loadLibrary("rme_ahbprobe"); if(selected < -1 || selected > 3){report("FAIL invalid case");return;}StringBuilder all=new StringBuilder();for(int i=0;i<4;i++){if(selected>=0 && selected!=i)continue;report(name(i)+" BEGIN");String result=probe(i);all.append(name(i)).append(": ").append(result).append("\n");report(name(i)+" DONE "+result);}report("FINISHED\n"+all);}catch(Throwable e){report("FAIL "+e.getClass().getSimpleName());Log.e(TAG,"probe failure");}},"rme-ahb-probe").start();
  }
}
