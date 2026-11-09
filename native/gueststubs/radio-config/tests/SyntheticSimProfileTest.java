package app.aemu.stub;

import java.util.Arrays;

/** Synthetic identity and records only; not evidence of a loaded or ready SIM. */
public final class SyntheticSimProfileTest {
    private static void check(boolean ok, String reason) { if (!ok) throw new AssertionError(reason); }
    public static void main(String[] args) {
        SyntheticSingleSlot slot=SyntheticSingleSlot.forTests();
        SyntheticSimProfile profile=slot.simProfile();
        check(profile.iccid().equals(SyntheticSingleSlot.TEST_ICCID), "config and profile ICCID differ");
        check("A00000008710020001".equals(profile.aid()), "wrong synthetic USIM AID");
        check(profile.atr().equals(SyntheticSingleSlot.TEST_ATR), "config and profile ATR differ");
        check(profile.imsi().equals("460000123456789"), "unexpected synthetic IMSI");
        check(profile.operatorNumeric().equals("46000") && profile.imsi().startsWith(profile.operatorNumeric()), "IMSI and PLMN disagree");
        check(profile.operatorName().equals("RedMagicEMU"), "unexpected synthetic operator name");
        check(profile.mncDigits()==2, "MNC length differs from synthetic PLMN");
        byte[] iccid=profile.efIccid();
        check(Arrays.equals(iccid,new byte[]{(byte)0x98,0,0,0,0,0,0,0,0,0x40}),"incorrect EF_ICCID BCD");
        iccid[0]=0;
        check(profile.efIccid()[0]==(byte)0x98, "EF_ICCID exposed mutable state");
        byte[] ad=profile.efAdministrativeData();
        check(Arrays.equals(ad,new byte[]{0,0,0,2}),"EF_AD does not encode two-digit MNC");
        ad[3]=3;
        check(profile.efAdministrativeData()[3]==2,"EF_AD exposed mutable state");
        byte[] ust=profile.efUsimServiceTable();
        check(Arrays.equals(ust,new byte[]{0,0,4,0}),"UST must advertise only SPN");
        ust[2]=0;
        check(profile.efUsimServiceTable()[2]==4,"UST exposed mutable state");
        check(slot.isSimReady() && !slot.isNetworkRegistered() && !slot.canDeliverVoiceOrSms(),
                "connected USIM or powered-off network state incorrect");
        System.out.println("SYNTHETIC_SIM_PROFILE_OK");
    }
}
