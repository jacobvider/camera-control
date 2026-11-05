"""In-process fake of slcamd for unit-testing code that uses :class:`CameraClient`.

It speaks the same protocol as the real server but keeps everything in
memory: no camera, no FITS files. Use it as a context manager::

    with FakeCameraServer() as server:
        cam = CameraClient("127.0.0.1", server.port)
        assert cam.expose().endswith(".fits")

The integration tests in this repository run the same client tests against
the real C++ server, which keeps the two implementations honest.
"""

from __future__ import annotations

import socketserver
import threading
import time
from dataclasses import dataclass, field
from types import TracebackType
from urllib.parse import quote

_LIMITS = {"exptime": (0.0, 240.0), "setpoint": (-80.0, 90.0)}
_SHUTTER = {
    "normal": "normal",
    "1": "normal",
    "closed": "closed",
    "2": "closed",
    "open": "open",
    "3": "open",
}
_GAIN = {"low": "low", "1": "low", "medium": "medium", "2": "medium", "high": "high", "3": "high"}


class _Err(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


def _enc(value: str) -> str:
    return quote(value, safe="/:._-+") if value else "-"


def _num(text: str, what: str) -> float:
    try:
        return float(text)
    except ValueError:
        raise _Err("invalid_argument", f"{what} must be a number, got '{text}'") from None


@dataclass
class FakeCameraState:
    """Mutable state of the fake camera; inspect it from your tests."""

    exptime: float = 1.0
    shutter: str = "normal"
    gain: str = "high"
    setpoint: float = -60.0
    reading: float = -60.0
    state: str = "idle"
    frames: int = 0
    last_file: str = ""
    token: str = ""
    allow_shutdown: bool = False
    time_scale: float = 0.0
    """Fraction of the real exposure time to sleep (0 = return immediately)."""
    log: list[str] = field(default_factory=list)
    """Every request line received, in order."""
    lock: threading.Lock = field(default_factory=threading.Lock, repr=False)


class _Handler(socketserver.StreamRequestHandler):
    server: _TCPServer

    def handle(self) -> None:
        authed = False
        for raw in self.rfile:
            line = raw.decode("ascii", "replace").strip()
            if not line:
                continue
            fake = self.server.fake
            with fake.lock:
                fake.log.append(line)
            try:
                payload, close, authed = self._dispatch(line, authed)
                reply = "OK" + (f" {payload}" if payload else "")
            except _Err as e:
                reply, close = f"ERR {e.code} {e.message}", False
            self.wfile.write((reply + "\n").encode("ascii"))
            self.wfile.flush()
            if close:
                return

    def _dispatch(self, line: str, authed: bool) -> tuple[str, bool, bool]:
        s = self.server.fake
        tokens = line.split()
        head = tokens[0]
        if "=" in head:
            cmd, _, arg = head.partition("=")
            pos = [arg] if arg else []
        else:
            cmd, pos = head, []
        cmd = cmd.lower()
        opts: dict[str, str] = {}
        for t in tokens[1:]:
            if "=" in t:
                k, _, v = t.partition("=")
                opts[k.lower()] = v
            else:
                pos.append(t)

        if cmd == "ping":
            return "pong", False, authed
        if cmd == "quit":
            return "bye", True, authed
        if cmd == "auth":
            if not s.token:
                return "auth=disabled", False, authed
            if pos != [s.token]:
                raise _Err("unauthorized", "invalid token")
            return "authenticated", False, True
        if s.token and not authed:
            raise _Err("unauthorized", "send 'auth <token>' first")

        if cmd == "version":
            return "version=0.1.0 protocol=2", False, authed
        if cmd == "info":
            return (
                "model=Fake%20Camera serial=FAKE0001 sensor=fake width=2048 height=2048 "
                "simulated=1 backend=fake version=0.1.0",
                False,
                authed,
            )
        if cmd == "status":
            with s.lock:
                return (
                    f"state={s.state} exptime={s.exptime:.3f} shutter={s.shutter} gain={s.gain} "
                    f"temp={s.reading:.2f} setpoint={s.setpoint:.2f} locked=1 temp_age=0.0 "
                    f"progress=0.000 elapsed=0.00 frames={s.frames} last_file={_enc(s.last_file)} "
                    "last_error=-",
                    False,
                    authed,
                )
        if cmd == "exptime":
            if pos:
                seconds = _num(pos[0], "exptime")
                lo, hi = _LIMITS["exptime"]
                if not lo <= seconds <= hi:
                    raise _Err("out_of_range", f"exposure time must be between {lo} and {hi} s")
                s.exptime = seconds
            return f"{s.exptime:.3f}", False, authed
        if cmd in ("shutter", "shutter_mode"):
            if pos:
                if pos[0].lower() not in _SHUTTER:
                    raise _Err("invalid_argument", "shutter must be normal, closed or open")
                s.shutter = _SHUTTER[pos[0].lower()]
            return s.shutter, False, authed
        if cmd in ("gain", "analog_gain"):
            if pos:
                if pos[0].lower() not in _GAIN:
                    raise _Err("invalid_argument", "gain must be low, medium or high")
                self._require_idle()
                s.gain = _GAIN[pos[0].lower()]
            return s.gain, False, authed
        if cmd in ("temp", "setpoint"):
            if pos:
                celsius = _num(pos[0], "set point")
                lo, hi = _LIMITS["setpoint"]
                if not lo <= celsius <= hi:
                    raise _Err(
                        "out_of_range", f"temperature set point must be between {lo} and {hi} C"
                    )
                self._require_idle()
                s.setpoint = s.reading = celsius
            if cmd == "setpoint":
                return f"{s.setpoint:.2f}", False, authed
            return f"reading={s.reading:.2f} setpoint={s.setpoint:.2f} locked=1", False, authed
        if cmd in ("expose", "dark", "bias"):
            ftype = {"expose": "object", "dark": "dark", "bias": "bias"}[cmd]
            exptime = 0.0 if cmd == "bias" else _num(opts.get("exptime", str(s.exptime)), "exptime")
            return f"file={_enc(self._expose(ftype, exptime))} type={ftype}", False, authed
        if cmd == "burst":
            if not pos:
                raise _Err("invalid_argument", "usage: burst <count> [exptime=S] [object=NAME]")
            try:
                n = int(pos[0])
            except ValueError:
                raise _Err("invalid_argument", "burst count must be an integer") from None
            if not 1 <= n <= 100:
                raise _Err("out_of_range", "burst count must be between 1 and 100")
            exptime = _num(opts.get("exptime", str(s.exptime)), "exptime")
            files = [_enc(self._expose("object", exptime)) for _ in range(n)]
            return f"count={n} files={','.join(files)}", False, authed
        if cmd in ("shutdown", "exit"):
            if not s.allow_shutdown:
                raise _Err("not_permitted", "remote shutdown is disabled")
            threading.Thread(target=self.server.shutdown, daemon=True).start()
            return "shutting down", True, authed
        raise _Err("unknown_command", f"unknown command '{cmd}' (try 'help')")

    def _require_idle(self) -> None:
        if self.server.fake.state != "idle":
            raise _Err("busy", "an exposure is in progress")

    def _expose(self, ftype: str, exptime: float) -> str:
        s = self.server.fake
        lo, hi = _LIMITS["exptime"]
        if not lo <= exptime <= hi:
            raise _Err("out_of_range", f"exposure time must be between {lo} and {hi} s")
        with s.lock:
            if s.state != "idle":
                raise _Err("busy", "an exposure is already in progress")
            s.state = "exposing"
        try:
            if s.time_scale > 0:
                time.sleep(exptime * s.time_scale)
            with s.lock:
                s.frames += 1
                s.last_file = f"/fake/data/fake_{s.frames:04d}_{ftype}.fits"
                return s.last_file
        finally:
            with s.lock:
                s.state = "idle"


class _TCPServer(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True
    fake: FakeCameraState


class FakeCameraServer:
    """Runs a fake slcamd on ``127.0.0.1`` in a background thread."""

    def __init__(
        self, *, token: str = "", allow_shutdown: bool = False, time_scale: float = 0.0
    ) -> None:
        self.state = FakeCameraState(
            token=token, allow_shutdown=allow_shutdown, time_scale=time_scale
        )
        self._server = _TCPServer(("127.0.0.1", 0), _Handler)
        self._server.fake = self.state
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)

    @property
    def port(self) -> int:
        return int(self._server.server_address[1])

    def start(self) -> FakeCameraServer:
        self._thread.start()
        return self

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()

    def __enter__(self) -> FakeCameraServer:
        return self.start()

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        self.stop()
