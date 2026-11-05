"""Exception hierarchy for the SuperLOTIS camera client."""

from __future__ import annotations


class CameraError(Exception):
    """Base class for every error raised by this package."""


class ConnectionFailed(CameraError):
    """The server could not be reached, or the connection dropped."""


class ProtocolError(CameraError):
    """The server sent something that does not follow the protocol."""


class CommandError(CameraError):
    """The server rejected a command (an ``ERR <code> <message>`` reply)."""

    code: str = "error"

    def __init__(self, message: str, code: str | None = None) -> None:
        super().__init__(message)
        if code is not None:
            self.code = code
        self.message = message

    def __str__(self) -> str:
        return f"{self.code}: {self.message}"


class BadRequest(CommandError):
    code = "bad_request"


class UnknownCommand(CommandError):
    code = "unknown_command"


class InvalidArgument(CommandError):
    code = "invalid_argument"


class OutOfRange(InvalidArgument):
    code = "out_of_range"


class Busy(CommandError):
    """Another exposure is in progress (retry later)."""

    code = "busy"


class HardwareError(CommandError):
    """The camera or SDK reported a failure."""

    code = "camera_error"


class StorageError(CommandError):
    """The server could not write the FITS file."""

    code = "storage_error"


class Unauthorized(CommandError):
    code = "unauthorized"


class NotPermitted(CommandError):
    code = "not_permitted"


class ServerInternalError(CommandError):
    code = "internal_error"


_BY_CODE: dict[str, type[CommandError]] = {
    cls.code: cls
    for cls in (
        BadRequest,
        UnknownCommand,
        InvalidArgument,
        OutOfRange,
        Busy,
        HardwareError,
        StorageError,
        Unauthorized,
        NotPermitted,
        ServerInternalError,
    )
}


def error_for(code: str, message: str) -> CommandError:
    """Return the most specific exception for a server error code."""
    cls = _BY_CODE.get(code)
    return cls(message) if cls is not None else CommandError(message, code=code)
