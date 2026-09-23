from __future__ import annotations

import sys

from .backend import (
    BackendError,
    JsonCliBackend,
    command_prefix_from_env,
    timeout_from_env,
)
from .server import build_server


def main() -> None:
    try:
        backend = JsonCliBackend(
            command_prefix=command_prefix_from_env(),
            timeout_seconds=timeout_from_env(),
        )
    except BackendError as exc:
        print(f"colosseum-agent-bridge: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc

    build_server(backend).run("stdio")


if __name__ == "__main__":
    main()
