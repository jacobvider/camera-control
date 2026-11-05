"""Client contract tests. Each runs against the fake AND the real server."""

from __future__ import annotations

import threading
import time

import pytest

from superlotis import (
    Busy,
    CameraClient,
    ConnectionFailed,
    Gain,
    InvalidArgument,
    NotPermitted,
    OutOfRange,
    ShutterMode,
    State,
    Unauthorized,
    UnknownCommand,
)

from .conftest import RunningServer, ServerFactory


def client(s: RunningServer, **kw: object) -> CameraClient:
    return CameraClient(s.host, s.port, timeout=10, readout_margin=10, **kw)  # type: ignore[arg-type]


def test_ping_version_info(server: RunningServer) -> None:
    with client(server) as cam:
        assert cam.ping()
        assert cam.version()["protocol"] == "2"
        info = cam.info()
        assert info.simulated
        assert info.width > 0
        assert info.height > 0


def test_settings_round_trip(server: RunningServer) -> None:
    with client(server) as cam:
        assert cam.set_exptime(2.5) == 2.5
        assert cam.get_exptime() == 2.5
        assert cam.set_shutter("closed") is ShutterMode.CLOSED
        assert cam.get_shutter() is ShutterMode.CLOSED
        assert cam.set_gain(Gain.LOW) is Gain.LOW
        assert cam.get_gain() is Gain.LOW
        assert cam.set_setpoint(-70) == -70.0
        assert cam.temperature().setpoint_c == -70.0


def test_status(server: RunningServer) -> None:
    with client(server) as cam:
        cam.set_exptime(3)
        st = cam.status()
        assert st.state is State.IDLE
        assert not st.busy
        assert st.exptime_s == 3.0
        assert st.frames_written == 0
        assert st.last_file is None


def test_exposures_produce_distinct_files(server: RunningServer) -> None:
    with client(server) as cam:
        files = [cam.expose(exptime=0.1, object="M31"), cam.dark(exptime=0.1), cam.bias()]
        files += cam.burst(2, exptime=0.1)
        assert len(set(files)) == 5
        assert all(f.endswith(".fits") for f in files)
        st = cam.status()
        assert st.frames_written == 5
        assert st.last_file == files[-1]


@pytest.mark.parametrize(
    ("call", "exc"),
    [
        (lambda c: c.set_exptime(-1), OutOfRange),
        (lambda c: c.set_exptime(10_000), OutOfRange),
        (lambda c: c.set_setpoint(-200), OutOfRange),
        (lambda c: c.request("exptime", "abc"), InvalidArgument),
        (lambda c: c.request("frobnicate"), UnknownCommand),
        (lambda c: c.burst(0), ValueError),
        (lambda c: c.shutdown_server(), NotPermitted),
    ],
)
def test_errors(server: RunningServer, call: object, exc: type[Exception]) -> None:
    with client(server) as cam:
        with pytest.raises(exc):
            call(cam)  # type: ignore[operator]
        assert cam.ping(), "connection must stay usable after an error"


def test_connection_refused() -> None:
    cam = CameraClient("127.0.0.1", 1, timeout=1)
    with pytest.raises(ConnectionFailed):
        cam.ping()


def test_token_authentication(make_server: ServerFactory) -> None:
    s = make_server(token="hunter2")
    with pytest.raises(Unauthorized):
        client(s).status()
    with pytest.raises(Unauthorized):
        client(s, token="wrong").connect()
    with client(s, token="hunter2") as cam:
        assert cam.status().state is State.IDLE


def test_status_from_second_client_while_exposing(make_server: ServerFactory) -> None:
    s = make_server(time_scale=1.0)
    result: dict[str, str] = {}

    def run() -> None:
        with client(s) as cam:
            result["file"] = cam.expose(exptime=1.5)

    t = threading.Thread(target=run)
    t.start()
    time.sleep(0.5)
    with client(s) as watcher:
        start = time.monotonic()
        st = watcher.status()
        assert time.monotonic() - start < 0.5
        assert st.state is State.EXPOSING
        with pytest.raises(Busy):
            watcher.expose(exptime=0.1)
        t.join(timeout=10)
        assert result["file"].endswith(".fits")
        assert watcher.wait_until_idle(timeout=5).state is State.IDLE


def test_remote_shutdown(make_server: ServerFactory) -> None:
    s = make_server(allow_shutdown=True)
    cam = client(s)
    cam.shutdown_server()
    time.sleep(0.5)
    with pytest.raises(ConnectionFailed):
        CameraClient(s.host, s.port, timeout=1).ping()
