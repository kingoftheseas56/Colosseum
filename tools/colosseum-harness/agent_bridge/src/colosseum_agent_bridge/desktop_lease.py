from __future__ import annotations

import json
import os
import shutil
import uuid
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Iterator


LEASE_NAME = "desktop-raw-input"
LEASE_DIRNAME = f"{LEASE_NAME}.lease"


class DesktopLeaseError(RuntimeError):
    def __init__(
        self,
        code: str,
        message: str,
        *,
        retryable: bool = False,
        details: dict[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.retryable = retryable
        self.details = details or {}


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _iso(value: datetime) -> str:
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def _parse_iso(value: object) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)
    except ValueError:
        return None


class DesktopLeaseManager:
    """Cross-process lease compatible with Preflight Direct Desktop's lock layout."""

    def __init__(self, runtime_root: Path, *, now=_utc_now) -> None:
        self.runtime_root = Path(runtime_root)
        self.locks_root = self.runtime_root / "locks"
        self.lease_dir = self.locks_root / LEASE_DIRNAME
        self.record_path = self.lease_dir / "lease.json"
        self.operation_dir = self.lease_dir / "operation.lock"
        self._now = now

    def _read(self) -> dict[str, Any] | None:
        try:
            raw = self.record_path.read_text(encoding="utf-8")
        except FileNotFoundError:
            return None
        try:
            value = json.loads(raw)
        except json.JSONDecodeError as exc:
            raise DesktopLeaseError(
                "LEASE_STATE_UNCERTAIN",
                "desktop controller lease record is malformed",
                details={"path": str(self.record_path)},
            ) from exc
        if not isinstance(value, dict):
            raise DesktopLeaseError(
                "LEASE_STATE_UNCERTAIN",
                "desktop controller lease record is not an object",
                details={"path": str(self.record_path)},
            )
        return value

    def status(self) -> dict[str, Any] | None:
        if not self.lease_dir.exists():
            return None
        record = self._read()
        if record is None:
            return {
                "state": "UNCERTAIN",
                "path": str(self.lease_dir),
            }
        expires = _parse_iso(record.get("expiresAt"))
        return {
            **record,
            "state": "EXPIRED" if expires is not None and expires <= self._now() else "OWNED",
            "leasePath": str(self.lease_dir),
        }

    def _write(self, record: dict[str, Any]) -> dict[str, Any]:
        temp = self.lease_dir / f"lease.{uuid.uuid4().hex}.tmp"
        temp.write_text(json.dumps(record, sort_keys=True) + "\n", encoding="utf-8")
        os.replace(temp, self.record_path)
        return record

    def _reclaim_if_expired(self, record: dict[str, Any]) -> bool:
        expires = _parse_iso(record.get("expiresAt"))
        if expires is None:
            raise DesktopLeaseError(
                "LEASE_STATE_UNCERTAIN",
                "desktop controller lease has no valid expiry",
                details={"lease": record},
            )
        if expires > self._now():
            return False
        tombstone = self.locks_root / f"{LEASE_DIRNAME}.expired-{uuid.uuid4().hex}"
        try:
            os.replace(self.lease_dir, tombstone)
        except FileNotFoundError:
            return True
        except PermissionError as exc:
            raise DesktopLeaseError(
                "RESOURCE_BUSY",
                "expired desktop lease could not be reclaimed",
                retryable=True,
                details={"lease": record},
            ) from exc
        shutil.rmtree(tombstone, ignore_errors=True)
        return True

    def acquire(
        self,
        controller: str,
        *,
        ttl_seconds: int,
        hwnd: int,
        title: str,
    ) -> dict[str, Any]:
        controller = controller.strip()
        if not controller:
            raise DesktopLeaseError("INVALID_ARGUMENT", "controller_id is required")
        if ttl_seconds < 30 or ttl_seconds > 900:
            raise DesktopLeaseError(
                "INVALID_ARGUMENT",
                "ttl_seconds must be between 30 and 900",
            )
        self.locks_root.mkdir(parents=True, exist_ok=True)

        for _ in range(5):
            try:
                self.lease_dir.mkdir()
            except FileExistsError:
                current = self._read()
                if current is None:
                    raise DesktopLeaseError(
                        "LEASE_STATE_UNCERTAIN",
                        "desktop controller lease exists without a readable record",
                    )
                if self._reclaim_if_expired(current):
                    continue
                if current.get("controller") == controller:
                    current["expiresAt"] = _iso(self._now() + timedelta(seconds=ttl_seconds))
                    current["hwnd"] = int(hwnd)
                    current["title"] = title
                    return self._write(current)
                raise DesktopLeaseError(
                    "RESOURCE_BUSY",
                    f"desktop is leased by {current.get('controller', 'another controller')}",
                    retryable=True,
                    details={"lease": current},
                )

            now = self._now()
            record = {
                "name": LEASE_NAME,
                "controller": controller,
                "nonce": str(uuid.uuid4()),
                "acquiredAt": _iso(now),
                "expiresAt": _iso(now + timedelta(seconds=ttl_seconds)),
                "hwnd": int(hwnd),
                "title": title,
                "pendingActionId": None,
            }
            try:
                self.record_path.write_text(
                    json.dumps(record, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
            except BaseException:
                shutil.rmtree(self.lease_dir, ignore_errors=True)
                raise
            return record

        raise DesktopLeaseError(
            "RESOURCE_BUSY",
            "desktop controller lease changed during acquisition",
            retryable=True,
        )

    def assert_owner(self, controller: str, *, refresh_seconds: int = 300) -> dict[str, Any]:
        record = self._read()
        if record is None or record.get("controller") != controller:
            raise DesktopLeaseError(
                "LEASE_CONFLICT",
                "controller does not own the desktop lease",
                retryable=True,
            )
        expires = _parse_iso(record.get("expiresAt"))
        if expires is None or expires <= self._now():
            raise DesktopLeaseError(
                "LEASE_EXPIRED",
                "desktop controller lease expired",
                retryable=True,
                details={"lease": record},
            )
        record["expiresAt"] = _iso(self._now() + timedelta(seconds=refresh_seconds))
        return self._write(record)

    def update(self, controller: str, **fields: Any) -> dict[str, Any]:
        record = self.assert_owner(controller)
        record.update(fields)
        return self._write(record)

    @contextmanager
    def operation_lock(self, controller: str) -> Iterator[dict[str, Any]]:
        record = self.assert_owner(controller)
        try:
            self.operation_dir.mkdir()
        except FileExistsError as exc:
            raise DesktopLeaseError(
                "RESOURCE_BUSY",
                "another desktop operation is already in progress",
                retryable=True,
            ) from exc
        try:
            yield record
        finally:
            try:
                self.operation_dir.rmdir()
            except FileNotFoundError:
                pass

    def release(self, controller: str) -> bool:
        record = self.assert_owner(controller, refresh_seconds=30)
        if record.get("pendingActionId"):
            raise DesktopLeaseError(
                "VISUAL_CONFIRMATION_REQUIRED",
                "confirm the pending desktop action before releasing control",
                details={"actionId": record["pendingActionId"]},
            )
        tombstone = self.locks_root / f"{LEASE_DIRNAME}.release-{record['nonce']}"
        try:
            os.replace(self.lease_dir, tombstone)
        except FileNotFoundError:
            return False
        shutil.rmtree(tombstone, ignore_errors=True)
        return True
