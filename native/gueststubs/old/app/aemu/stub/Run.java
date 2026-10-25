package app.aemu.stub;

import java.lang.reflect.InvocationTargetException;

/**
 * Run for pre-1.0 builds (M3/M5, dalvik reads only dex 013; built with the dx of the M5 SDK, see build013.sh).
 * There even System.exit() does not end the tool: exit() waits on the binder thread. The process is killed
 * outright: android.os.Process.killProcess(myPid()) (both in the M5 API docs), Runtime.halt() if that fails.
 */
public final class Run {
    public static void main(String[] args) throws Throwable {
        if (args.length == 0) {
            System.err.println("usage: Run <main class> [args...]");
            end(2);
        }
        String[] rest = new String[args.length - 1];
        System.arraycopy(args, 1, rest, 0, rest.length);
        int code = 0;
        try {
            Class.forName(args[0]).getMethod("main", new Class[] { String[].class }).invoke(null, new Object[] { rest });
        } catch (InvocationTargetException e) {
            Throwable c = e.getCause() != null ? e.getCause() : e;
            c.printStackTrace();
            code = 1;
        }
        end(code);
    }

    private static void end(int code) {
        System.out.flush();
        System.err.flush();
        try {
            Class p = Class.forName("android.os.Process");
            Object pid = p.getMethod("myPid", new Class[0]).invoke(null, new Object[0]);
            p.getMethod("killProcess", new Class[] { int.class }).invoke(null, new Object[] { pid });
        } catch (Throwable t) {
            // fall through
        }
        Runtime.getRuntime().halt(code);
    }
}
