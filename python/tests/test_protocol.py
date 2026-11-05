from __future__ import annotations

import pytest

from superlotis import errors
from superlotis.protocol import encode_request, parse_kv, parse_list, parse_response


def test_encode_request() -> None:
    assert encode_request("ping") == b"ping\n"
    assert encode_request("exptime", 2.5) == b"exptime 2.5\n"
    assert encode_request("expose", exptime=5.0, object="M31") == b"expose exptime=5.0 object=M31\n"
    assert encode_request("dark", exptime=None) == b"dark\n"


@pytest.mark.parametrize("bad", ["", "a b", "a=b", "ünïcode"])
def test_encode_rejects_unsendable_values(bad: str) -> None:
    with pytest.raises(ValueError, match="cannot be sent"):
        encode_request("expose", object=bad)


def test_encode_rejects_bad_command() -> None:
    with pytest.raises(ValueError, match="invalid command"):
        encode_request("two words")


def test_parse_ok() -> None:
    assert parse_response("OK\n") == ""
    assert parse_response("OK 1.000\n") == "1.000"


@pytest.mark.parametrize(
    ("line", "exc"),
    [
        ("ERR busy an exposure is already in progress", errors.Busy),
        ("ERR out_of_range too big", errors.OutOfRange),
        ("ERR camera_error PICam: Acquire failed", errors.HardwareError),
        ("ERR unauthorized send auth", errors.Unauthorized),
        ("ERR brand_new_code something", errors.CommandError),
    ],
)
def test_parse_err_maps_to_exceptions(line: str, exc: type[Exception]) -> None:
    with pytest.raises(exc):
        parse_response(line)


def test_out_of_range_is_an_invalid_argument() -> None:
    with pytest.raises(errors.InvalidArgument):
        parse_response("ERR out_of_range nope")


def test_parse_garbage() -> None:
    with pytest.raises(errors.ProtocolError):
        parse_response("HELLO")


def test_parse_kv_decodes_values() -> None:
    d = parse_kv("file=/data/my%20dir/a%3Db.fits empty=- n=3")
    assert d == {"file": "/data/my dir/a=b.fits", "empty": "", "n": "3"}
    assert parse_list("a%2Cb,c") == ["a,b", "c"]
    with pytest.raises(errors.ProtocolError):
        parse_kv("novalue")
