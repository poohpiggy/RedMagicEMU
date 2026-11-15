package app.aemu.stub;

import java.io.File;
import java.io.FileOutputStream;
import java.util.Properties;

public final class WifiConfigTest {
    private static WifiConfig load(Properties p) throws Exception {
        File file = File.createTempFile("rme-wifi-test-", ".properties");
        try {
            try (FileOutputStream out = new FileOutputStream(file)) { p.store(out, null); }
            return WifiConfig.load(file);
        } finally { file.delete(); }
    }
    public static void main(String[] args) throws Exception {
        Properties p = new Properties();
        WifiConfig open = load(p);
        if (open == null || !"RedMagicEMU".equals(open.ssid) || open.secured) throw new AssertionError("default");
        p.setProperty("secured", "true");
        p.setProperty("psk", "correct-password");
        if (load(p) != null) throw new AssertionError("secured networks must be rejected");
        p.setProperty("secured", "false");
        if (load(p) != null) throw new AssertionError("password cannot be silently discarded");
        p.setProperty("psk", "");
        if (load(p) == null) throw new AssertionError("explicit open network");
        if (!WifiConfig.matchesSsid("RedMagicEMU", "\"RedMagicEMU\"")) throw new AssertionError("quoted UTF-8 SSID");
        if (!WifiConfig.matchesSsid("RedMagicEMU", "5265644d61676963454d55")) throw new AssertionError("hexadecimal SSID");
        if (!WifiConfig.matchesSsid("RedMagicEMU", "5265644D61676963454D55")) throw new AssertionError("uppercase hexadecimal SSID");
        if (!WifiConfig.matchesSsid("热点", "e783ade782b9")) throw new AssertionError("non-ASCII hexadecimal SSID");
        if (WifiConfig.matchesSsid("face", "face")) throw new AssertionError("unquoted text must be interpreted as hexadecimal");
        if (WifiConfig.matchesSsid("RedMagicEMU", "5265644d61676963454d5")) throw new AssertionError("odd hex");
        if (WifiConfig.matchesSsid("RedMagicEMU", "5265644d61676963454d5z")) throw new AssertionError("invalid hex");
        if (WifiConfig.matchesSsid("RedMagicEMU", null)) throw new AssertionError("missing SSID");
        System.out.println("WifiConfigTest passed");
    }
}
