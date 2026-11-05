"""Typed views of server replies."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

from .errors import ProtocolError


class ShutterMode(str, Enum):
    NORMAL = "normal"
    CLOSED = "closed"
    OPEN = "open"


class Gain(str, Enum):
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"


class State(str, Enum):
    IDLE = "idle"
    EXPOSING = "exposing"
    WRITING = "writing"


def _float(d: dict[str, str], key: str) -> float:
    try:
        return float(d[key])
    except (KeyError, ValueError) as exc:
        raise ProtocolError(f"missing or invalid field {key!r} in reply") from exc


def _int(d: dict[str, str], key: str) -> int:
    try:
        return int(d[key])
    except (KeyError, ValueError) as exc:
        raise ProtocolError(f"missing or invalid field {key!r} in reply") from exc


def _bool(d: dict[str, str], key: str) -> bool:
    return _int(d, key) != 0


@dataclass(frozen=True)
class Temperature:
    reading_c: float
    setpoint_c: float
    locked: bool

    @classmethod
    def from_kv(cls, d: dict[str, str]) -> Temperature:
        return cls(_float(d, "reading"), _float(d, "setpoint"), _bool(d, "locked"))


@dataclass(frozen=True)
class CameraInfo:
    model: str
    serial: str
    sensor: str
    width: int
    height: int
    simulated: bool
    backend: str
    server_version: str

    @classmethod
    def from_kv(cls, d: dict[str, str]) -> CameraInfo:
        return cls(
            model=d.get("model", ""),
            serial=d.get("serial", ""),
            sensor=d.get("sensor", ""),
            width=_int(d, "width"),
            height=_int(d, "height"),
            simulated=_bool(d, "simulated"),
            backend=d.get("backend", ""),
            server_version=d.get("version", ""),
        )


@dataclass(frozen=True)
class Status:
    state: State
    exptime_s: float
    shutter: ShutterMode
    gain: Gain
    temperature_c: float
    setpoint_c: float
    locked: bool
    temperature_age_s: float
    progress: float
    elapsed_s: float
    frames_written: int
    last_file: str | None
    last_error: str | None

    @property
    def busy(self) -> bool:
        return self.state is not State.IDLE

    @classmethod
    def from_kv(cls, d: dict[str, str]) -> Status:
        try:
            state = State(d["state"])
            shutter = ShutterMode(d["shutter"])
            gain = Gain(d["gain"])
        except (KeyError, ValueError) as exc:
            raise ProtocolError(f"invalid status reply: {d}") from exc
        return cls(
            state=state,
            exptime_s=_float(d, "exptime"),
            shutter=shutter,
            gain=gain,
            temperature_c=_float(d, "temp"),
            setpoint_c=_float(d, "setpoint"),
            locked=_bool(d, "locked"),
            temperature_age_s=_float(d, "temp_age"),
            progress=_float(d, "progress"),
            elapsed_s=_float(d, "elapsed"),
            frames_written=_int(d, "frames"),
            last_file=d.get("last_file") or None,
            last_error=d.get("last_error") or None,
        )
