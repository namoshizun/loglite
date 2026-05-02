# Loglite (C++)

Native C++ implementation of Loglite — fully featured and a drop-in replacement for the Python build. The same config file and database work unchanged; no migration needed when switching between the two.

The only current limitation is partial harvester support: only the **file harvester** is implemented.

## Why C++?

- **Single binary, no runtime** — one executable with no interpreter or VM; deployment is a file copy.
- **Low memory footprint** — tighter runtime suitable for small or constrained hosts.
- **Embedded-friendly** — minimal RAM and trivial deployment fits edge environments.

## Prerequisites

- **CMake** ≥ 3.25
- **C++23** toolchain (Clang or GCC)
- **Conan 2.x**: installed as [`conan`](https://conan.io/)

On macOS (Apple Silicon), the build script uses Homebrew LLVM Clang (`/opt/homebrew/opt/llvm/bin/clang++`) when present.
On Linux, release builds must run in a musl environment, such as Alpine Linux or `Dockerfile.linux.release`.

## Build

```bash
./build.sh            # Debug (fast iteration)
./build.sh --release  # Release (O3, LTO, stripped)
```

Runs `conan install`, configures CMake, and builds the `loglite` target. The binary is placed at `build/<os-arch>/<debug|release>/loglite`.

Linux release builds produce a statically linked musl binary under `build/linux-musl-<arch>/release/loglite`.
The generated release container uses `scratch` and contains only `/usr/local/bin/loglite`.

## Docker Images

- `Dockerfile.linux.test` builds the glibc/GCC base image used for CI tests.

- `Dockerfile.linux.release` builds the portable Linux release image from Alpine/musl.

## Tests

```bash
./run-tests.sh           # Build and run GoogleTest suite
./run-tests.sh --cov     # Same, plus gcov/lcov coverage summary (requires `lcov`)
```
