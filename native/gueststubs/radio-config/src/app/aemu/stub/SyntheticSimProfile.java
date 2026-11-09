package app.aemu.stub;

/** A fixed test identity. It is not read from, or transmitted to, the host cellular stack. */
final class SyntheticSimProfile {
    static final String TEST_ICCID = "89000000000000000004";
    static final String TEST_ATR = "3B00";
    // 46000 is used only as the user's selected synthetic identifier; it is not
    // a claim that RedMagicEMU is a licensed operator or a real network.
    private static final String TEST_IMSI = "460000123456789";
    private static final String TEST_OPERATOR = "46000";
    private static final String TEST_OPERATOR_NAME = "RedMagicEMU";
    private static final String TEST_AID = "A00000008710020001";

    String iccid() { return TEST_ICCID; }
    String atr() { return TEST_ATR; }
    String imsi() { return TEST_IMSI; }
    String aid() { return TEST_AID; }
    String operatorNumeric() { return TEST_OPERATOR; }
    String operatorName() { return TEST_OPERATOR_NAME; }
    int mncDigits() { return 2; }

    /** ICCID decimal digits use low nibble first in EF_ICCID. */
    byte[] efIccid() { return new byte[]{(byte) 0x98,0,0,0,0,0,0,0,0,0x40}; }

    /** Only the MNC digit count is represented; this does not implement the SIM file protocol. */
    byte[] efAdministrativeData() { return new byte[]{0,0,0,2}; }

    /** USIM service table: only service 19 (SPN) is supplied by this prototype. */
    byte[] efUsimServiceTable() { return new byte[]{0,0,4,0}; }

    /** EF_SPN: display condition followed by a GSM default-alphabet name and FF padding. */
    byte[] efServiceProviderName() {
        byte[] value = new byte[17];
        java.util.Arrays.fill(value, 1, value.length, (byte) 0xff);
        byte[] name = TEST_OPERATOR_NAME.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        System.arraycopy(name, 0, value, 1, name.length);
        return value;
    }
}
