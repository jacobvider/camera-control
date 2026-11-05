"""``slcam`` command-line interface."""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Sequence
from dataclasses import asdict
from enum import Enum
from typing import Any

from . import __version__
from .client import DEFAULT_PORT, CameraClient
from .errors import CameraError, CommandError, ConnectionFailed


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="slcam",
        description="Control the SuperLOTIS camera server (slcamd).",
        epilog="Environment: SLCAM_HOST, SLCAM_PORT, SLCAM_TOKEN provide defaults.",
    )
    p.add_argument("--host", default=None, help="server host (default: $SLCAM_HOST or localhost)")
    p.add_argument("--port", type=int, default=None, help=f"server port (default: {DEFAULT_PORT})")
    p.add_argument("--token", default=None, help="auth token (default: $SLCAM_TOKEN)")
    p.add_argument("--timeout", type=float, default=10.0, help="command timeout in seconds")
    p.add_argument("--json", action="store_true", help="machine-readable JSON output")
    p.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    sub = p.add_subparsers(dest="command", required=True, metavar="COMMAND")

    sub.add_parser("ping", help="check that the server is reachable")
    sub.add_parser("info", help="camera model, serial number and geometry")
    sub.add_parser("status", help="current state, settings and temperature")
    sub.add_parser("temp", help="sensor temperature and lock status")

    s = sub.add_parser("exptime", help="get or set the exposure time (seconds)")
    s.add_argument("seconds", nargs="?", type=float)
    s = sub.add_parser("shutter", help="get or set the shutter mode")
    s.add_argument("mode", nargs="?", choices=["normal", "closed", "open"])
    s = sub.add_parser("gain", help="get or set the ADC analog gain")
    s.add_argument("level", nargs="?", choices=["low", "medium", "high"])
    s = sub.add_parser("setpoint", help="get or set the sensor temperature set point (C)")
    s.add_argument("celsius", nargs="?", type=float)

    s = sub.add_parser("expose", help="take a science frame")
    s.add_argument("-t", "--exptime", type=float, help="exposure time for this frame only")
    s.add_argument("-o", "--object", help="target name for the OBJECT header keyword")
    s = sub.add_parser("dark", help="take a dark frame (shutter closed)")
    s.add_argument("-t", "--exptime", type=float)
    sub.add_parser("bias", help="take a bias frame (0 s, shutter closed)")
    s = sub.add_parser("burst", help="take several science frames back to back")
    s.add_argument("count", type=int)
    s.add_argument("-t", "--exptime", type=float)
    s.add_argument("-o", "--object")

    s = sub.add_parser("raw", help="send a raw protocol line and print the reply payload")
    s.add_argument("line", nargs="+")
    sub.add_parser("shutdown", help="stop the server (if remote shutdown is enabled)")
    return p


def _jsonable(value: Any) -> Any:
    if isinstance(value, Enum):
        return value.value
    if hasattr(value, "__dataclass_fields__"):
        return {k: _jsonable(v) for k, v in asdict(value).items()}
    if isinstance(value, dict):
        return {k: _jsonable(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_jsonable(v) for v in value]
    return value


def _print(result: Any, as_json: bool) -> None:
    if as_json:
        print(json.dumps(_jsonable(result), indent=2, sort_keys=True))
        return
    if hasattr(result, "__dataclass_fields__"):
        for key, value in _jsonable(result).items():
            print(f"{key:>18}: {'' if value is None else value}")
    elif isinstance(result, list):
        for item in result:
            print(item)
    elif isinstance(result, Enum):
        print(result.value)
    else:
        print(result)


def _run(args: argparse.Namespace, cam: CameraClient) -> Any:
    cmd = args.command
    if cmd == "ping":
        return "pong" if cam.ping() else "unexpected reply"
    if cmd == "info":
        return cam.info()
    if cmd == "status":
        return cam.status()
    if cmd == "temp":
        return cam.temperature()
    if cmd == "exptime":
        return cam.get_exptime() if args.seconds is None else cam.set_exptime(args.seconds)
    if cmd == "shutter":
        return cam.get_shutter() if args.mode is None else cam.set_shutter(args.mode)
    if cmd == "gain":
        return cam.get_gain() if args.level is None else cam.set_gain(args.level)
    if cmd == "setpoint":
        if args.celsius is None:
            return cam.temperature().setpoint_c
        return cam.set_setpoint(args.celsius)
    if cmd == "expose":
        return cam.expose(exptime=args.exptime, object=args.object)
    if cmd == "dark":
        return cam.dark(exptime=args.exptime)
    if cmd == "bias":
        return cam.bias()
    if cmd == "burst":
        return cam.burst(args.count, exptime=args.exptime, object=args.object)
    if cmd == "raw":
        head, *rest = args.line
        return cam.request(head, *rest)
    if cmd == "shutdown":
        cam.shutdown_server()
        return "server is shutting down"
    raise AssertionError(f"unhandled command {cmd}")  # pragma: no cover


def main(argv: Sequence[str] | None = None) -> int:
    """Entry point. Returns 0 on success, 1 on command errors, 2 on connection errors."""
    import os

    args = _build_parser().parse_args(argv)
    host = args.host or os.environ.get("SLCAM_HOST", "localhost")
    port = args.port or int(os.environ.get("SLCAM_PORT", DEFAULT_PORT))
    try:
        with CameraClient(host, port, timeout=args.timeout, token=args.token) as cam:
            _print(_run(args, cam), args.json)
    except ConnectionFailed as exc:
        print(f"slcam: {exc}", file=sys.stderr)
        return 2
    except CommandError as exc:
        print(f"slcam: error: {exc}", file=sys.stderr)
        return 1
    except (CameraError, ValueError) as exc:
        print(f"slcam: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
