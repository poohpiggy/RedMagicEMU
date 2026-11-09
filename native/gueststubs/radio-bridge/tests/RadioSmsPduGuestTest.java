package app.aemu.stub;

import android.telephony.SmsMessage;

/** Fixed synthetic fixture only; not proof that the radio or messaging app received an SMS. */
public final class RadioSmsPduGuestTest {
    public static void main(String[] args) {
        byte[] pdu={0,4,5,(byte)0x91,1,(byte)0x80,(byte)0xf6,0,8,0x62,0x11,0x70,0x21,0,0,0,2,0,0x41};
        SmsMessage message=SmsMessage.createFromPdu(pdu,"3gpp");
        if (message==null) throw new AssertionError("synthetic PDU parser returned null");
        if (!"+10086".equals(message.getOriginatingAddress()))
            throw new AssertionError("synthetic origin mismatch: " + message.getOriginatingAddress());
        if (!"A".equals(message.getMessageBody()))
            throw new AssertionError("synthetic SMS-DELIVER fixture did not decode as expected");
        byte[] unicode={0,4,5,(byte)0x91,1,(byte)0x80,(byte)0xf6,0,8,0x62,0x11,0x70,0x21,0,0,0,6,
                0x4f,0x60,(byte)0xd8,0x3d,(byte)0xde,0};
        SmsMessage unicodeMessage=SmsMessage.createFromPdu(unicode,"3gpp");
        if (unicodeMessage==null || !"\u4f60\ud83d\ude00".equals(unicodeMessage.getMessageBody()))
            throw new AssertionError("synthetic Unicode fixture did not decode as expected");
        System.out.println("RADIO_SMS_PDU_GUEST_OK");
    }
}
