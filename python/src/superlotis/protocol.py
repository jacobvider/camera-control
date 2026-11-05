"""Wire-format helpers for protocol v2 (see docs/protocol.md)."""

from __future__ import annotations

from urllib.parse import unquote

from .errors import ProtocolError, error_for

PROTOCOL_VERSION = 2
MAX_LINE = 4096
_FORBIDDEN = set(" \t\r\n=")


def encode_request(command: str, *args: object, **options: object) -> bytes:
    """Build one request line, e.g. ``encode_request("expose", exptime=5)``.

    Options whose value is ``None`` are omitted. Values may not contain
    whitespace or ``=``; that is rejected here rather than on the server.
    """
    if not command or any(c in _FORBIDDEN for c in command):
        raise ValueError(f"invalid command name: {command!r}")
    parts = [command]
    for value in args:
        parts.append(_token(value))
    for key, value in options.items():
        if value is None:
            continue
        parts.append(f"{key}={_token(value)}")
    line = " ".join(parts) + "\n"
    if len(line) > MAX_LINE:
        raise ValueError("request is longer than the protocol allows")
    return line.encode("ascii")


def _token(value: object) -> str:
    if isinstance(value, bool):
        text = "1" if value else "0"
    elif isinstance(value, float):
        text = repr(value)
    else:
        text = str(value)
    if not text or any(c in _FORBIDDEN for c in text) or not text.isascii():
        raise ValueError(f"argument {value!r} cannot be sent (empty, whitespace, '=' or non-ASCII)")
    return text


def parse_response(line: str) -> str:
    """Return the payload of an ``OK`` reply or raise the matching CommandError."""
    line = line.rstrip("\r\n")
    if line == "OK":
        return ""
    if line.startswith("OK "):
        return line[3:]
    if line.startswith("ERR "):
        code, _, message = line[4:].partition(" ")
        raise error_for(code, message or code)
    raise ProtocolError(f"unexpected reply from server: {line!r}")


def parse_kv(payload: str) -> dict[str, str]:
    """Parse ``k1=v1 k2=v2`` into a dict, percent-decoding values (``-`` = empty)."""
    out: dict[str, str] = {}
    for item in payload.split():
        key, sep, value = item.partition("=")
        if not sep:
            raise ProtocolError(f"expected key=value, got {item!r}")
        out[key] = "" if value == "-" else unquote(value)
    return out


def parse_list(value: str) -> list[str]:
    """Split a comma-separated list of percent-encoded values."""
    return [unquote(v) for v in value.split(",") if v]
