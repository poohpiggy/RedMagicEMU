package app.aemu.stub;

import android.hardware.radio.network.CellInfo;
import android.hardware.radio.network.CellInfoLte;
import java.nio.charset.StandardCharsets;

public final class ServingLteObservationTest {
    private static ServingLteObservation parse(String record, long now) {
        return ServingLteObservation.parse(record.getBytes(StandardCharsets.US_ASCII), now);
    }

    public static void main(String[] args) {
        String fixture = "1 20000 310 026 100 200 300 765 -94";
        ServingLteObservation observation = parse(fixture, 23000);
        if (observation == null || !observation.fresh(50000) || observation.fresh(50001))
            throw new AssertionError("guest must use the host observation timestamp");
        if (parse(fixture, 19999) != null || parse(fixture, 50001) != null ||
                parse(fixture.replace("100", "268435456"), 23000) != null ||
                parse(fixture.replace("026", "x26"), 23000) != null ||
                parse(fixture + " extra", 23000) != null)
            throw new AssertionError("invalid LTE observations reached Guest Radio");
        CellInfo output = observation.toCellInfo();
        CellInfoLte lte = output.ratSpecificInfo.getLte();
        if (!output.registered || !"310".equals(lte.cellIdentityLte.mcc)
                || !"026".equals(lte.cellIdentityLte.mnc) || lte.cellIdentityLte.ci != 100
                || lte.cellIdentityLte.tac != 200 || lte.signalStrengthLte.rsrp != 94)
            throw new AssertionError("LTE observation fields changed across the radio boundary");
        System.out.println("ServingLteObservationTest passed");
    }
}
