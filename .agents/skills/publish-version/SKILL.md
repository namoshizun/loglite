---
name: publish-version
description: Publish a LogLite GitHub release and upload Python wheels to PyPI for a given version_number using pypi_token. Use when the user asks to publish a version, cut a GitHub release, upload wheels to PyPI, or after a vX.Y.Z tag is pushed.
---

# Publish version

Accept two inputs: `version_number` (semver `X.Y.Z`, e.g. `1.3.2`) and `pypi_token`. Stop if either is missing, or if `version_number` is not `X.Y.Z`.

Tag is `v{version_number}` (e.g. `v1.3.2`). Do not push tags, commit, or force-push.

Never echo `pypi_token`, write it to disk, or include it in logs, commit messages, or the final report. Pass it only as the `UV_PUBLISH_TOKEN` environment variable.

Repo root is the workspace root. Run `gh` from there. Use a temp dir for downloads (`mktemp -d`); do not leave artifacts in the repo.

## Workflow

Copy this checklist and complete in order. Stop on the first failure.

```
- [ ] 1. check that tag v{version_number} has been pushed to GitHub
- [ ] 2. wait until the cpp and python release GitHub Actions complete successfully
- [ ] 3. download cpp binaries and python wheels from those run artifacts
- [ ] 4. create the GitHub release with gh, uploading the downloaded artifacts
- [ ] 5. publish the wheels to PyPI
```

### 1. check that tag v{version_number} has been pushed to GitHub

```bash
git ls-remote --exit-code origin "refs/tags/v{version_number}"
```

If this fails, stop. Tell the user to push `v{version_number}` first. Do not create the tag.

If `gh release view "v{version_number}"` already succeeds, stop. Do not recreate or overwrite the release.

### 2. wait until the cpp and python release GitHub Actions complete successfully

Workflows (tag `v*` push):

| Workflow file | Artifact names to download later |
|---|---|
| `.github/workflows/build-cpp-release-image.yml` | `loglite-amd64-v{version_number}`, `loglite-arm64-v{version_number}` |
| `.github/workflows/build-python-wheels.yml` | `wheels-ubuntu-24.04-x86_64`, `wheels-ubuntu-24.04-arm-aarch64` |

Resolve the latest run for each workflow on that tag:

```bash
gh run list --workflow build-cpp-release-image.yml --branch "v{version_number}" --limit 1 \
  --json databaseId,status,conclusion,url
gh run list --workflow build-python-wheels.yml --branch "v{version_number}" --limit 1 \
  --json databaseId,status,conclusion,url
```

If a run is missing, poll every 30–60s until both exist (the tag push may still be queueing). Do not give up after one check.

Then wait until both finish:

```bash
gh run watch {cpp_run_id} --exit-status
gh run watch {python_run_id} --exit-status
```

These builds can take tens of minutes. Keep watching; do not abort early.

Both must end with `conclusion: success`. If either failed or was cancelled, stop and report the run URL. Do not download artifacts or publish.

### 3. download cpp binaries and python wheels from those run artifacts

Download **only** the named artifacts (the cpp workflow also has `image-*` tars — skip those).

```bash
gh run download {cpp_run_id} \
  -n "loglite-amd64-v{version_number}" \
  -n "loglite-arm64-v{version_number}" \
  -D "$WORKDIR/cpp"

gh run download {python_run_id} \
  -n wheels-ubuntu-24.04-x86_64 \
  -n wheels-ubuntu-24.04-arm-aarch64 \
  -D "$WORKDIR/wheels"

mkdir -p "$WORKDIR/pypi"
find "$WORKDIR/wheels" -name '*.whl' -exec cp {} "$WORKDIR/pypi/" \;
```

Expect:

- `$WORKDIR/cpp/loglite-amd64-v{version_number}/loglite-amd64`
- `$WORKDIR/cpp/loglite-arm64-v{version_number}/loglite-arm64`
- several `loglite-{version_number}-cp3*-manylinux_*.whl` files in `$WORKDIR/pypi`

Stop if any of those binaries or wheels are missing.

### 4. create the GitHub release with gh, uploading the downloaded artifacts

Release notes are the list items under `### {version_number}` in `CHANGELOG.md` (exact heading), up to the next `###` heading. Do not include the heading itself. If that section is missing or empty, stop.

Title is `v{version_number}`. Upload the two binaries **as** `loglite-amd64` and `loglite-arm64` (existing asset names; do not add a version suffix) plus every wheel.

```bash
gh release create "v{version_number}" \
  --title "v{version_number}" \
  --notes-file "$WORKDIR/notes.md" \
  --verify-tag \
  "$WORKDIR/cpp/loglite-amd64-v{version_number}/loglite-amd64" \
  "$WORKDIR/cpp/loglite-arm64-v{version_number}/loglite-arm64" \
  "$WORKDIR/pypi/"*.whl
```

### 5. publish the wheels to PyPI

```bash
UV_PUBLISH_TOKEN="$pypi_token" uv publish "$WORKDIR/pypi/"*.whl
```

If this fails after the GitHub release was created, report that the release exists and PyPI failed. Do not delete the GitHub release.

## Done

Report the tag, GitHub release URL, that both workflow runs succeeded, which assets were uploaded, and that wheels were published to PyPI. Do not print `pypi_token`.
