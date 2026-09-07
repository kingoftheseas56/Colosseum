"""Pinned-player lifecycle probe with an honest native-libmpv control seam.

The P07-B packet can exercise synthetic fixtures as unit tests, but synthetic
events are never a runtime qualification.  Native playback is driven through
the pinned libmpv client API and software render target; no custom event
protocol is injected into an external player.
"""

from __future__ import annotations

import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

from ..evidence import EvidenceSchema


CASE_INPUTS = (
    "warm_cache",
    "player_buffer",
    "file_index",
    "peer_script",
    "resource_counter",
)

EVENT_NAMES = {
    "valid_media_decode": "valid_media_decode",
    "presented_frame": "first_presented_frame",
    "seek_complete": "seek_completion",
    "stall": "stall",
}

MPV_FORMAT_FLAG = 3
MPV_FORMAT_DOUBLE = 5
MPV_FORMAT_INT64 = 4
MPV_EVENT_NONE = 0
MPV_EVENT_SHUTDOWN = 1
MPV_EVENT_START_FILE = 6
MPV_EVENT_END_FILE = 7
MPV_EVENT_FILE_LOADED = 8
MPV_EVENT_VIDEO_RECONFIG = 17
MPV_EVENT_SEEK = 20
MPV_EVENT_PLAYBACK_RESTART = 21
MPV_EVENT_PROPERTY_CHANGE = 22
MPV_RENDER_API_TYPE = 1
MPV_RENDER_UPDATE_FRAME = 1
MPV_RENDER_PARAM_SW_SIZE = 17
MPV_RENDER_PARAM_SW_FORMAT = 18
MPV_RENDER_PARAM_SW_STRIDE = 19
MPV_RENDER_PARAM_SW_POINTER = 20


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _state(name: str, state: str, **details: Any) -> dict[str, Any]:
    return {"name": name, "state": state, **details}


class _MpvEvent(ctypes.Structure):
    _fields_ = [
        ("event_id", ctypes.c_int),
        ("error", ctypes.c_int),
        ("reply_userdata", ctypes.c_ulonglong),
        ("data", ctypes.c_void_p),
    ]


class _MpvEventProperty(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("format", ctypes.c_int),
        ("data", ctypes.c_void_p),
    ]


class _MpvRenderParam(ctypes.Structure):
    _fields_ = [
        ("param_type", ctypes.c_int),
        ("data", ctypes.c_void_p),
    ]


_MpvUpdateCallback = ctypes.CFUNCTYPE(None, ctypes.c_void_p)


class _LibMpvBindings:
    """Small, explicit ctypes binding for the libmpv client/render APIs."""

    def __init__(self, path: Path) -> None:
        self.path = path.resolve()
        if not self.path.is_file():
            raise FileNotFoundError(f"libmpv DLL not found: {self.path}")
        self._dll_directories: list[Any] = []
        if os.name == "nt" and hasattr(os, "add_dll_directory"):
            for directory in (self.path.parent, self.path.parent.parent / "bin"):
                if directory.is_dir():
                    try:
                        self._dll_directories.append(os.add_dll_directory(str(directory)))
                    except OSError:
                        pass
        try:
            self.dll = ctypes.CDLL(str(self.path))
        except OSError:
            for directory in (self.path.parent, self.path.parent.parent / "bin"):
                if directory.is_dir():
                    try:
                        self._dll_directories.append(os.add_dll_directory(str(directory)))
                    except OSError:
                        pass
            self.dll = ctypes.CDLL(str(self.path))
        self._bind()

    def _bind(self) -> None:
        self.client_api_version = self.dll.mpv_client_api_version
        self.client_api_version.argtypes = []
        self.client_api_version.restype = ctypes.c_ulong

        self.error_string = self.dll.mpv_error_string
        self.error_string.argtypes = [ctypes.c_int]
        self.error_string.restype = ctypes.c_char_p

        self.create = self.dll.mpv_create
        self.create.argtypes = []
        self.create.restype = ctypes.c_void_p
        self.initialize = self.dll.mpv_initialize
        self.initialize.argtypes = [ctypes.c_void_p]
        self.initialize.restype = ctypes.c_int
        self.set_option_string = self.dll.mpv_set_option_string
        self.set_option_string.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        self.set_option_string.restype = ctypes.c_int
        self.command = self.dll.mpv_command
        self.command.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p)]
        self.command.restype = ctypes.c_int
        self.wait_event = self.dll.mpv_wait_event
        self.wait_event.argtypes = [ctypes.c_void_p, ctypes.c_double]
        self.wait_event.restype = ctypes.POINTER(_MpvEvent)
        self.observe_property = self.dll.mpv_observe_property
        self.observe_property.argtypes = [ctypes.c_void_p, ctypes.c_ulonglong, ctypes.c_char_p, ctypes.c_int]
        self.observe_property.restype = ctypes.c_int
        self.render_context_create = self.dll.mpv_render_context_create
        self.render_context_create.argtypes = [
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.c_void_p,
            ctypes.POINTER(_MpvRenderParam),
        ]
        self.render_context_create.restype = ctypes.c_int
        self.render_context_free = self.dll.mpv_render_context_free
        self.render_context_free.argtypes = [ctypes.c_void_p]
        self.render_context_free.restype = None
        self.render_context_set_update_callback = self.dll.mpv_render_context_set_update_callback
        self.render_context_set_update_callback.argtypes = [
            ctypes.c_void_p,
            _MpvUpdateCallback,
            ctypes.c_void_p,
        ]
        self.render_context_set_update_callback.restype = None
        self.render_context_update = self.dll.mpv_render_context_update
        self.render_context_update.argtypes = [ctypes.c_void_p]
        self.render_context_update.restype = ctypes.c_ulonglong
        self.render_context_render = self.dll.mpv_render_context_render
        self.render_context_render.argtypes = [ctypes.c_void_p, ctypes.POINTER(_MpvRenderParam)]
        self.render_context_render.restype = ctypes.c_int
        self.terminate_destroy = self.dll.mpv_terminate_destroy
        self.terminate_destroy.argtypes = [ctypes.c_void_p]
        self.terminate_destroy.restype = None

    def error(self, code: int) -> str:
        value = self.error_string(code)
        return value.decode("utf-8", errors="replace") if value else f"mpv error {code}"

    @property
    def client_api(self) -> int:
        return int(self.client_api_version())


