# Migrating from the original camera server

This project replaces the prototype `camserver_cit` server (`camera.cpp`,
`server.c`, `camserver.c`). The table maps the old commands to the new ones.

| Original (`echo … \| nc localhost 6972`) | Now | Notes |
|---|---|---|
| `exptime=5000` (ms) | `exptime 5` (s) | Units changed to seconds (the FITS convention). Old millisecond values above 240 are rejected, so a stale script fails loudly. |
| `exptime` | `exptime` | Getter now returns the real value (it used to return uninitialized memory). |
| `shutter_mode=2` | `shutter closed` (or `shutter 2`) | |
| `analog_gain=3` | `gain high` (or `gain 3`) | Setting the gain now actually changes it. |
| `temp=-60` | `setpoint -60` (or `temp -60`) | Works whether or not the sensor is currently locked. |
| `temp` | `temp` | Returns reading, set point and lock status. |
| `expose` | `expose [exptime=S] [object=NAME]` | Uses the configured exposure time (it was hardcoded to 6 s) and returns the FITS path. |
| `dark` | `dark [exptime=S]` | Shutter is now actually closed. |
| `bias` | `bias` | 0 s with the shutter closed (was 100 ms with the shutter in whatever mode). |
| `burst=5` | `burst 5` | Returns every file name. Stops and reports on the first failure. |
| `status` | `status` | Implemented. Works during an exposure. |
| `genfits` | (not needed) | FITS is written directly. There is no raw intermediate file. |
| `exit` | `shutdown` | Disabled unless `server.allow_remote_shutdown = true`. |
| (none) | `info`, `version`, `ping`, `auth`, `quit`, `help` | |

## Files

| Old | New |
|---|---|
| `exposure_file`, `dark_file`, `bias_file` in the working directory (overwritten each time) | `<data_dir>/<YYYYMMDD>/<prefix>_<UTC timestamp>_<type>_<seq>.fits` (never overwritten) |
| Header: `TEMPERAT`, `EXPTIME` (ms), `GAIN` (int), `DATE-OBS` (conversion time) | `CCD-TEMP`, `SET-TEMP`, `TEMPLOCK`, `EXPTIME` (s), `GAINMODE`, `SHUTTER`, `IMAGETYP`, `OBJECT`, `DATE-OBS` (exposure start), `DATE`, `INSTRUME`, `TELESCOP`, `CAMMODEL`, `CAMSERNO`, `SIMULATE`, `FRAMENUM`, `FILENAME`, `ORIGIN` |

## Python

```python
# before (client_testing.py, Python 2)
s.sendall('expose'); print s.recv(4096)

# now
from superlotis import CameraClient
with CameraClient("camera-pc") as cam:
    path = cam.expose(exptime=30, object="M31")
```
