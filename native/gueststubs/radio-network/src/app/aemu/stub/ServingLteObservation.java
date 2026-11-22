package app.aemu.stub;

import android.hardware.radio.RadioConst;
import android.hardware.radio.network.*;
import java.nio.charset.StandardCharsets;

/** One host modem observation admitted through the authenticated Radio connection. */
final class ServingLteObservation {
    final long observedMs;
    private final String mcc, mnc;
    private final int ci, tac, pci, earfcn, rsrp;

    private ServingLteObservation(long observedMs, String mcc, String mnc, int ci, int tac,
            int pci, int earfcn, int rsrp) {
        this.observedMs=observedMs;
        this.mcc=mcc; this.mnc=mnc; this.ci=ci; this.tac=tac;
        this.pci=pci; this.earfcn=earfcn; this.rsrp=rsrp;
    }

    boolean fresh(long now) {
        return observedMs>0 && observedMs<=now && now-observedMs<=30000;
    }

    static ServingLteObservation parse(byte[] bytes, long now) {
        if (bytes==null || bytes.length>96) return null;
        String[] part=new String(bytes,StandardCharsets.US_ASCII).split(" ",-1);
        if (part.length!=9 || !part[0].equals("1") ||
                !part[2].matches("[0-9]{3}") || !part[3].matches("[0-9]{2,3}")) return null;
        try {
            ServingLteObservation cell=new ServingLteObservation(Long.parseLong(part[1]),part[2],part[3],
                    Integer.parseInt(part[4]),Integer.parseInt(part[5]),Integer.parseInt(part[6]),
                    Integer.parseInt(part[7]),Integer.parseInt(part[8]));
            return cell.fresh(now) && cell.ci>=0 && cell.ci<=268435455 && cell.tac>=0 && cell.tac<=65535 &&
                    cell.pci>=0 && cell.pci<=503 && cell.earfcn>=0 && cell.earfcn<=262143 &&
                    ((cell.rsrp>=-140 && cell.rsrp<=-44) || cell.rsrp==RadioConst.VALUE_UNAVAILABLE) ? cell : null;
        } catch (NumberFormatException malformed) {
            return null;
        }
    }

    CellInfo toCellInfo() {
        int unknown=RadioConst.VALUE_UNAVAILABLE;
        CellIdentityLte identity=new CellIdentityLte();
        identity.mcc=mcc; identity.mnc=mnc; identity.ci=ci;
        identity.tac=tac; identity.pci=pci; identity.earfcn=earfcn;
        identity.bandwidth=unknown; identity.additionalPlmns=new String[0]; identity.bands=new int[0];
        identity.operatorNames=new OperatorInfo();
        identity.operatorNames.alphaLong=""; identity.operatorNames.alphaShort="";
        identity.operatorNames.operatorNumeric=mcc+mnc;
        identity.operatorNames.status=OperatorInfo.STATUS_CURRENT;

        LteSignalStrength signal=new LteSignalStrength();
        signal.rsrp=rsrp==unknown ? unknown : -rsrp;
        signal.signalStrength=unknown; signal.rsrq=unknown; signal.rssnr=unknown;
        signal.cqi=unknown; signal.timingAdvance=unknown; signal.cqiTableIndex=unknown;

        CellInfoLte lte=new CellInfoLte();
        lte.cellIdentityLte=identity; lte.signalStrengthLte=signal;
        CellInfo cell=new CellInfo();
        cell.registered=true;
        cell.connectionStatus=CellConnectionStatus.PRIMARY_SERVING;
        cell.ratSpecificInfo=CellInfoRatSpecificInfo.lte(lte);
        return cell;
    }
}
