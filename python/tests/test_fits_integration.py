"""Validates FITS files written by the real slcamd with astropy."""

from __future__ import annotations

from pathlib import Path

import pytest

from superlotis import CameraClient

from .conftest import RunningServer

fits = pytest.importorskip("astropy.io.fits")
np = pytest.importorskip("numpy")


def test_fits_files_are_valid_and_complete(server: RunningServer) -> None:
    if server.kind != "real":
        pytest.skip("the fake server writes no files")
    with CameraClient(server.host, server.port) as cam:
        cam.set_gain("medium")
        obj = Path(cam.expose(exptime=2.0, object="NGC7000"))
        dark = Path(cam.dark(exptime=2.0))
        bias = Path(cam.bias())

    with fits.open(obj) as hdul:
        hdul.verify("exception")
        h, data = hdul[0].header, hdul[0].data
        assert data.dtype == np.uint16
        assert data.shape == (48, 64)
        assert h["EXPTIME"] == pytest.approx(2.0)
        assert h["IMAGETYP"] == "OBJECT"
        assert h["OBJECT"] == "NGC7000"
        assert h["GAINMODE"] == "medium"
        assert h["SHUTTER"] == "normal"
        assert h["SIMULATE"] is True
        assert "DATE-OBS" in h
        assert h["FILENAME"] == obj.name

    with fits.open(dark) as hdul:
        assert hdul[0].header["IMAGETYP"] == "DARK"
        assert hdul[0].header["SHUTTER"] == "closed"
    with fits.open(bias) as hdul:
        assert hdul[0].header["IMAGETYP"] == "BIAS"
        assert hdul[0].header["EXPTIME"] == 0.0
        bias_mean = float(hdul[0].data.mean())
    with fits.open(obj) as hdul:
        assert float(hdul[0].data.mean()) > bias_mean, "open-shutter frame should collect signal"