class LibMpvAdapter:
    """Identity and playback adapter for the pinned libmpv client API."""

    def __init__(self, path: Path | str, expected_sha256: str | None = None) -> None:
        self.path = Path(path).expanduser().resolve()
        self.expected_sha256 = expected_sha256.lower() if expected_sha256 else None
        self.bindings = _LibMpvBindings(self.path)

    def identity(self) -> dict[str, Any]:
        digest = _sha256(self.path)
        version = _windows_file_version(self.path)
        api = self.bindings.client_api
        return {
            "backend": "libmpv",
            "path": str(self.path),
            "bytes": self.path.stat().st_size,
            "sha256": digest,
            "expected_sha256": self.expected_sha256,
            "hash_match": self.expected_sha256 is None or digest == self.expected_sha256,
            "file_version": version,
            "client_api": f"{api >> 16}.{api & 0xFFFF}",
            "client_api_numeric": api,
        }

    def probe(
        self,
        *,
        media_url: str,
        seek_seconds: float | None,
        timeout_seconds: float,
        cancel_requested: Callable[[], bool] | None = None,
    ) -> dict[str, Any]:
        return _LibMpvPlayback(
            self.bindings,
            media_url=media_url,
            seek_seconds=seek_seconds,
            timeout_seconds=timeout_seconds,
            cancel_requested=cancel_requested,
        ).run()


