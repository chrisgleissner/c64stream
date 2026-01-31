# Agent Guide (LLM / Copilot / Cursor)

This repository is an OBS Studio source plugin (`c64stream`) for streaming C64 Ultimate video/audio over the network.

## Guardrails (must follow)

- Always investigate errors, warnings, and assertion failures; fix the root cause.
- Never skip tests or reduce coverage to make failures pass.
- Run E2E only on a local machine with a GUI (X11/Wayland). Do not run E2E in CI or cloud shells.
- Formatting is mandatory before any push: run `./build-aux/run-clang-format --check`, then format files and re-check if needed.
- Review docs you touched (`doc/`, `docs/`, `README.md`, this file) before declaring work done.
- C64Script trace validation applies to `.c64script` changes; update matching `.expected-trace.yaml` files and keep traces under 1000 steps.

## Build + test (local)

Preferred workflow (Linux):

```bash
./build --tests --script-tests
```

Common targets:

```bash
./build --install
./build --install --e2e
```

Formatting:

```bash
./build-aux/run-clang-format --check
./build-aux/run-clang-format path/to/file.c path/to/file.h
```

Focused tests:

```bash
ctest --test-dir build_x86_64 --output-on-failure
ctest --test-dir build_x86_64 -R c64script_all_scripts --output-on-failure
python3 -m unittest tests/e2e/util/test_network_simulation.py tests/e2e/util/test_network_timing_validation.py
```

## CI/Copilot builds (short)

- CI build entrypoint: `.github/scripts/build-ubuntu --target ubuntu-x86_64 --config RelWithDebInfo`
- Copilot deps installer: `./.github/scripts/install-copilot-deps.sh`
- `./build --install-deps` installs core build deps; `--install-e2e-deps` adds OBS/xvfb for local E2E.
- clang-format may be missing in minimal Copilot environments; CI validates formatting.

## Where to look

- Project overview: `README.md`
- Plans (multi-hour work): `PLANS.md`
- Investigations (deep fixes): `INVESTIGATIONS.md`
- Protocol: `doc/c64/c64u-stream-spec.md`
- REST: `doc/c64/c64u-rest-api.md`, `doc/c64/c64u-openapi.yaml`

## Engineering priorities

- Low latency and robustness over new features.
- Deterministic render behavior across OBS render paths.
- Keep modules focused; split files that grow beyond ~1000 lines.
