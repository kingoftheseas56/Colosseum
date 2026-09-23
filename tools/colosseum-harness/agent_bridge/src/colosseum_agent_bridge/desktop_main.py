from __future__ import annotations

import sys

from .cursortouch import CursorTouchError
from .desktop_control import ColosseumDesktopController, DesktopControlError
from .desktop_lease import DesktopLeaseError
from .desktop_server import build_desktop_server


def main() -> None:
    try:
        controller = ColosseumDesktopController()
    except (DesktopControlError, DesktopLeaseError, CursorTouchError) as exc:
        print(f"colosseum-desktop-bridge: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc

    build_desktop_server(controller).run("stdio")


if __name__ == "__main__":
    main()
