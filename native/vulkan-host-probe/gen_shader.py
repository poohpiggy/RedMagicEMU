"""Generate the embedded shader header from validated SPIR-V."""
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
if len(data) < 20 or len(data) % 4:
    raise ValueError("invalid SPIR-V size")
words = struct.unpack(f"<{len(data) // 4}I", data)
if words[0] != 0x07230203:
    raise ValueError("invalid SPIR-V magic")
Path(sys.argv[2]).write_text(
    "#include <stdint.h>\nstatic const uint32_t shader_spv[] = {\n" +
    ",".join(f"0x{x:08x}u" for x in words) + "\n};\n", encoding="ascii")
