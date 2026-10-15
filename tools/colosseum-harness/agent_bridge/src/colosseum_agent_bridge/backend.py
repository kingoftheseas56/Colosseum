from __future__ import annotations

import asyncio
import json
import os
from dataclasses import dataclass
from typing import Any, Mapping, Protocol, Sequence

JsonObject = dict[str, Any]

ALLOWED_OPERATIONS = frozenset(
    {"status", "inspect", "context-for-task", "test", "journeys", "journey", "verify"}
)


class BackendError(RuntimeError):
    """Expected harness-backend failure that is safe to surface to an MCP caller."""


class HarnessBackend(Protocol):
    async def call(
        self, operation: str, arguments: Sequence[str] = ()
    ) -> JsonObject:
        """Invoke one Colosseum semantic operation and return its JSON object."""


@dataclass(frozen=True, slots=True)
class JsonCliBackend:
    """Invoke a fixed Colosseum harness CLI prefix without a shell."""

    command_prefix: tuple[str, ...]
    timeout_seconds: float = 120.0

    async def call(
        self, operation: str, arguments: Sequence[str] = ()
    ) -> JsonObject:
        if operation not in ALLOWED_OPERATIONS:
            raise BackendError(f"unsupported Colosseum operation: {operation}")
        if not self.command_prefix:
            raise BackendError("Colosseum harness command is not configured")
        if self.timeout_seconds <= 0:
            raise BackendError("backend timeout must be greater than zero")

        argv = (*self.command_prefix, operation, *arguments, "--json")
        process = await asyncio.create_subprocess_exec(
            *argv,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        try:
            stdout, stderr = await asyncio.wait_for(
                process.communicate(), timeout=self.timeout_seconds
            )
        except TimeoutError as exc:
            process.kill()
            await process.wait()
            raise BackendError(
                f"Colosseum harness timed out after {self.timeout_seconds:g}s"
            ) from exc

        stdout_text = stdout.decode("utf-8", errors="replace").strip()
        stderr_text = stderr.decode("utf-8", errors="replace").strip()
        payload = _decode_json_object(stdout_text)

        if process.returncode != 0:
            detail = _failure_detail(payload, stderr_text, stdout_text)
            raise BackendError(
                f"Colosseum harness exited {process.returncode}: {detail}"
            )
        if payload is None:
            detail = stderr_text or stdout_text or "<no output>"
            raise BackendError(
                f"Colosseum harness did not return a JSON object: {detail}"
            )

        if payload.get("ok") is False:
            raise BackendError(
                "Colosseum harness reported failure: "
                + json.dumps(payload, ensure_ascii=False, sort_keys=True)
            )
        return payload


def command_prefix_from_env(
    env: Mapping[str, str] | None = None,
) -> tuple[str, ...]:
    source = os.environ if env is None else env
    raw = source.get("COLOSSEUM_HARNESS_COMMAND_JSON", "").strip()
    if not raw:
        raise BackendError(
            "COLOSSEUM_HARNESS_COMMAND_JSON must be a JSON array of argv strings"
        )
    try:
        value = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise BackendError(
            "COLOSSEUM_HARNESS_COMMAND_JSON is not valid JSON"
        ) from exc

    if (
        not isinstance(value, list)
        or not value
        or not all(isinstance(item, str) and item for item in value)
    ):
        raise BackendError(
            "COLOSSEUM_HARNESS_COMMAND_JSON must be a non-empty string array"
        )
    return tuple(value)


def timeout_from_env(
    env: Mapping[str, str] | None = None,
) -> float:
    source = os.environ if env is None else env
    raw = source.get("COLOSSEUM_HARNESS_TIMEOUT_SECONDS", "120").strip()
    try:
        timeout = float(raw)
    except ValueError as exc:
        raise BackendError(
            "COLOSSEUM_HARNESS_TIMEOUT_SECONDS must be numeric"
        ) from exc
    if timeout <= 0:
        raise BackendError(
            "COLOSSEUM_HARNESS_TIMEOUT_SECONDS must be greater than zero"
        )
    return timeout


def _decode_json_object(text: str) -> JsonObject | None:
    if not text:
        return None
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        return None
    return value if isinstance(value, dict) else None


def _failure_detail(
    payload: JsonObject | None,
    stderr_text: str,
    stdout_text: str,
) -> str:
    if payload is not None:
        return json.dumps(payload, ensure_ascii=False, sort_keys=True)
    return stderr_text or stdout_text or "<no output>"
