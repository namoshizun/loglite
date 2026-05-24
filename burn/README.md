# burn

HTTP load generator for Loglite ingest (`POST /logs`). Used to stress the server, validate keep-alive behavior, and explore accept vs. durable throughput under configurable rates.

Standalone C++20 binary — not linked to `loglite_lib`. Same Conan/Boost/CLI11 stack as [`../cpp/`](../cpp/), lighter dependencies only.

## Behavior

1. **Schema discovery** — blocking `GET /schema` before any load. Requires columns `timestamp`, `message`, and `level`. Builds a shared payload plan from each column’s `kind` in the JSON response.
2. **Senders** — `concurrency` coroutines on a **single-threaded** `io_context`. Each coroutine opens one TCP connection, enables HTTP keep-alive, and reuses it for every `POST /logs`.
3. **Rate** — total `--qps` is split evenly: each sender paces at `qps / concurrency` using `steady_timer` (supports fractional rates).
4. **Shutdown** — `--duration` sets a stop flag; `SIGTERM` / `SIGINT` do the same. When the last sender exits, the process calls `ioc.stop()` so pending signal/timer handlers do not hang the run.

Compatible with Loglite 1.3.0+. Previous loglite versions did not support HTTP keep-alive on non-SSE routes.

## Prerequisites

Same as the main C++ tree: CMake ≥ 3.25, C++20, Conan 2.x. Toolchain files are reused from [`../cpp/cmake/toolchains/`](../cpp/cmake/toolchains/).

## Build

```bash
./build.sh            # Debug — compile_commands.json for clangd / IDE
./build.sh --release  # Release — O3, LTO, strip, -march=native, visibility / dead-strip
```

Output: `build/<os>-<arch>/<debug|release>/burn`

Dependencies: Boost (Asio + Beast), CLI11, nlohmann_json — see [`conanfile.py`](conanfile.py).

## Run

Start Loglite first, then for example:

```bash
./build/apple-arm64/debug/burn \
  --endpoint http://localhost:7788 \
  --concurrency 8 \
  --qps 100 \
  --duration 60
```

Only `http://` endpoints are supported (no TLS).

### CLI

| Option           | Default                 | Notes                                                                            |
| ---------------- | ----------------------- | -------------------------------------------------------------------------------- |
| `--endpoint`     | `http://localhost:7788` | Host/port only; paths ignored (`/logs`, `/schema` are fixed).                    |
| `--concurrency`  | *(required)*            | Sender coroutines; must be &gt; 0.                                               |
| `--qps`          | *(required)*            | Total requests/s across all senders; must be &gt; 0.                             |
| `--message-size` | `128`                   | Mean length of the `message` field (normal distribution, σ ≈ mean/4, min 1).     |
| `--info-ratio`   | `0.9`                   | Fraction of logs at `INFO`; remainder uniform over DEBUG/WARNING/ERROR/CRITICAL. |
| `--duration`     | `60`                    | Run length in seconds.                                                           |

Exit code: `0` if every attempted POST got HTTP 200 and `"status":"accepted"`; `1` if any failed or CLI/schema error.

### Summary line

Printed on stderr when the run finishes:

```text
burn finished: 60.0s  ok=119400  fail=0  effective_qps=1990.0
```

- **`ok`** — HTTP success + accepted body (ingest enqueue only, not SQLite commit).
- **`fail`** — non-2xx, bad JSON body, or I/O error after one reconnect attempt.
- **`effective_qps`** — `ok / wall_time` from sender start to process exit (includes pacing and shutdown tail). Compare to `--qps` to see how well burn kept its throttle; it does **not** by itself measure Loglite’s maximum capacity.

For durable throughput, correlate with Loglite metrics (`insert_batch`, backlog drops) and row counts in the DB.
