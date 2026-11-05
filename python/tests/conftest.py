"""Fixtures that run the same tests against the fake server and the real slcamd.

The real server is used when the ``SLCAMD_BIN`` environment variable points at
a built ``slcamd`` (CI does this), or when ``../build/server/slcamd`` exists.
"""

from __future__ import annotations

import os
import re
import subprocess
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from pathlib import Path

import pytest

from superlotis.testing import FakeCameraServer

REPO = Path(__file__).resolve().parents[2]


def slcamd_binary() -> Path | None:
    env = os.environ.get("SLCAMD_BIN")
    if env:
        return Path(env)
    default = REPO / "build" / "server" / "slcamd"
    return default if default.exists() else None


@dataclass
class RunningServer:
    kind: str
    host: str
    port: int
    data_dir: Path | None


class _RealServer:
    def __init__(
        self, binary: Path, data_dir: Path, token: str, allow_shutdown: bool, time_scale: float
    ):
        args = [
            str(binary),
            "--port",
            "0",
            "--data-dir",
            str(data_dir),
            "--log-level",
            "info",
            "--set",
            f"simulator.time_scale={time_scale}",
            "--set",
            "simulator.width=64",
            "--set",
            "simulator.height=48",
            "--set",
            "simulator.readout_s=0",
            "--set",
            "simulator.seed=1",
            "--set",
            f"server.allow_remote_shutdown={'true' if allow_shutdown else 'false'}",
        ]
        if token:
            args += ["--set", f"server.auth_token={token}"]
        self.proc = subprocess.Popen(args, stderr=subprocess.PIPE, text=True)
        assert self.proc.stderr is not None
        self.port = 0
        for line in self.proc.stderr:
            m = re.search(r"Listening on \S+:(\d+)", line)
            if m:
                self.port = int(m.group(1))
                break
        if not self.port:
            self.proc.kill()
            raise RuntimeError("slcamd did not start")

    def stop(self) -> None:
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        if self.proc.stderr:
            self.proc.stderr.close()


ServerFactory = Callable[..., RunningServer]


def _backends() -> list[object]:
    params: list[object] = ["fake"]
    params.append(
        pytest.param(
            "real",
            marks=[
                pytest.mark.integration,
                pytest.mark.skipif(
                    slcamd_binary() is None, reason="slcamd not built (set SLCAMD_BIN)"
                ),
            ],
        )
    )
    return params


@pytest.fixture(params=_backends())
def make_server(request: pytest.FixtureRequest, tmp_path: Path) -> Iterator[ServerFactory]:
    """Factory: ``make_server(token=..., allow_shutdown=..., time_scale=...)``."""
    kind: str = request.param
    started: list[object] = []

    def factory(
        token: str = "", allow_shutdown: bool = False, time_scale: float = 0.0
    ) -> RunningServer:
        if kind == "fake":
            fake = FakeCameraServer(
                token=token, allow_shutdown=allow_shutdown, time_scale=time_scale
            ).start()
            started.append(fake)
            return RunningServer("fake", "127.0.0.1", fake.port, None)
        binary = slcamd_binary()
        assert binary is not None
        data = tmp_path / f"data{len(started)}"
        real = _RealServer(binary, data, token, allow_shutdown, time_scale)
        started.append(real)
        return RunningServer("real", "127.0.0.1", real.port, data)

    yield factory
    for s in started:
        s.stop()  # type: ignore[attr-defined]


@pytest.fixture
def server(make_server: ServerFactory) -> RunningServer:
    return make_server()
