# superlotis-camera (Python client)

A typed, dependency-free Python client and `slcam` command-line tool for
[`slcamd`](https://github.com/jacobvider/superlotis-camera), the SuperLOTIS
Sophia 2048B-UV camera server.

```bash
pip install "git+https://github.com/jacobvider/superlotis-camera.git#subdirectory=python"
```

## Library

```python
from superlotis import Busy, CameraClient

with CameraClient("camera-pc", token="...") as cam:  # or SLCAM_TOKEN env var
    print(cam.info())
    cam.set_exptime(30)
    cam.set_gain("medium")
    cam.set_setpoint(-60)

    path = cam.expose(object="M31")  # blocks until the FITS file is written
    darks = [cam.dark(exptime=30) for _ in range(5)]
    biases = [cam.bias() for _ in range(3)]

    st = cam.status()
    print(st.state, st.temperature_c, st.locked, st.last_file)
```

Server errors become exceptions that match the server's error codes:
`Busy`, `OutOfRange`, `InvalidArgument`, `HardwareError`, `StorageError`,
`Unauthorized`, ... (all subclasses of `superlotis.CameraError`). Network
problems raise `ConnectionFailed`.

To watch an exposure in progress, poll `status()` from a **second** client:

```python
with CameraClient("camera-pc") as watcher:
    watcher.wait_until_idle(poll=1.0)
```

## CLI

```console
$ export SLCAM_HOST=camera-pc
$ slcam status
$ slcam exptime 30
$ slcam expose -o GRB261105A
$ slcam burst 5 -t 10
$ slcam --json temp
```

Exit status: `0` success, `1` the server rejected the command, `2` connection failure.

## Testing your own code

`superlotis.testing.FakeCameraServer` runs an in-memory fake of `slcamd`:

```python
from superlotis import CameraClient
from superlotis.testing import FakeCameraServer


def test_my_observing_script():
    with FakeCameraServer() as server:
        cam = CameraClient("127.0.0.1", server.port)
        run_my_script(cam)
        assert server.state.frames == 3
        assert "expose object=M31" in server.state.log
```
