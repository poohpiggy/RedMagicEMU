package app.aemu.stub;

import java.lang.reflect.Method;

/** Isolated Guest decoder check; does not register a Radio service or load SIM records. */
public final class RadioSimSpnGuestTest {
    public static void main(String[] args) throws Exception {
        byte[] spn = SyntheticSingleSlot.forTests().simProfile().efServiceProviderName();
        if (spn.length != 17 || spn[0] != 0) throw new AssertionError("invalid EF_SPN framing");
        Class<?> iccUtils = Class.forName("com.android.internal.telephony.uicc.IccUtils");
        Method decoder = iccUtils.getMethod("adnStringFieldToString", byte[].class, int.class, int.class);
        Object decoded = decoder.invoke(null, spn, 1, spn.length - 1);
        if (!"RedMagicEMU".equals(decoded)) throw new AssertionError("EF_SPN decoder mismatch");
        System.out.println("RADIO_SIM_SPN_GUEST_OK");
    }
}
