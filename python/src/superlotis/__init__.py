"""Python client for the SuperLOTIS camera control server.

>>> from superlotis import CameraClient
>>> with CameraClient("localhost") as cam:          # doctest: +SKIP
...     cam.set_exptime(5)
...     print(cam.expose(object="M31"))
"""

from __future__ import annotations

__version__ = "0.1.0"

from .client import CameraClient, connect
from .errors import (
    BadRequest,
    Busy,
    CameraError,
    CommandError,
    ConnectionFailed,
    HardwareError,
    InvalidArgument,
    NotPermitted,
    OutOfRange,
    ProtocolError,
    StorageError,
    Unauthorized,
    UnknownCommand,
)
from .models import CameraInfo, Gain, ShutterMode, State, Status, Temperature

__all__ = [
    "BadRequest",
    "Busy",
    "CameraClient",
    "CameraError",
    "CameraInfo",
    "CommandError",
    "ConnectionFailed",
    "Gain",
    "HardwareError",
    "InvalidArgument",
    "NotPermitted",
    "OutOfRange",
    "ProtocolError",
    "ShutterMode",
    "State",
    "Status",
    "StorageError",
    "Temperature",
    "Unauthorized",
    "UnknownCommand",
    "__version__",
    "connect",
]
