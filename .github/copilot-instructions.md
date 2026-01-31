# Copilot Instructions for c64stream

Primary reference: see [AGENTS.md](../AGENTS.md).

This file is a short entrypoint for automated agents. Follow the guardrails and build/test workflow in AGENTS.md.

Key reminders:
- Formatting is required before any push: run `./build-aux/run-clang-format --check` and fix failures.
- E2E runs only on a local machine with a GUI. Do not run E2E in CI or cloud shells.
