#!/usr/bin/env python3
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
start = source.index('                if (', source.index('// Display surface creation failed.'))
end = source.index('\n            }\n        }\n    }\n\n    auto watchdog', start)
Path(sys.argv[2]).write_text(source[start:end])
