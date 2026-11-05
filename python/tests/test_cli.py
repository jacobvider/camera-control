from __future__ import annotations

import json

import pytest

from superlotis.cli import main

from .conftest import RunningServer


def run(server: RunningServer, *args: str) -> list[str]:
    return ["--host", server.host, "--port", str(server.port), *args]


def test_cli_get_and_set(server: RunningServer, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(run(server, "exptime", "4")) == 0
    assert capsys.readouterr().out.strip() == "4.0"
    assert main(run(server, "gain", "medium")) == 0
    assert capsys.readouterr().out.strip() == "medium"


def test_cli_json_status(server: RunningServer, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(run(server, "--json", "status")) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["state"] == "idle"
    assert "temperature_c" in data


def test_cli_expose(server: RunningServer, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(run(server, "expose", "-t", "0.1", "-o", "M31")) == 0
    assert capsys.readouterr().out.strip().endswith(".fits")


def test_cli_exit_codes(server: RunningServer, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(run(server, "exptime", "9999")) == 1
    assert "out_of_range" in capsys.readouterr().err
    assert main(["--host", "127.0.0.1", "--port", "1", "ping"]) == 2


def test_cli_raw(server: RunningServer, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(run(server, "raw", "ping")) == 0
    assert capsys.readouterr().out.strip() == "pong"
