"""Resource observations for a single lab run."""

from __future__ import annotations

from dataclasses import dataclass
import os
import time
from typing import Any, Callable, Iterable, Mapping


@dataclass(frozen=True)
class ResourceObservationPolicy:
    sample_interval: float = 0.25

    def __post_init__(self) -> None:
        if self.sample_interval <= 0:
            raise ValueError("resource sample interval must be positive")


class ResourceProbe:
    """Normalize resource snapshots and calculate same-run deltas."""

    def __init__(self, policy: ResourceObservationPolicy | None = None, *, clock: Callable[[], float] = time.monotonic) -> None:
        self.policy = policy or ResourceObservationPolicy()
        self.clock = clock

    def observe(self, samples: Iterable[Mapping[str, Any]]) -> dict[str, Any]:
        normalized: list[dict[str, Any]] = []
        errors: list[str] = []
        previous: dict[str, Any] | None = None
        for index, raw in enumerate(samples):
            try:
                sample = dict(raw)
                at = float(sample.pop("at", self.clock()))
                delta: dict[str, float | int] = {}
                if previous is not None:
                    for key, value in sample.items():
                        old = previous.get(key)
                        if isinstance(value, (int, float)) and isinstance(old, (int, float)):
                            delta[key] = round(value - old, 12)
                normalized.append({"index": index, "at": at, **sample, "values": sample, "delta": delta})
                previous = sample
            except (TypeError, ValueError) as error:
                errors.append(f"sample[{index}]: {type(error).__name__}: {error}")
        return {
            "status": "ERROR" if errors else ("PASS" if normalized else "NOT_RUN"),
            "sample_interval_seconds": self.policy.sample_interval,
            "samples": normalized,
            "errors": errors,
            "observation_definition": "raw timestamped values plus deltas between adjacent samples in one run",
        }

    def sample_process(self, pid: int | None = None) -> dict[str, Any]:
        """Take one best-effort process sample without making psutil mandatory."""

        pid = pid or os.getpid()
        try:
            import psutil  # type: ignore
            process = psutil.Process(pid)
            memory = process.memory_info()
            cpu = process.cpu_times()
            values = {
                "pid": pid,
                "rss_bytes": memory.rss,
                "vms_bytes": memory.vms,
                "cpu_seconds": cpu.user + cpu.system,
                "open_files": len(process.open_files()),
                "threads": process.num_threads(),
            }
        except ImportError:
            values = {"pid": pid, "resource_status": "psutil unavailable"}
        except Exception as error:
            values = {"pid": pid, "resource_error": f"{type(error).__name__}: {error}"}
        return {"at": self.clock(), **values}

    def sample(self, pid: int | None = None) -> dict[str, Any]:
        return self.sample_process(pid)
