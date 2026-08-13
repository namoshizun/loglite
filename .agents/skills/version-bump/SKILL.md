---
name: version-bump
description: Bump the LogLite C++ and Python package version in lockstep. Use when the user asks to bump the version, cut a release version, or provides a version_number to apply.
---

# Version bump

Accept a single input: `version_number` (semver `X.Y.Z`, e.g. `1.3.2`). Stop if it is missing or not `X.Y.Z`.

Do not commit unless the user asks.

Repo root is the workspace root. All paths below are relative to it.

## Workflow

Copy this checklist and complete in order. Stop on the first failure.

```
- [ ] 1. checks that the version has corresponding changelog
- [ ] 2. update cpp/CMakeLists.txt
- [ ] 3. run `uv version`
- [ ] 4. update py/loglite/__init__.py
- [ ] 5. execute cpp unit tests, make sure all pass
- [ ] 6. execute python unit tests, make sure all pass
```

### 1. checks that the version has corresponding changelog

`CHANGELOG.md` must contain a heading `### {version_number}` (exact match) with at least one list item under it.

If the heading is missing or the section has no entries, stop. Tell the user to add the changelog section first. Do not edit version files.

### 2. update cpp/CMakeLists.txt

In `cpp/CMakeLists.txt`, set the `project()` version:

```
project(loglite VERSION {version_number} LANGUAGES CXX)
```

C++ `kVersion` comes from `cpp/src/version.hpp.in` via `@PROJECT_VERSION@`. Do not edit the generated header.

### 3. run `uv version`

From `py/`:

```bash
uv version {version_number}
```

This updates `py/pyproject.toml`. Do not hand-edit that file.

### 4. update py/loglite/__init__.py

Set:

```python
__version__ = "{version_number}"
```

### 5. execute cpp unit tests, make sure all pass

From the repo root:

```bash
./cpp/run-tests.sh
```

All tests must pass. If any fail, stop and report. Do not continue to Python tests.

### 6. execute python unit tests, make sure all pass

From `py/`:

```bash
uv run pytest
```

All tests must pass. If any fail, stop and report.

## Done

Report the new version and that changelog, CMake, `uv version`, `__init__.py`, C++ tests, and Python tests all succeeded.