class _LibMpvPlayback:
    """Drive one libmpv instance and collect only native observable evidence."""

    def __init__(
        self,
        bindings: _LibMpvBindings,
        *,
        media_url: str,
        seek_seconds: float | None,
        timeout_seconds: float,
        cancel_requested: Callable[[], bool] | None,
    ) -> None:
        self.bindings = bindings
        self.media_url = media_url
        self.seek_seconds = seek_seconds
        self.timeout_seconds = timeout_seconds
        self.cancel_requested = cancel_requested
        self.events: list[dict[str, Any]] = []
        self.properties: list[dict[str, Any]] = []
        self._update_pending = False
        self._update_callback = _MpvUpdateCallback(self._on_update)
        self._mpv: ctypes.c_void_p | None = None
        self._render_context: ctypes.c_void_p | None = None
        self.options: dict[str, str] = {}

    def _on_update(self, _ctx: ctypes.c_void_p) -> None:
        self._update_pending = True

    def _command(self, *arguments: str) -> tuple[int, list[str]]:
        encoded = [argument.encode("utf-8") for argument in arguments]
        argv = (ctypes.c_char_p * (len(encoded) + 1))()
        for index, value in enumerate(encoded):
            argv[index] = value
        argv[len(encoded)] = None
        return self.bindings.command(self._mpv, argv), list(arguments)

    def _record_event(self, event: _MpvEvent) -> int:
        event_id = int(event.contents.event_id)
        record: dict[str, Any] = {
            "event_id": event_id,
            "error_code": int(event.contents.error),
        }
        if event_id == MPV_EVENT_PROPERTY_CHANGE and event.contents.data:
            prop = ctypes.cast(event.contents.data, ctypes.POINTER(_MpvEventProperty)).contents
            name = prop.name.decode("utf-8", errors="replace") if prop.name else ""
            record["event"] = "property_change"
            record["property"] = name
            record["format"] = int(prop.format)
            if prop.data:
                if prop.format == MPV_FORMAT_FLAG:
                    record["value"] = bool(ctypes.cast(prop.data, ctypes.POINTER(ctypes.c_int)).contents.value)
                elif prop.format == MPV_FORMAT_DOUBLE:
                    record["value"] = float(ctypes.cast(prop.data, ctypes.POINTER(ctypes.c_double)).contents.value)
                elif prop.format == MPV_FORMAT_INT64:
                    record["value"] = int(ctypes.cast(prop.data, ctypes.POINTER(ctypes.c_longlong)).contents.value)
            self.properties.append(record)
        else:
            record["event"] = {
                MPV_EVENT_NONE: "none",
                MPV_EVENT_SHUTDOWN: "shutdown",
                MPV_EVENT_START_FILE: "start_file",
                MPV_EVENT_END_FILE: "end_file",
                MPV_EVENT_FILE_LOADED: "file_loaded",
                MPV_EVENT_VIDEO_RECONFIG: "video_reconfig",
                MPV_EVENT_SEEK: "seek",
                MPV_EVENT_PLAYBACK_RESTART: "playback_restart",
            }.get(event_id, f"event_{event_id}")
        self.events.append(record)
        return event_id

    def _render_frame(self) -> dict[str, Any] | None:
        if self._render_context is None:
            return None
        update_flags = int(self.bindings.render_context_update(self._render_context))
        if not self._update_pending and not (update_flags & MPV_RENDER_UPDATE_FRAME):
            return None
        self._update_pending = False
        width, height = 320, 180
        stride = width * 4
        sentinel = bytes([0xA5]) * (stride * height)
        backing = (ctypes.c_ubyte * (len(sentinel) + 63))()
        backing_address = ctypes.addressof(backing)
        aligned_offset = (-backing_address) % 64
        pixels = (ctypes.c_ubyte * len(sentinel)).from_buffer(backing, aligned_offset)
        ctypes.memmove(pixels, sentinel, len(sentinel))
        size = (ctypes.c_int * 2)(width, height)
        format_name = ctypes.create_string_buffer(b"rgb0")
        stride_value = ctypes.c_size_t(stride)
        params = (_MpvRenderParam * 6)()
        params[0] = _MpvRenderParam(MPV_RENDER_PARAM_SW_SIZE, ctypes.cast(size, ctypes.c_void_p))
        params[1] = _MpvRenderParam(MPV_RENDER_PARAM_SW_FORMAT, ctypes.cast(format_name, ctypes.c_void_p))
        params[2] = _MpvRenderParam(MPV_RENDER_PARAM_SW_STRIDE, ctypes.cast(ctypes.pointer(stride_value), ctypes.c_void_p))
        params[3] = _MpvRenderParam(MPV_RENDER_PARAM_SW_POINTER, ctypes.cast(pixels, ctypes.c_void_p))
        params[4] = _MpvRenderParam(0, None)
        result = int(self.bindings.render_context_render(self._render_context, params))
        if result != 0:
            return {"state": "UNAVAILABLE", "render_error": self.bindings.error(result)}
        rendered = bytes(pixels)
        if rendered == sentinel:
            return None
        return {
            "width": width,
            "height": height,
            "stride": stride,
            "format": "rgb0",
            "frame_sha256": hashlib.sha256(rendered).hexdigest(),
            "render_api": "libmpv:sw",
            "render_target": "software:rgb0",
        }

    def run(self) -> dict[str, Any]:
        observations: dict[str, dict[str, Any]] = {
            "process_start": _state("process_start", "UNAVAILABLE", reason="libmpv instance not initialized"),
            "http_readiness": _state("http_readiness", "NOT_RUN", reason="performed by PlayerProbe"),
            "valid_media_decode": _state("valid_media_decode", "UNAVAILABLE", reason="no native frame evidence"),
            "first_presented_frame": _state("first_presented_frame", "UNAVAILABLE", reason="no native software render evidence"),
            "seek_completion": _state("seek_completion", "NOT_RUN" if self.seek_seconds is None else "UNAVAILABLE", reason="no native seek completion evidence"),
            "stall": _state("stall", "UNAVAILABLE", reason="no paused-for-cache transition observed"),
            "timeout": _state("timeout", "NOT_RUN"),
            "cancellation": _state("cancellation", "NOT_RUN"),
            "unavailable_telemetry": _state("unavailable_telemetry", "UNAVAILABLE"),
        }
        commands: list[dict[str, Any]] = []
        error: str | None = None
        started = time.monotonic()
        seek_issued = False
        loaded = False
        frame: dict[str, Any] | None = None
        saw_paused_for_cache = False
        try:
            self._mpv = self.bindings.create()
            if not self._mpv:
                raise RuntimeError("mpv_create returned null")
            for name, value in (
                ("config", "no"),
                ("terminal", "no"),
                ("msg-level", "all=error"),
                ("ao", "null"),
                ("vo", "libmpv"),
                ("hwdec", "no"),
                ("ytdl", "no"),
                ("load-scripts", "no"),
                ("osc", "no"),
                ("idle", "yes"),
                ("keep-open", "yes"),
                ("video-sync", "audio"),
            ):
                result = int(self.bindings.set_option_string(self._mpv, name.encode(), value.encode()))
                if result != 0:
                    raise RuntimeError(f"mpv_set_option_string {name}: {self.bindings.error(result)}")
                self.options[name] = value
            result = int(self.bindings.initialize(self._mpv))
            if result != 0:
                raise RuntimeError(f"mpv_initialize: {self.bindings.error(result)}")
            observations["process_start"] = _state(
                "process_start",
                "PASS",
                process_model="in_process_libmpv",
                launch_boundary="mpv_create+mpv_initialize",
            )
            for property_name, property_format in (
                ("time-pos", MPV_FORMAT_DOUBLE),
                ("duration", MPV_FORMAT_DOUBLE),
                ("paused-for-cache", MPV_FORMAT_FLAG),
                ("eof-reached", MPV_FORMAT_FLAG),
                ("demuxer-cache-time", MPV_FORMAT_DOUBLE),
            ):
                result = int(self.bindings.observe_property(self._mpv, 1, property_name.encode(), property_format))
                if result != 0:
                    raise RuntimeError(f"mpv_observe_property {property_name}: {self.bindings.error(result)}")
            api_type = ctypes.create_string_buffer(b"sw")
            render_params = (_MpvRenderParam * 2)()
            render_params[0] = _MpvRenderParam(MPV_RENDER_API_TYPE, ctypes.cast(api_type, ctypes.c_void_p))
            render_params[1] = _MpvRenderParam(0, None)
            render_context = ctypes.c_void_p()
            result = int(self.bindings.render_context_create(ctypes.byref(render_context), self._mpv, render_params))
            if result != 0:
                raise RuntimeError(f"mpv_render_context_create: {self.bindings.error(result)}")
            self._render_context = render_context
            self.bindings.render_context_set_update_callback(self._render_context, self._update_callback, None)
            result, argv = self._command("loadfile", self.media_url, "replace")
            commands.append({"operation": "loadfile", "argv": argv, "status": result})
            if result != 0:
                raise RuntimeError(f"loadfile: {self.bindings.error(result)}")
            deadline = time.monotonic() + self.timeout_seconds
            while time.monotonic() < deadline:
                if self.cancel_requested and self.cancel_requested():
                    observations["cancellation"] = _state("cancellation", "PASS", reason="requested by caller")
                    break
                event_pointer = self.bindings.wait_event(self._mpv, 0.05)
                if event_pointer:
                    event_id = self._record_event(event_pointer)
                    if event_id == MPV_EVENT_FILE_LOADED:
                        loaded = True
                        if self.seek_seconds is not None and not seek_issued:
                            result, argv = self._command("seek", str(self.seek_seconds), "absolute+exact")
                            commands.append({"operation": "seek", "argv": argv, "status": result})
                            seek_issued = result == 0
                    elif event_id == MPV_EVENT_PLAYBACK_RESTART and seek_issued and observations["seek_completion"]["state"] != "PASS":
                        observations["seek_completion"] = _state(
                            "seek_completion",
                            "PASS",
                            completion_signal="mpv_event_playback_restart",
                            target_seconds=self.seek_seconds,
                        )
                    elif event_id == MPV_EVENT_SHUTDOWN:
                        break
                for prop in self.properties[-8:]:
                    if prop.get("property") == "paused-for-cache" and prop.get("value") is True:
                        saw_paused_for_cache = True
                        observations["stall"] = _state(
                            "stall",
                            "PASS",
                            signal="paused-for-cache",
                            source="mpv_event_property_change",
                        )
                if loaded and frame is None:
                    candidate = self._render_frame()
                    if candidate and candidate.get("state") == "UNAVAILABLE":
                        observations["first_presented_frame"] = _state("first_presented_frame", "UNAVAILABLE", **candidate)
                    elif candidate:
                        frame = candidate
                        observations["valid_media_decode"] = _state(
                            "valid_media_decode",
                            "PASS",
                            source="mpv_render_context_render",
                            **candidate,
                        )
                        observations["first_presented_frame"] = _state(
                            "first_presented_frame",
                            "PASS",
                            source="mpv_render_context_render",
                            presentation="native_software_surface",
                            **candidate,
                        )
                seek_complete = self.seek_seconds is None or observations["seek_completion"]["state"] == "PASS"
                if frame is not None and seek_complete and observations["stall"]["state"] == "PASS":
                    break
            else:
                observations["timeout"] = _state("timeout", "PASS", limit_seconds=self.timeout_seconds)
        except (OSError, RuntimeError, ValueError) as exc:
            error = str(exc)
            observations["process_start"] = _state("process_start", "ERROR", error=error)
        finally:
            if self._render_context is not None:
                try:
                    self.bindings.render_context_free(self._render_context)
                except OSError:
                    pass
                self._render_context = None
            if self._mpv is not None:
                try:
                    self.bindings.terminate_destroy(self._mpv)
                except OSError:
                    pass
                self._mpv = None
        unavailable = [
            name
            for name, value in observations.items()
            if name != "unavailable_telemetry" and value["state"] == "UNAVAILABLE"
        ]
        observations["unavailable_telemetry"] = _state(
            "unavailable_telemetry",
            "PASS" if not unavailable else "UNAVAILABLE",
            fields=unavailable,
        )
        if observations["cancellation"]["state"] == "PASS":
            result = "ERROR"
        elif error:
            result = "NOT_RUN"
        elif observations["timeout"]["state"] == "PASS":
            result = "ERROR"
        else:
            required_observations = ["process_start", "valid_media_decode", "first_presented_frame", "stall"]
            if self.seek_seconds is not None:
                required_observations.append("seek_completion")
            result = "PASS" if all(observations[name]["state"] == "PASS" for name in required_observations) else "NOT_RUN"
        return {
            "result": result,
            "observations": observations,
            "events": self.events,
            "properties": self.properties,
            "options": self.options,
            "commands": commands,
            "frame": frame,
            "error": error,
            "elapsed_ms": round((time.monotonic() - started) * 1000, 3),
            "media_url": self.media_url,
            "seek_seconds": self.seek_seconds,
            "loaded": loaded,
            "saw_paused_for_cache": saw_paused_for_cache,
        }


