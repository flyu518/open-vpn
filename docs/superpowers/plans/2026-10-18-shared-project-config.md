# Shared Project Configuration Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to execute each task inline, with test-first changes and verification checkpoints.

**Goal:** macOS and Windows share one product version and connection policy, with a root release procedure usable on either platform.

**Architecture:** `project-config.json` is the checked-in source of truth. A shared CMake reader validates typed values, serves packaging queries, and generates compile-time C++ constants. Packaging accepts legacy version arguments only when they match the configuration. Runtime does not read a writable external configuration.

**Tech Stack:** JSON, CMake 3.21+, C++20, PowerShell, Bash, GitHub Actions.

## Task 1 Preserve the tested Windows baseline

- [x] Run existing core and Qt CTest suites and `git diff --check`.
- [x] Review and commit only the pending Windows task source, packaging, documentation and tests; exclude temporary files, credentials, binaries and the new release documents. Baseline: `0ae700f`.

## Task 2 Shared reader and validation tests

- [x] Add `tests/config/test_project_config.cmake`; first run must fail because the shared reader is missing.
- [x] Add `project-config.json` with version `1.0.0`, timeout 30 seconds and auth retry disabled.
- [x] Add `cmake/ProjectConfig.cmake`: validate schema, version, positive bounded integer timeout, boolean retry, known query keys, and expected version; expose values to CMake and CLI using the same parser.
- [x] Add `cmake/openvpn_project_config.h.in` and generated-header support.
- [x] Run `cmake -P tests/config/test_project_config.cmake`; cover default/alternate values, invalid/missing/type-invalid values, unknown queries, expected-version mismatch and generated C++ constants.

## Task 3 Wire both build trees and native consumers

- [x] Make core and Qt read the root configuration and depend on its contents for regeneration.
- [x] Make the shared engine header use generated constants; add include paths to real and fixture engine targets on Windows and macOS.
- [x] Make Qt project/app/Bundle versions use the configured product version and reject conflicting version inputs; add and test Windows PE version resources.
- [x] Update timeout checks to verify the canonical configuration and generated aliases rather than a duplicate literal.
- [x] Register the shared configuration regression suite in both CTest trees.

## Task 4 Wire packaging and CI

- [x] Make Windows packaging query the shared reader before building, default to its version, and reject conflicting explicit versions before invoking build tools.
- [x] Make both macOS packaging scripts query the same reader, reject mismatches and validate the actual app Bundle version.
- [x] Make macOS CI validate tag/manual version against the checked-out configuration before publishing; do not add an unreviewed Windows publication path.
- [x] Add tests for packaging defaults, early mismatch rejection and syntax; run PowerShell checks and Bash syntax/mismatch checks where available.

## Task 5 Document, build and commit

- [x] Update `RELEASING.md` to describe the implemented JSON contract, identical query operation and unchanged publication gate limitations.
- [x] Build affected Windows production/test targets and run complete core/Qt suites.
- [x] Audit active READMEs, platform/protocol/acceptance documents and generated release notes; preserve historical results and remaining limitations.
- [x] Review the final diff and prepare only configuration, tests and documentation for the requested commit; report the resulting commit ID after Git succeeds.
- [x] Do not tag, publish a Release, reinstall the running application, or claim that Mac has been built on Windows.

## Verification evidence and handoff

- Windows MSVC production and test targets rebuilt successfully. Final rerun: core 17/17, Qt 12/12 passed.
- Shared parser and packaging tests passed, including a red/green regression rejecting alternate JSON in production configuration and a red/green Windows PE metadata test.
- Independent read-only code review found a production configuration override; fixed with the regression above. Follow-up found no remaining definite Critical/Important issue.
- Git Bash checked both Mac packaging scripts and their early mismatch rejection. Native Mac compilation, PKG installation and real Mac VPN regression remain for the Mac host.
- Active document relative links checked across 17 Markdown files; workflow YAML and seven shell blocks parsed, and release notes rendered without backtick command substitution.
- Historical Windows 0.0.14 installation/connection evidence remains separate from the new 1.0.0 candidate's build tests. No new installer or Release was delivered in this task.
