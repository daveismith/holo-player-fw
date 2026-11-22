#!/usr/bin/env python3
"""Raw CAN frames from a computer, through a gs_usb adapter (candleLight, CANable), for testing the
board's `can` command and the wiring under it (manual/use/cyphal.md, "Testing the wiring").

The tool is esp-console-kit's, shared with the dome panel's firmware:
external/esp-console-kit/tools/can_check.py, which has the usage and the adapter's rules
(`listen`, `send`, `echo`, `flood`, `flush`). This file only loads it, so that the command line,
`import can_check` (tests/hardware/conftest.py, tools/yakut_can.py) and `tools/can_check.py flush`
all work from here as before.
"""

import importlib.util
import sys
from pathlib import Path

_KIT = Path(__file__).resolve().parent.parent / "external" / "esp-console-kit" / "tools" / "can_check.py"
_spec = importlib.util.spec_from_file_location("_kit_can_check", _KIT)
_kit = importlib.util.module_from_spec(_spec)
sys.modules["_kit_can_check"] = _kit        # a Thread subclass or dataclass in it looks its module up
_spec.loader.exec_module(_kit)
globals().update({name: value for name, value in vars(_kit).items() if not name.startswith("__")})

if __name__ == "__main__":
    sys.exit(_kit.main())
