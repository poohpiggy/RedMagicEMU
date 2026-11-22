"""Exercise the Guest's location demand lease and idle transitions."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = """
package app.aemu.stub;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public final class LocationDemandTest {
    public static void main(String[] args) throws Exception {
        Path path = Paths.get(args[0]);
        Files.write(path, "1 5000\\n".getBytes(StandardCharsets.US_ASCII));
        LocationDemand demand = new LocationDemand(path);
        demand.refresh(1000);
        expect(path, "0\\n");

        if (!demand.setRequested(true)) throw new AssertionError("missing start transition");
        demand.refresh(1000);
        expect(path, "1 5000\\n");
        if (demand.setRequested(true)) throw new AssertionError("duplicate start transition");
        demand.refresh(2000);
        expect(path, "1 6000\\n");

        if (!demand.setRequested(false)) throw new AssertionError("missing stop transition");
        demand.refresh(2001);
        expect(path, "0\\n");
        Files.delete(path);
        demand.refresh(3000);
        if (Files.exists(path)) throw new AssertionError("idle lease rewritten");
    }

    private static void expect(Path path, String expected) throws Exception {
        String actual = new String(Files.readAllBytes(path), StandardCharsets.US_ASCII);
        if (!actual.equals(expected)) throw new AssertionError("expected " + expected + " got " + actual);
    }
}
"""

with tempfile.TemporaryDirectory(prefix="rme-location-demand-") as temp:
    scratch = Path(temp)
    test = scratch / "LocationDemandTest.java"
    test.write_text(TEST, encoding="utf-8")
    subprocess.run(
        ["javac", "-Xlint:-options", "--release", "8", "-d", scratch,
         ROOT / "src/app/aemu/stub/LocationDemand.java", test],
        check=True,
    )
    subprocess.run(
        ["java", "-cp", scratch, "app.aemu.stub.LocationDemandTest", scratch / "demand"],
        check=True,
    )
print("Guest location lease transitions passed")
