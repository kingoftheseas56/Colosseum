"""Byte-level stream observations used by the Server 1.0 lab.

Headers are an observation, not proof that a media stream started.  The probe
keeps monotonic timing and validity decisions separate so callers can preserve
the raw evidence in an EvidenceSchema receipt.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import time
from typing import Any, Callable, Iterable, Mapping
from urllib.request import Request, urlopen


ByteValidator = Callable[[bytes], bool]


@dataclass(frozen=True)
class ObservationPolicy:
    """Frozen timing boundaries, expressed in monotonic elapsed seconds."""

    header_deadline: float = 1.0
    first_byte_deadline: float = 2.0
    first_frame_deadline: float = 5.0

    def __post_init__(self) -> None:
        if min(self.header_deadline, self.first_byte_deadline, self.first_frame_deadline) < 0:
            raise ValueError("observation deadlines must be non-negative")
        if self.first_byte_deadline < self.header_deadline:
            raise ValueError("first-byte deadline cannot precede header deadline")
        if self.first_frame_deadline < self.first_byte_deadline:
            raise ValueError("first-frame deadline cannot precede first-byte deadline")


@dataclass
class ByteObservation:
    """Structured result with stable keys for evidence receipts."""

    status: str = "NOT_RUN"
    header_observed: bool = False
    header_at: float | None = None
    first_valid_byte: float | None = None
    first_valid_frame: float | None = None
    total_bytes: int = 0
    valid_bytes: int = 0
    clock_boundaries: dict[str, float] = field(default_factory=dict)
    timeout: bool = False
    cancelled: bool = False
    cancellation_at: float | None = None
    errors: list[str] = field(default_factory=list)
    events: list[dict[str, Any]] = field(default_factory=list)

    def as_dict(self) -> dict[str, Any]:
        value = dict(self.__dict__)
        value["startup_success"] = self.first_valid_frame is not None and self.status == "PASS"
        return value


def default_frame_validator(body: bytes) -> bool:
    """Require an explicit media-aware validator before claiming a frame.

    Format-specific probes must provide the frame predicate.  A generic byte
    observation is intentionally insufficient to establish a media frame.
    """

    return False


class ByteProbe:
    """Observe HTTP headers, valid bytes, frames, deadlines, and cancellation."""

    def __init__(
        self,
        policy: ObservationPolicy | None = None,
        *,
        byte_validator: ByteValidator | None = None,
        frame_validator: ByteValidator | None = None,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self.policy = policy or ObservationPolicy()
        self.byte_validator = byte_validator or (lambda body: bool(body and body.strip()))
        self.frame_validator = frame_validator if frame_validator is not None else default_frame_validator
        self.clock = clock

    def observe(
        self,
        events: Iterable[Mapping[str, Any] | bytes],
        *,
        end_at: float | None = None,
    ) -> dict[str, Any]:
        started = self.clock()
        result = ByteObservation(clock_boundaries={
            "header_deadline": self.policy.header_deadline,
            "first_byte_deadline": self.policy.first_byte_deadline,
            "first_frame_deadline": self.policy.first_frame_deadline,
        })
        body = bytearray()
        header_buffer = bytearray()
        try:
            for raw_event in events:
                event = {"data": raw_event} if isinstance(raw_event, bytes) else dict(raw_event)
                at = float(event.get("at", self.clock() - started))
                if end_at is not None and at >= end_at and result.first_valid_frame is None:
                    break
                kind = str(event.get("kind", "body"))
                if kind == "cancel":
                    if result.first_valid_frame is None:
                        result.cancelled = True
                        result.cancellation_at = at
                        result.events.append({"at": at, "kind": "cancel", "reason": event.get("reason", "cancelled")})
                        break
                    continue
                if result.first_valid_frame is None:
                    if not result.header_observed and at >= self.policy.header_deadline:
                        result.timeout = True
                        result.events.append({"at": at, "kind": "timeout", "reason": "header_deadline"})
                        break
                    if result.first_valid_byte is None and at >= self.policy.first_byte_deadline:
                        result.timeout = True
                        result.events.append({"at": at, "kind": "timeout", "reason": "first_byte_deadline"})
                        break
                    if at >= self.policy.first_frame_deadline:
                        result.timeout = True
                        result.events.append({"at": at, "kind": "timeout", "reason": "first_frame_deadline"})
                        break
                data = event.get("data", b"")
                if isinstance(data, str):
                    data = data.encode("latin-1")
                if not isinstance(data, (bytes, bytearray)):
                    raise TypeError("byte observation data must be bytes")
                data = bytes(data)
                result.total_bytes += len(data)
                if kind == "headers":
                    header_buffer.extend(data)
                    if b"\r\n\r\n" in header_buffer:
                        result.header_observed = True
                        result.header_at = at
                else:
                    body.extend(data)
                    if not result.header_observed and b"\r\n\r\n" in body:
                        _, remainder = body.split(b"\r\n\r\n", 1)
                        body = bytearray(remainder)
                        result.header_observed = True
                        result.header_at = at
                    if data and self.byte_validator(bytes(body)) and result.first_valid_byte is None:
                        result.first_valid_byte = at
                        result.valid_bytes = len(body)
                    if self.frame_validator(bytes(body)) and result.first_valid_frame is None:
                        result.first_valid_frame = at
                        result.valid_bytes = len(body)
                result.events.append({"at": at, "kind": kind, "bytes": len(data)})
        except Exception as error:  # Preserve probe failures for the receipt.
            result.errors.append(f"{type(error).__name__}: {error}")

        if end_at is not None and result.first_valid_frame is None and not result.cancelled and not result.errors:
            if not result.header_observed and end_at >= self.policy.header_deadline:
                end_reason = "header_deadline"
            elif result.first_valid_byte is None and end_at >= self.policy.first_byte_deadline:
                end_reason = "first_byte_deadline"
            elif end_at >= self.policy.first_frame_deadline:
                end_reason = "first_frame_deadline"
            else:
                end_reason = "stream_ended_without_frame"
            result.events.append({"at": end_at, "kind": "end", "reason": end_reason})
            if end_reason.endswith("_deadline"):
                result.timeout = True

        if result.errors:
            result.status = "ERROR"
        elif result.first_valid_frame is not None:
            result.status = "PASS"
        elif result.cancelled:
            result.status = "FAIL"
        elif result.timeout:
            result.status = "FAIL"
        else:
            result.status = "FAIL"
        return result.as_dict()

    def probe_url(self, url: str, *, timeout: float | None = None, headers: Mapping[str, str] | None = None) -> dict[str, Any]:
        """Capture a real HTTP response as the same observation shape."""

        started = self.clock()
        events: list[dict[str, Any]] = []
        try:
            request = Request(url, headers=dict(headers or {}))
            with urlopen(request, timeout=timeout or self.policy.first_frame_deadline) as response:
                events.append({"at": self.clock() - started, "kind": "headers", "data": _response_headers(response)})
                while True:
                    chunk = response.read(64 * 1024)
                    if not chunk:
                        break
                    events.append({"at": self.clock() - started, "kind": "body", "data": chunk})
        except Exception as error:
            return ByteObservation(status="ERROR", errors=[f"{type(error).__name__}: {error}"]).as_dict()
        return self.observe(events)


def _response_headers(response: Any) -> bytes:
    status = getattr(response, "status", 200)
    reason = getattr(response, "reason", "OK")
    lines = [f"HTTP/1.1 {status} {reason}"]
    lines.extend(f"{key}: {value}" for key, value in response.headers.items())
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1")
