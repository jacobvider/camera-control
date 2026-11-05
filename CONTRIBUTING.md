# Contributing

## Setup

```bash
cmake -S . -B build -G Ninja -DSUPERLOTIS_WERROR=ON
cmake --build build
pip install -e "python[dev]"
```

## Before opening a pull request

```bash
make test        # C++ + Python (the Python suite also runs against build/server/slcamd)
make lint        # clang-format, ruff, mypy --strict
make sanitize    # optional locally; CI runs ASan/UBSan and TSan
```

## Guidelines

- **Bug fixes come with a regression test.** Controller behaviour can be tested
  with `SpyBackend` (`server/tests/spy_backend.hpp`) without any hardware.
- **Protocol changes** must update `docs/protocol.md`, the C++ handler,
  `superlotis.testing.FakeCameraServer`, and the Python client together. The
  contract tests in `python/tests/test_client.py` run against both servers and
  will catch drift.
- **Hardware code** (`picam_backend.cpp`) can't run in CI. Describe how you tested
  it (demo camera or real hardware) in the PR.
- Keep the Python package dependency-free at runtime.
- Units: seconds and degrees Celsius everywhere. Convert at the backend boundary
  (PICam uses milliseconds).
- Commit messages: imperative mood, short summary line, body explaining *why*.
