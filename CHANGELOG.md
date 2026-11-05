# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-11-05

First release of the rewrite. Supersedes the prototype `camserver_cit`.

### Added
- `slcamd` C++17 server with a pluggable `CameraBackend` interface.
- `SimulatedBackend`: bias, dark current (temperature dependent), sky signal,
  read noise, gain, a first-order cooler model, and real-time or instant timing.
- `PicamBackend` for the Sophia 2048B-UV via PICam 5.x. Every SDK call is
  checked. Demo camera is opt-in only.
- Protocol v2: persistent connections, pipelining, `OK`/`ERR <code>` replies,
  percent-encoded `key=value` payloads, legacy `cmd=value` syntax accepted.
- Commands: `status`, `info`, `version`, `ping`, `setpoint`, `auth`, `quit`,
  `help`; `expose`/`dark` take `exptime=` and `object=` options.
- Multi-client server; `status` never blocks behind an exposure.
- Token authentication, client limit, idle timeout, max line length, loopback
  default bind, opt-in remote shutdown.
- Dependency-free FITS writer with atomic, no-overwrite writes and a full header.
- INI configuration with strict validation and `--check-config`; CLI overrides.
- systemd unit with hardening and graceful shutdown.
- Python package `superlotis-camera`: typed client, exception hierarchy
  mirroring server error codes, `slcam` CLI with `--json`, and
  `superlotis.testing.FakeCameraServer` for downstream tests.
- GoogleTest and pytest suites; GitHub Actions CI (gcc/clang, sanitizers,
  lint, type-checking, Python 3.9–3.13, client ↔ server integration).

### Fixed (relative to the prototype)
- `expose` and `dark` ignored the configured exposure time (hard-coded 6000 ms).
- `dark` did not close the shutter; `bias` used a 100 ms exposure in the current
  shutter mode instead of 0 s closed.
- `get_exposure_time` overwrote the value it read with the caller's
  uninitialized argument.
- `get_analog_gain` never wrote its output; `set_analog_gain` wrote back the
  current gain, so the gain could never change.
- `set_temp` only applied the set point when the sensor was already locked,
  ignored errors from the SDK, and reported success.
- FITS files were created as `SHORT_IMG` but written with `TUSHORT` data, so
  pixel values above 32767 overflowed.
- `resize_raw` built a padded buffer, never wrote it, and leaked it.
- `EXPTIME` was written in milliseconds; `DATE-OBS` recorded the conversion
  time rather than the exposure start.
- All frames were written to the same file name and overwritten.
- The server silently opened the SDK demo camera when no hardware was found.
- `status` and `genfits` were advertised in `help` but not implemented; `help`
  listed `gain` while the server only accepted `analog_gain`.
- Client input was passed to `printf` as a format string; several `printf`
  calls used mismatched format specifiers (undefined behaviour).
- The server listened on all interfaces with no authentication and accepted
  only one request at a time.
- `Picam_GetCameraID` was passed a pointer to the handle instead of the handle.
- The build depended on absolute paths, unused third-party SDK include
  directories, a gcc-4.8.2-specific `libgomp.a`, and a vendored CFITSIO 4.6.2
  header linked against CFITSIO 4.4.1.

### Removed
- Raw-file intermediate step and the `genfits` command (FITS is written directly).
- Vendored third-party headers and the redistributed SDK manual.
