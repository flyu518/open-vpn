# Windows Startup Fix Release Readiness Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to carry out this plan task by task.

**Goal:** Complete and record every release check that is available for the Windows startup fix without publishing while required platform gates remain open.

**Architecture:** Keep the frozen Windows source candidate `26bf5e2944db3c21f0a9ce7e1cf5723a60644c29` unchanged. Build and test that candidate on macOS ARM64, inspect live services before any installer run, and record platform-specific results separately. Publish only after the exact candidate is installed and accepted on all target platforms and the release workflow can aggregate their assets safely.

**Tech Stack:** Git, CMake 4.4.4, Qt 6.12.0, CTest, macOS Installer, GitHub Actions.

**Spec:** `RELEASING.md`, `docs/releases/1.0.0-windows.md`, `docs/releases/1.0.0-macos.md`.

## Global Constraints

- Windows and macOS acceptance must refer to the same full source candidate SHA.
- Build candidates in fresh directories with the locked OpenVPN3 revision and matching architecture dependencies.
- Record build tests, package hashes, installed-package results, and untested items separately.
- Do not interrupt active VPN sessions for installer acceptance.
- Do not create a release tag or publish while target architecture, signing, or cross-platform aggregation gates are incomplete.
- Keep Windows Startup-folder behavior and macOS LaunchAgent behavior documented as separate platform implementations.

## Review Focus

- Active daemon sessions must not be stopped during package installation; query the daemon snapshot before proceeding.
- The macOS LaunchAgent starts at user login and must be tested independently from Windows startup registration.
- The frozen Windows candidate still needs its own post-install reboot check; a prior build does not substitute.
- Intel macOS packages need Intel hardware installation acceptance, even if an x86_64 package can be cross-built.
- Release automation must not create a public release from only macOS assets or unsigned candidates.

---

### Task 1: Verify Frozen Candidate and Platform Gates

**Files:** Read `RELEASING.md`, both `docs/releases/1.0.0-*.md`, and `.github/workflows/release-macos.yml`.

- [x] Confirm the Windows source candidate SHA and whether any later commit changes product code.
- [x] Confirm remote tags, branch state, current documentation edits, and the release-workflow limitation.
- [x] Confirm local Mac architecture, available signing identities, and active daemon session count without exposing profile details.

### Task 2: Rebuild the Exact Candidate for macOS ARM64

**Files:** No source changes; create a fresh build directory under `/tmp` and update `docs/releases/1.0.0-macos.md` with evidence.

- [x] Build candidate SHA `26bf5e2944db3c21f0a9ce7e1cf5723a60644c29` using the pinned toolchain and locked OpenVPN3 source.
- [x] Run the prescribed Core and Qt CTest suites; require zero failures.
- [x] Generate and hash the ARM64 PKG; record runtime validation and exact package hash.

### Task 3: Install Only When the VPN Is Idle

**Files:** Run the generated PKG scripts and verify the installed app and services.

- [x] Query `snapshot` and proceed only when no VPN sessions are active.
- [ ] Install the generated PKG, verify app version/path, daemon/helper health, and startup setting behavior.
- [ ] Do not claim reboot acceptance unless the machine actually logs out/restarts and the installed UI starts afterward.

### Task 4: Record and Share Evidence

**Files:** `desktop/qt/README.md`, `packaging/macos/README.md`, `docs/releases/1.0.0-macos.md`.

- [x] Keep Windows and macOS startup mechanisms, current-session behavior, and system-prompt behavior explicit.
- [x] Record completed ARM64 evidence and every remaining architecture, reboot, signing, and workflow gate.
- [ ] Run `git diff --check`, commit only the release documentation, and push it to the confirmed shared branch.

### Task 5: Stop at the Formal Release Gate

**Files:** `.github/workflows/release-macos.yml`, `RELEASING.md`.

- [ ] Verify that all target platform packages, install tests, signing/notarization, and cross-platform aggregation gates are complete.
- [ ] Create a tag and publish only if every required gate passes; otherwise report the exact external blocker and do not publish.
