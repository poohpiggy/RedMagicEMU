/* Modified for AEmulator Super Plus, 2026-10-11: neighbouring networks and signal level. GPL-3.0; see LICENSE and NOTICE.md. */
/* Modified for RedMagicEMU, 2026-11-10: validate credentials without silently creating an open access point. */
package app.aemu.stub;

import java.io.File;
import java.io.FileInputStream;
import java.net.InetAddress;
import java.util.Properties;

/** The emulated network, as the host wrote it to /data/system/aemu-wifi.properties (see WifiEmu.kt). */
final class WifiConfig {
    static final String FILE = "/data/system/aemu-wifi.properties";

    final String ssid;
    final boolean secured;
    final String psk;
    final String bssid;
    final String mac;
    final String ip;
    final String gateway;
    final int prefix;
    final String dns1;
    final String dns2;
    final int rssi;
    final int speed;
    final int frequency;
    /** Other networks in range: listed by a scan, locked (their passphrase is unknown, so joining fails). */
    final java.util.List<Neighbor> neighbors = new java.util.ArrayList<Neighbor>();

    static final class Neighbor {
        final String ssid, bssid, caps;
        final int rssi, frequency;
        Neighbor(String ssid, String bssid, String caps, int rssi, int frequency) {
            this.ssid = ssid; this.bssid = bssid; this.caps = caps; this.rssi = rssi; this.frequency = frequency;
        }
    }

    private WifiConfig(Properties p) {
        ssid = p.getProperty("ssid", "RedMagicEMU");
        String securedValue = p.getProperty("secured", "false");
        if (!"false".equals(securedValue)) throw new IllegalArgumentException("only open Wi-Fi is supported");
        secured = false;
        psk = p.getProperty("psk", "");
        if (!psk.isEmpty()) throw new IllegalArgumentException("credential supplied for open network");
        bssid = p.getProperty("bssid", "02:00:00:00:00:01");
        mac = p.getProperty("mac", "02:15:5d:ae:00:01");
        ip = p.getProperty("ip", "10.0.2.15");
        gateway = p.getProperty("gateway", "10.0.2.2");
        prefix = num(p.getProperty("prefix"), 24);
        dns1 = p.getProperty("dns1", "8.8.8.8");
        dns2 = p.getProperty("dns2", "1.1.1.1");
        rssi = num(p.getProperty("rssi"), -48);
        speed = num(p.getProperty("speed"), 72);
        frequency = num(p.getProperty("frequency"), 2437);
        // "ssid|bssid|caps|rssi|frequency" entries separated by newlines (written by WifiEmu.kt)
        String list = p.getProperty("neighbors", "");
        for (String line : list.split("\n")) {
            String[] f = line.split("\\|", -1);
            if (f.length != 5 || f[0].length() == 0) continue;
            neighbors.add(new Neighbor(f[0], f[1], f[2], num(f[3], -80), num(f[4], 2412)));
        }
    }

    private static int num(String s, int d) {
        try { return Integer.parseInt(s.trim()); } catch (Throwable t) { return d; }
    }

    /** WifiConfiguration.SSID is quoted UTF-8 text or an unquoted hexadecimal byte sequence. */
    static boolean matchesSsid(String expected, String configured) {
        if (expected == null || configured == null) return false;
        int length = configured.length();
        if (length >= 2 && configured.charAt(0) == '"' && configured.charAt(length - 1) == '"')
            return expected.equals(configured.substring(1, length - 1));
        byte[] bytes = expected.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        if (length == 0 || length != bytes.length * 2) return false;
        for (int i = 0; i < bytes.length; i++) {
            int high = hexDigit(configured.charAt(i * 2));
            int low = hexDigit(configured.charAt(i * 2 + 1));
            if (high < 0 || low < 0 || ((high << 4) | low) != (bytes[i] & 0xff)) return false;
        }
        return true;
    }

    private static int hexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    static WifiConfig load() {
        return load(new File(FILE));
    }

    static WifiConfig load(File f) {
        if (!f.isFile()) return null;
        FileInputStream in = null;
        try {
            in = new FileInputStream(f);
            Properties p = new Properties();
            p.load(in);
            return new WifiConfig(p);
        } catch (Exception t) {
            Refl.log("cannot read " + f + ": " + t);
            return null;
        } finally {
            if (in != null) try { in.close(); } catch (Throwable ignored) { }
        }
    }

    /** IPv4 text as the int the framework uses in DhcpInfo and WifiInfo (first octet in the lowest byte). */
    static int ipInt(String s) {
        try {
            byte[] b = InetAddress.getByName(s).getAddress();
            return (b[0] & 0xff) | (b[1] & 0xff) << 8 | (b[2] & 0xff) << 16 | (b[3] & 0xff) << 24;
        } catch (Throwable t) {
            return 0;
        }
    }

    static int netmaskInt(int prefix) {
        int m = prefix <= 0 ? 0 : (-1 << (32 - Math.min(prefix, 32)));
        return Integer.reverseBytes(m);
    }
}
