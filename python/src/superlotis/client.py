"""Blocking TCP client for slcamd."""

from __future__ import annotations

import os
import socket
import threading
import time
from collections.abc import Iterator
from contextlib import contextmanager
from types import TracebackType

from .errors import Busy, ConnectionFailed, ProtocolError
from .models import CameraInfo, Gain, ShutterMode, Status, Temperature
from .protocol import MAX_LINE, encode_request, parse_kv, parse_list, parse_response

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 6972


class CameraClient:
    """A connection to the SuperLOTIS camera server.

    The connection is opened lazily and kept open between calls. Instances are
    thread-safe (requests are serialized), but because the server handles one
    request per connection at a time, use a *second* client if you want to poll
    :meth:`status` while another thread waits on :meth:`expose`.

    Example::

        with CameraClient("camera-pc", token="...") as cam:
            cam.set_exptime(30)
            path = cam.expose(object="M31")

    Args:
        host: Server host name or address.
        port: Server TCP port.
        timeout: Socket timeout in seconds for ordinary commands.
        token: Shared secret, if the server has ``server.auth_token`` set.
            Defaults to the ``SLCAM_TOKEN`` environment variable.
        readout_margin: Extra seconds allowed on top of the exposure time
            when waiting for an exposure to finish.
    """

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        *,
        timeout: float = 10.0,
        token: str | None = None,
        readout_margin: float = 60.0,
    ) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self.token = token if token is not None else os.environ.get("SLCAM_TOKEN") or None
        self.readout_margin = readout_margin
        self._sock: socket.socket | None = None
        self._buffer = b""
        self._lock = threading.RLock()

    # -- connection management -------------------------------------------------

    def connect(self) -> None:
        """Open the connection (and authenticate) if it is not already open."""
        with self._lock:
            if self._sock is not None:
                return
            try:
                self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
            except OSError as exc:
                raise ConnectionFailed(f"cannot connect to {self.host}:{self.port}: {exc}") from exc
            self._buffer = b""
            if self.token:
                try:
                    self._call_raw(encode_request("auth", self.token), self.timeout)
                except Exception:
                    self._drop()
                    raise

    def close(self) -> None:
        """Close the connection. Safe to call more than once."""
        with self._lock:
            if self._sock is None:
                return
            try:
                self._sock.sendall(b"quit\n")
            except OSError:
                pass
            finally:
                self._sock.close()
                self._sock = None

    def __enter__(self) -> CameraClient:
        self.connect()
        return self

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        self.close()

    def __repr__(self) -> str:
        state = "connected" if self._sock else "disconnected"
        return f"CameraClient({self.host!r}, {self.port}, {state})"

    # -- low-level request/response -------------------------------------------

    def request(
        self, command: str, *args: object, timeout: float | None = None, **options: object
    ) -> str:
        """Send one command and return the ``OK`` payload (raises on ``ERR``)."""
        return self._call_raw(encode_request(command, *args, **options), timeout or self.timeout)

    def _call_raw(self, line: bytes, timeout: float) -> str:
        with self._lock:
            if self._sock is None:
                self.connect()
            assert self._sock is not None
            try:
                self._sock.settimeout(timeout)
                self._sock.sendall(line)
                reply = self._readline()
            except socket.timeout as exc:
                self._drop()
                raise ConnectionFailed(
                    f"timed out after {timeout:.1f} s waiting for the server"
                ) from exc
            except OSError as exc:
                self._drop()
                raise ConnectionFailed(
                    f"connection to {self.host}:{self.port} failed: {exc}"
                ) from exc
            return parse_response(reply)

    def _readline(self) -> str:
        assert self._sock is not None
        while b"\n" not in self._buffer:
            chunk = self._sock.recv(4096)
            if not chunk:
                self._drop()
                raise ConnectionFailed("server closed the connection")
            self._buffer += chunk
            if len(self._buffer) > 64 * MAX_LINE:
                self._drop()
                raise ProtocolError("reply too long")
        line, _, self._buffer = self._buffer.partition(b"\n")
        try:
            return line.decode("ascii")
        except UnicodeDecodeError as exc:
            raise ProtocolError("reply is not ASCII") from exc

    def _drop(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None
        self._buffer = b""

    # -- commands ---------------------------------------------------------------

    def ping(self) -> bool:
        return self.request("ping") == "pong"

    def version(self) -> dict[str, str]:
        return parse_kv(self.request("version"))

    def info(self) -> CameraInfo:
        return CameraInfo.from_kv(parse_kv(self.request("info")))

    def status(self) -> Status:
        return Status.from_kv(parse_kv(self.request("status")))

    def get_exptime(self) -> float:
        return float(self.request("exptime"))

    def set_exptime(self, seconds: float) -> float:
        return float(self.request("exptime", float(seconds)))

    def get_shutter(self) -> ShutterMode:
        return ShutterMode(self.request("shutter"))

    def set_shutter(self, mode: ShutterMode | str) -> ShutterMode:
        return ShutterMode(self.request("shutter", ShutterMode(mode).value))

    def get_gain(self) -> Gain:
        return Gain(self.request("gain"))

    def set_gain(self, gain: Gain | str) -> Gain:
        return Gain(self.request("gain", Gain(gain).value))

    def temperature(self) -> Temperature:
        return Temperature.from_kv(parse_kv(self.request("temp")))

    def set_setpoint(self, celsius: float) -> float:
        return float(self.request("setpoint", float(celsius)))

    def _exposure_timeout(self, exptime: float | None, frames: int = 1) -> float:
        seconds = exptime if exptime is not None else self.get_exptime()
        return frames * (seconds + self.readout_margin) + self.timeout

    def expose(self, *, exptime: float | None = None, object: str | None = None) -> str:
        """Take a science frame and return the path of the FITS file on the server.

        ``exptime`` overrides the current exposure time for this frame only.
        """
        timeout = self._exposure_timeout(exptime)
        reply = self.request(
            "expose",
            timeout=timeout,
            exptime=None if exptime is None else float(exptime),
            object=object,
        )
        return parse_kv(reply)["file"]

    def dark(self, *, exptime: float | None = None) -> str:
        """Take a dark frame (shutter closed)."""
        timeout = self._exposure_timeout(exptime)
        reply = self.request(
            "dark", timeout=timeout, exptime=None if exptime is None else float(exptime)
        )
        return parse_kv(reply)["file"]

    def bias(self) -> str:
        """Take a zero-second bias frame (shutter closed)."""
        return parse_kv(self.request("bias", timeout=self.timeout + self.readout_margin))["file"]

    def burst(
        self, count: int, *, exptime: float | None = None, object: str | None = None
    ) -> list[str]:
        """Take ``count`` science frames back to back; returns the file paths."""
        if count < 1:
            raise ValueError("count must be >= 1")
        timeout = self._exposure_timeout(exptime, frames=count)
        reply = self.request(
            "burst",
            int(count),
            timeout=timeout,
            exptime=None if exptime is None else float(exptime),
            object=object,
        )
        return parse_list(parse_kv(reply)["files"])

    def shutdown_server(self) -> None:
        """Ask the server to exit (requires ``server.allow_remote_shutdown``)."""
        self.request("shutdown")
        self._drop()

    def wait_until_idle(self, *, poll: float = 0.5, timeout: float = 600.0) -> Status:
        """Poll :meth:`status` until no exposure is running."""
        deadline = time.monotonic() + timeout
        while True:
            st = self.status()
            if not st.busy:
                return st
            if time.monotonic() > deadline:
                raise Busy(f"camera still {st.state.value} after {timeout:.0f} s")
            time.sleep(poll)


@contextmanager
def connect(
    host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, **kwargs: object
) -> Iterator[CameraClient]:
    """``with connect(...) as cam:`` shorthand for :class:`CameraClient`."""
    client = CameraClient(host, port, **kwargs)  # type: ignore[arg-type]
    try:
        client.connect()
        yield client
    finally:
        client.close()
