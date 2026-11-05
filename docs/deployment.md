# Deploying on the camera PC

The PICam SDK is officially supported on a limited set of Linux distributions.
The SuperLOTIS camera PC runs **CentOS 7**, which has been end-of-life since
June 2024. Check the current PICam release notes before upgrading the OS. The
server itself only needs a C++17 compiler and CMake ≥ 3.16.

## 1. Install the PICam SDK

1. Download `picam_sdk.run` from Teledyne Princeton Instruments and run it as root.
   By default it installs to `/opt/PrincetonInstruments/picam`.
2. The Sophia connects over GigE. Allow traffic on the camera's Ethernet
   interface (for example, put it in the `trusted` firewalld zone):
   ```bash
   sudo firewall-cmd --permanent --zone=trusted --change-interface=<camera-nic>
   sudo firewall-cmd --reload
   ```

## 2. Toolchain on CentOS 7

The system compiler (gcc 4.8) predates C++17. Use Red Hat's devtoolset from
Software Collections:

```bash
sudo yum install -y centos-release-scl
sudo yum install -y devtoolset-11-gcc-c++ cmake3 ninja-build
scl enable devtoolset-11 bash
```

CentOS 7's mirrors moved to `vault.centos.org` after EOL, so you may need to
point the repo files there.

## 3. Build with the PICam backend

```bash
git clone https://github.com/jacobvider/superlotis-camera.git
cd superlotis-camera
cmake3 -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
       -DSUPERLOTIS_WITH_PICAM=ON -DPICAM_ROOT=/opt/PrincetonInstruments/picam
cmake3 --build build
(cd build && ctest3)              # runs against the simulator; no camera needed
build/server/slcamd --version      # should print "(protocol 2, picam)"
```

## 4. First light with the SDK demo camera

Before touching hardware, exercise the PICam code path with the SDK's virtual Sophia:

```bash
build/server/slcamd --backend picam --demo --data-dir /tmp/slcam-demo
# in another terminal
slcam info          # simulated=1, model from the SDK
slcam expose -t 1
```

Then power the camera and run without `--demo`. The server exits with an
error if no camera is found; it never falls back to the demo camera.

## 5. Install as a service

```bash
sudo cmake3 --install build --prefix /usr/local
sudo useradd --system --home /var/lib/superlotis --shell /sbin/nologin slcam
sudo install -d -o slcam -g slcam /var/lib/superlotis/data
sudo install -D -m 640 -o root -g slcam config/slcamd.example.conf /etc/superlotis/slcamd.conf
sudoedit /etc/superlotis/slcamd.conf     # backend = picam, bind_address, auth_token, ...
sudo cp deploy/systemd/slcamd.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now slcamd
journalctl -u slcamd -f
```

The unit runs `slcamd --check-config` before starting, restarts on failure,
and waits up to 5 minutes on stop so that an in-progress exposure can finish
and be written. The `slcam` user needs access to the camera's network
interface. It does not need root.

## 6. Network exposure

* Leave `bind_address = 127.0.0.1` if the scheduler runs on the camera PC.
* Otherwise bind to the private observatory interface, set a long random
  `auth_token` (`openssl rand -hex 24`), and restrict the port with the firewall
  to the scheduler host. The token is sent in clear text: treat it as
  protection against accidents on a trusted LAN, not against an attacker on
  the wire. For untrusted networks, tunnel over SSH
  (`ssh -L 6972:localhost:6972 camera-pc`).

## 7. Client machines

```bash
pip install "git+https://github.com/jacobvider/superlotis-camera.git#subdirectory=python"
export SLCAM_HOST=camera-pc SLCAM_TOKEN=...
slcam status
```

The client is pure Python (3.9+) with no dependencies.
