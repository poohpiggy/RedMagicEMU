"""Build only the target VoldStub override; never rebuild the shared services JAR."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("--sdk", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
android = args.sdk / "platforms/android-36/android.jar"
d8 = args.sdk / "build-tools/36.0.0" / ("d8.bat" if __import__("os").name == "nt" else "d8")
with tempfile.TemporaryDirectory(prefix="target-vold-build-") as temp:
    root = Path(temp); classes = root / "classes"; dex = root / "dex"
    classes.mkdir(); dex.mkdir()
    subprocess.run(["javac", "-Xlint:-options", "--release", "8", "-cp", str(android), "-d", str(classes),
                    str(HERE / "src/app/aemu/stub/VoldStub.java")], check=True)
    compiled = list(classes.rglob("*.class"))
    if [p.relative_to(classes).as_posix() for p in compiled] != ["app/aemu/stub/VoldStub.class"]:
        raise RuntimeError("unexpected compiled classes")
    subprocess.run([str(d8), "--min-api", "17", "--lib", str(android), "--output", str(dex), *map(str, compiled)], check=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED) as jar:
        for name, data in [("META-INF/redmagic-vold-owner", b"RedMagicEMU vold v1\n"), ("classes.dex", (dex / "classes.dex").read_bytes())]:
            entry = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0)); entry.compress_type = zipfile.ZIP_DEFLATED
            jar.writestr(entry, data)