class PlayerProbe:
    """Run a pinned player without promoting synthetic or incomplete evidence."""

    def run(
        self,
        *,
        media_url: str,
        data_root: Path,
        evidence_dir: Path,
        run_id: str,
        executable: Path | str | None = None,
        libmpv_path: Path | str | None = None,
        expected_player_sha256: str | None = None,
        config: Path | str | None = None,
        case_inputs: dict[str, Any] | None = None,
        seek_seconds: float | None = None,
        timeout_seconds: float = 5.0,
        http_ready: bool = True,
        runner: Any | None = None,
        fixture_corpus: Any | None = None,
        cancel_requested: Callable[[], bool] | None = None,
        reference_runtime: Any | None = None,
        fixture_identifiers: dict[str, Any] | None = None,
        synthetic_fixture: bool = False,
        qualification_scope: str = "packet_local_unit",
    ) -> dict[str, Any]:
        started_at = _utc_now()
        data_root = data_root.resolve()
        evidence_dir = evidence_dir.resolve()
        inputs = self._case_inputs(case_inputs)
        run_root = data_root / run_id
        run_root.mkdir(parents=True, exist_ok=False)
        evidence_dir.mkdir(parents=True, exist_ok=True)
        config_record = self._config_record(config)
        observations = self._empty_observations(http_ready)
        if http_ready:
            observations["http_readiness"] = self._http_readiness(media_url, timeout_seconds)
        else:
            observations["http_readiness"] = _state("http_readiness", "NOT_RUN", reason="readiness check not configured")

        source: dict[str, Any]
        configuration: dict[str, Any]
        request_sequence: list[dict[str, Any]]
        raw_lane: dict[str, Any]
        replay: dict[str, Any]
        errors: list[str] = []
        resource_state = "UNAVAILABLE" if inputs["resource_counter"] is None or str(inputs["resource_counter"]).lower() in {"unavailable", "unknown", "not_available"} else "PASS"
        resource_observations = [{"name": "resource_counter", "state": resource_state, "value": inputs["resource_counter"]}]

        if libmpv_path is not None:
            native = self._run_native(
                libmpv_path=Path(libmpv_path),
                expected_player_sha256=expected_player_sha256,
                media_url=media_url,
                seek_seconds=seek_seconds,
                timeout_seconds=timeout_seconds,
                cancel_requested=cancel_requested,
                run_root=run_root,
            )
            identity = native["identity"]
            native_result = native["result"]
            native_observations = native["observations"]
            for name, value in native_observations.items():
                if name != "http_readiness":
                    observations[name] = value
            source = {
                "authority": "P07-B",
                "evidence_class": "native_libmpv_adapter",
                "identity": identity["path"],
                "sha256": identity.get("sha256"),
                "backend": "libmpv",
            }
            configuration = {
                "evidence_class": "native_libmpv_adapter",
                "qualification_scope": qualification_scope,
                "player_backend": "libmpv",
                "player_identity": identity,
                "player_executable": None,
                "player_sha256": identity.get("sha256"),
                "player_version": identity,
                "config": config_record,
                "media_url": media_url,
                "seek_seconds": seek_seconds,
                "timeout_seconds": timeout_seconds,
                "native_control": "ctypes_client_api_and_software_render",
                "player_options": native["options"],
            }
            request_sequence = [
                {"operation": "libmpv_identity", "path": identity["path"], "sha256": identity.get("sha256")},
                *native["commands"],
            ]
            raw_lane = {
                "backend": "libmpv",
                "events": native["events"],
                "properties": native["properties"],
                "options": native["options"],
                "frame": native["frame"],
                "error": native["error"],
                "elapsed_ms": native["elapsed_ms"],
            }
            replay = {
                "backend": "libmpv",
                "exact_media_url": media_url,
                "libmpv_path": identity["path"],
                "libmpv_sha256": identity.get("sha256"),
                "commands": native["commands"],
                "required_substitutions": ["libmpv_path", "media_url", "data_root", "evidence_dir", "run_id"],
            }
            errors.extend([native["error"]] if native["error"] else [])
            result = native_result
            if qualification_scope != "runtime":
                errors.append("native adapter evidence is packet-local; P07-C/B-W1D owns runtime qualification")
                if result in {"PASS", "ERROR"}:
                    result = "NOT_RUN"
        elif executable is not None and synthetic_fixture:
            synthetic = self._run_synthetic(
                executable=Path(executable),
                media_url=media_url,
                seek_seconds=seek_seconds,
                timeout_seconds=timeout_seconds,
                run_root=run_root,
                cancel_requested=cancel_requested,
            )
            for name, value in synthetic["observations"].items():
                if name != "http_readiness":
                    observations[name] = value
            source = {
                "authority": "P07-B",
                "evidence_class": "synthetic_unit_fixture",
                "identity": str(synthetic["executable"]),
                "sha256": synthetic["sha256"],
            }
            configuration = {
                "evidence_class": "synthetic_unit_fixture",
                "qualification_scope": qualification_scope,
                "player_executable": str(synthetic["executable"]),
                "player_sha256": synthetic["sha256"],
                "player_version": synthetic["version"],
                "config": config_record,
                "media_url": media_url,
                "seek_seconds": seek_seconds,
                "timeout_seconds": timeout_seconds,
                "synthetic_fixture": True,
            }
            request_sequence = [
                {"operation": "version", "argv": synthetic["version"]["command"]},
                {"operation": "synthetic_fixture", "argv": synthetic["command"]},
            ]
            raw_lane = synthetic["raw_lane"]
            replay = {
                "backend": "synthetic_fixture",
                "exact_command": synthetic["command"],
                "required_substitutions": ["executable", "media_url", "data_root", "evidence_dir", "run_id"],
            }
            result = "NOT_RUN"
            errors.append("synthetic fixture evidence is unit-only and cannot qualify runtime")
        else:
            unqualified = self._unqualified_external(executable)
            source = {
                "authority": "P07-B",
                "evidence_class": "unqualified_external_player",
                "identity": unqualified["identity"],
                "sha256": unqualified["sha256"],
            }
            configuration = {
                "evidence_class": "unqualified_external_player",
                "qualification_scope": qualification_scope,
                "player_executable": unqualified["identity"],
                "player_sha256": unqualified["sha256"],
                "player_version": unqualified["version"],
                "config": config_record,
                "media_url": media_url,
                "seek_seconds": seek_seconds,
                "timeout_seconds": timeout_seconds,
                "custom_event_protocol": "disabled",
            }
            request_sequence = []
            raw_lane = {"backend": "unqualified_external_player", "events": [], "stdout": "", "stderr": ""}
            replay = {
                "backend": "unqualified_external_player",
                "required_substitutions": ["libmpv_path"],
                "reason": "no verified real-player adapter or harness was supplied",
            }
            result = "NOT_RUN"
            errors.append("no verified real-player adapter or harness; custom telemetry is not accepted")

        unavailable = [
            name
            for name, value in observations.items()
            if name != "unavailable_telemetry" and value["state"] == "UNAVAILABLE"
        ]
        observations["unavailable_telemetry"] = _state(
            "unavailable_telemetry",
            "PASS" if not unavailable else "UNAVAILABLE",
            fields=unavailable,
        )
        if result == "PASS" and source["evidence_class"] != "native_libmpv_adapter":
            result = "NOT_RUN"
        raw_stdout = str(raw_lane.get("stdout", ""))
        raw_stderr = str(raw_lane.get("stderr", ""))
        receipt = EvidenceSchema.receipt(
            result=result,
            run_id=run_id,
            engine={"name": "pinned-player-probe", "version": "2", "control_seam": "libmpv_ctypes"},
            source=source,
            scenario="P07-02",
            environment={"platform": sys.platform, "python": sys.version.split()[0]},
            configuration={
                **configuration,
                "interfaces_consumed": {
                    "LabRunner": self._interface_record(runner),
                    "QualifiedFixtureCorpus": self._interface_record(fixture_corpus),
                    "QualifiedReferenceRuntime": self._interface_record(reference_runtime),
                },
            },
            fixture_identifiers={**(fixture_identifiers or {}), "case_inputs": inputs},
            request_sequence=request_sequence,
            timestamps={"started": started_at, "finished": _utc_now()},
            raw_lane={**raw_lane, "stdout": raw_stdout, "stderr": raw_stderr},
            normalized_lane={"observations": observations},
            observations=list(observations.values()),
            errors=errors,
            replay=replay,
            resource_observations=resource_observations,
            peer_observations=[{"name": "peer_script", "state": "DECLARED", "value": inputs["peer_script"]}],
            paths={
                "run_root": str(run_root),
                "stdout": str(run_root / "stdout.txt"),
                "stderr": str(run_root / "stderr.txt"),
                "config": config_record.get("path"),
            },
        )
        EvidenceSchema.write(evidence_dir / "run.json", receipt)
        return receipt

    @staticmethod
    def _empty_observations(http_ready: bool) -> dict[str, dict[str, Any]]:
        return {
            "process_start": _state("process_start", "NOT_RUN", reason="no player control selected"),
            "http_readiness": _state("http_readiness", "NOT_RUN" if not http_ready else "UNAVAILABLE"),
            "valid_media_decode": _state("valid_media_decode", "UNAVAILABLE", reason="no native frame evidence"),
            "first_presented_frame": _state("first_presented_frame", "UNAVAILABLE", reason="no native presentation evidence"),
            "seek_completion": _state("seek_completion", "UNAVAILABLE", reason="no native seek evidence"),
            "stall": _state("stall", "UNAVAILABLE", reason="no native stall evidence"),
            "timeout": _state("timeout", "NOT_RUN"),
            "cancellation": _state("cancellation", "NOT_RUN"),
            "unavailable_telemetry": _state("unavailable_telemetry", "UNAVAILABLE"),
        }

    @staticmethod
    def _case_inputs(values: dict[str, Any] | None) -> dict[str, Any]:
        supplied = values or {}
        missing = [key for key in CASE_INPUTS if key not in supplied]
        if missing:
            raise ValueError(f"P07-02 case inputs missing: {missing}")
        return {key: supplied[key] for key in CASE_INPUTS}

    @staticmethod
    def _resolve_executable(value: Path) -> Path:
        resolved = Path(shutil.which(str(value)) or value).expanduser().resolve()
        if not resolved.is_file():
            raise FileNotFoundError(f"player executable not found: {value}")
        return resolved

    @staticmethod
    def _version_command(executable: Path) -> list[str]:
        return ([sys.executable, str(executable)] if executable.suffix.lower() == ".py" else [str(executable)]) + ["--version"]

    @classmethod
    def _version(cls, executable: Path) -> dict[str, Any]:
        command = cls._version_command(executable)
        try:
            completed = subprocess.run(command, capture_output=True, text=True, check=False, timeout=5)
        except (OSError, subprocess.TimeoutExpired) as error:
            return {
                "available": False,
                "command": command,
                "exit_code": None,
                "stdout": "",
                "stderr": str(error),
                "error": str(error),
            }
        return {
            "available": completed.returncode == 0 and bool((completed.stdout + completed.stderr).strip()),
            "command": command,
            "exit_code": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
            "text": (completed.stdout + completed.stderr).strip(),
        }

    @staticmethod
    def _command(executable: Path, media_url: str, seek_seconds: float | None) -> list[str]:
        command = [sys.executable, str(executable)] if executable.suffix.lower() == ".py" else [str(executable)]
        command.append(media_url)
        if seek_seconds is not None:
            command += ["--start", str(seek_seconds)]
        return command

    @staticmethod
    def _config_record(config: Path | str | None) -> dict[str, Any]:
        if config is None:
            return {"state": "UNAVAILABLE", "reason": "no config path supplied"}
        path = Path(config).expanduser().resolve()
        if not path.is_file():
            return {"state": "UNAVAILABLE", "path": str(path), "reason": "config path is not a file"}
        return {"state": "PASS", "path": str(path), "sha256": _sha256(path), "bytes": path.stat().st_size}

    @staticmethod
    def _interface_record(value: Any | None) -> dict[str, Any]:
        if value is None:
            return {"state": "UNAVAILABLE"}
        return {"state": "AVAILABLE", "type": f"{type(value).__module__}.{type(value).__qualname__}"}

    @staticmethod
    def _http_readiness(url: str, timeout: float) -> dict[str, Any]:
        started = time.monotonic()
        if not url.lower().startswith(("http://", "https://")):
            return _state("http_readiness", "NOT_RUN", reason="media URL is not HTTP")
        request = urllib.request.Request(
            url,
            headers={"Range": "bytes=0-0", "Accept-Encoding": "identity"},
            method="GET",
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return _state(
                    "http_readiness",
                    "PASS",
                    method="GET",
                    range="bytes=0-0",
                    status=response.status,
                    elapsed_ms=round((time.monotonic() - started) * 1000, 3),
                )
        except (OSError, urllib.error.URLError, urllib.error.HTTPError) as error:
            return _state(
                "http_readiness",
                "UNAVAILABLE",
                method="GET",
                range="bytes=0-0",
                reason=str(error),
                elapsed_ms=round((time.monotonic() - started) * 1000, 3),
            )

    def _run_native(
        self,
        *,
        libmpv_path: Path,
        expected_player_sha256: str | None,
        media_url: str,
        seek_seconds: float | None,
        timeout_seconds: float,
        cancel_requested: Callable[[], bool] | None,
        run_root: Path,
    ) -> dict[str, Any]:
        try:
            adapter = LibMpvAdapter(libmpv_path, expected_sha256=expected_player_sha256)
            identity = adapter.identity()
            if expected_player_sha256 and not identity["hash_match"]:
                return {
                    "identity": identity,
                    "result": "NOT_RUN",
                    "observations": self._empty_observations(False),
                    "events": [],
                    "properties": [],
                    "options": {},
                    "commands": [],
                    "frame": None,
                    "error": "libmpv SHA-256 does not match the pinned dependency identity",
                    "elapsed_ms": 0,
                }
            playback = adapter.probe(
                media_url=media_url,
                seek_seconds=seek_seconds,
                timeout_seconds=timeout_seconds,
                cancel_requested=cancel_requested,
            )
            (run_root / "libmpv-events.json").write_text(
                json.dumps(
                    {
                        "identity": identity,
                        "events": playback["events"],
                        "properties": playback["properties"],
                        "options": playback["options"],
                        "commands": playback["commands"],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            return {"identity": identity, **playback}
        except (AttributeError, FileNotFoundError, OSError, RuntimeError, ValueError, TypeError, ctypes.ArgumentError) as error:
            identity = {
                "backend": "libmpv",
                "path": str(libmpv_path.resolve()),
                "expected_sha256": expected_player_sha256.lower() if expected_player_sha256 else None,
                "available": False,
                "error": str(error),
            }
            return {
                "identity": identity,
                "result": "NOT_RUN",
                "observations": self._empty_observations(False),
                "events": [],
                "properties": [],
                "options": {},
                "commands": [],
                "frame": None,
                "error": str(error),
                "elapsed_ms": 0,
            }

    def _run_synthetic(
        self,
        *,
        executable: Path,
        media_url: str,
        seek_seconds: float | None,
        timeout_seconds: float,
        run_root: Path,
        cancel_requested: Callable[[], bool] | None,
    ) -> dict[str, Any]:
        executable_path = self._resolve_executable(executable)
        event_file = run_root / "synthetic-events.jsonl"
        stdout_path = run_root / "stdout.txt"
        stderr_path = run_root / "stderr.txt"
        version = self._version(executable_path)
        command = self._command(executable_path, media_url, seek_seconds)
        environment = {
            **os.environ,
            "COLOSSEUM_PLAYER_PROBE_EVENT_FILE": str(event_file),
            "COLOSSEUM_PLAYER_PROBE_RUN_ROOT": str(run_root),
            "COLOSSEUM_PLAYER_PROBE_MEDIA_URL": media_url,
        }
        observations = {
            "process_start": _state("process_start", "PASS" if version["available"] else "ERROR"),
            "http_readiness": _state("http_readiness", "NOT_RUN"),
            "valid_media_decode": _state("valid_media_decode", "UNAVAILABLE"),
            "first_presented_frame": _state("first_presented_frame", "UNAVAILABLE"),
            "seek_completion": _state("seek_completion", "UNAVAILABLE"),
            "stall": _state("stall", "UNAVAILABLE"),
            "timeout": _state("timeout", "NOT_RUN"),
            "cancellation": _state("cancellation", "NOT_RUN"),
            "unavailable_telemetry": _state("unavailable_telemetry", "UNAVAILABLE"),
        }
        process: subprocess.Popen[str] | None = None
        events: list[dict[str, Any]] = []
        exit_code: int | None = None
        launch_error: str | None = None
        timed_out = False
        cancelled = False
        if version["available"]:
            try:
                with stdout_path.open("w", encoding="utf-8") as stdout, stderr_path.open("w", encoding="utf-8") as stderr:
                    process = subprocess.Popen(command, cwd=run_root, env=environment, stdout=stdout, stderr=stderr, text=True)
                    deadline = time.monotonic() + timeout_seconds
                    while process.poll() is None:
                        events = self._read_events(event_file)
                        self._apply_events(observations, events)
                        if cancel_requested and cancel_requested():
                            cancelled = True
                            observations["cancellation"] = _state("cancellation", "PASS", reason="requested by caller")
                            process.terminate()
                            break
                        if time.monotonic() >= deadline:
                            timed_out = True
                            observations["timeout"] = _state("timeout", "PASS", limit_seconds=timeout_seconds)
                            process.terminate()
                            break
                        time.sleep(0.01)
                    try:
                        exit_code = process.wait(timeout=1.0)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        exit_code = process.wait(timeout=1.0)
                    events = self._read_events(event_file)
                    self._apply_events(observations, events)
            except OSError as error:
                launch_error = str(error)
                observations["process_start"] = _state("process_start", "ERROR", error=launch_error)
        else:
            launch_error = version.get("error") or "synthetic fixture version unavailable"
        raw_stdout = stdout_path.read_text(encoding="utf-8", errors="replace") if stdout_path.exists() else ""
        raw_stderr = stderr_path.read_text(encoding="utf-8", errors="replace") if stderr_path.exists() else ""
        return {
            "executable": executable_path,
            "sha256": _sha256(executable_path),
            "version": version,
            "command": command,
            "events": events,
            "observations": observations,
            "raw_lane": {
                "events": events,
                "stdout": raw_stdout,
                "stderr": raw_stderr,
                "exit_code": exit_code,
                "launch_error": launch_error,
                "synthetic_protocol": "COLOSSEUM_PLAYER_PROBE_EVENT_FILE",
                "timed_out": timed_out,
                "cancelled": cancelled,
            },
        }

    @staticmethod
    def _unqualified_external(executable: Path | str | None) -> dict[str, Any]:
        if executable is None:
            return {"identity": "UNAVAILABLE", "sha256": None, "version": {"available": False, "reason": "no executable supplied"}}
        path = Path(executable).expanduser().resolve()
        if not path.is_file():
            return {"identity": str(path), "sha256": None, "version": {"available": False, "reason": "executable not found"}}
        try:
            digest = _sha256(path)
        except OSError as error:
            digest = None
            error_text = str(error)
        else:
            error_text = "external player not trusted by P07-B"
        return {"identity": str(path), "sha256": digest, "version": {"available": False, "reason": error_text}}

    @staticmethod
    def _read_events(path: Path) -> list[dict[str, Any]]:
        if not path.is_file():
            return []
        events: list[dict[str, Any]] = []
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict) and isinstance(value.get("event"), str):
                events.append(value)
        return events

    @staticmethod
    def _apply_events(observations: dict[str, dict[str, Any]], events: list[dict[str, Any]]) -> None:
        for event in events:
            name = EVENT_NAMES.get(event.get("event"))
            if not name:
                continue
            details = {key: value for key, value in event.items() if key != "event"}
            observations[name] = _state(name, "PASS", **details)


def _windows_file_version(path: Path) -> str | None:
    if os.name != "nt":
        return None
    try:
        version = ctypes.windll.version
        version.GetFileVersionInfoSizeW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint)]
        version.GetFileVersionInfoSizeW.restype = ctypes.c_uint
        version.GetFileVersionInfoW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
        version.GetFileVersionInfoW.restype = ctypes.c_int
        version.VerQueryValueW.argtypes = [
            ctypes.c_void_p,
            ctypes.c_wchar_p,
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(ctypes.c_uint),
        ]
        version.VerQueryValueW.restype = ctypes.c_int
        handle = ctypes.c_uint()
        size = version.GetFileVersionInfoSizeW(str(path), ctypes.byref(handle))
        if not size:
            return None
        buffer = ctypes.create_string_buffer(size)
        if not version.GetFileVersionInfoW(str(path), 0, size, buffer):
            return None
        pointer = ctypes.c_void_p()
        length = ctypes.c_uint()
        if version.VerQueryValueW(buffer, "\\VarFileInfo\\Translation", ctypes.byref(pointer), ctypes.byref(length)) and pointer.value:
            translations = (ctypes.c_ushort * (length.value // ctypes.sizeof(ctypes.c_ushort))).from_address(pointer.value)
            for index in range(0, len(translations) - 1, 2):
                language = translations[index]
                codepage = translations[index + 1]
                query = f"\\StringFileInfo\\{language:04x}{codepage:04x}\\FileVersion"
                value_pointer = ctypes.c_void_p()
                value_length = ctypes.c_uint()
                if version.VerQueryValueW(buffer, query, ctypes.byref(value_pointer), ctypes.byref(value_length)) and value_pointer.value:
                    value = ctypes.cast(value_pointer, ctypes.c_wchar_p).value
                    if value:
                        return value
        for query in (
            "\\StringFileInfo\\040904b0\\FileVersion",
            "\\StringFileInfo\\040904e4\\FileVersion",
            "\\StringFileInfo\\040904e0\\FileVersion",
        ):
            value_pointer = ctypes.c_void_p()
            value_length = ctypes.c_uint()
            if version.VerQueryValueW(buffer, query, ctypes.byref(value_pointer), ctypes.byref(value_length)) and value_pointer.value:
                value = ctypes.cast(value_pointer, ctypes.c_wchar_p).value
                if value:
                    return value
        if version.VerQueryValueW(buffer, "\\", ctypes.byref(pointer), ctypes.byref(length)) and pointer.value:
            fixed = ctypes.cast(pointer, ctypes.POINTER(ctypes.c_uint32 * 13)).contents
            major, minor = fixed[2] >> 16, fixed[2] & 0xFFFF
            build, revision = fixed[3] >> 16, fixed[3] & 0xFFFF
            return f"{major}.{minor}.{build}.{revision}"
        return None
    except (AttributeError, OSError, TypeError, ValueError):
        return None
