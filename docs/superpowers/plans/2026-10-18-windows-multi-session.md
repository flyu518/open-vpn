# Windows Multi-Session Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Make Windows support the same concurrent VPN-session behavior as macOS without changing macOS interaction.

**Architecture:** Keep the existing session-oriented desktop protocol and Qt UI unchanged. Remove the Windows daemon's explicit one-session cap, introduce a small testable Windows session-capacity policy, and generate a patched Windows OpenVPN3 agent from the pinned vendored source at configure time. The generated agent owns Wintun/TAP resources, route policy, endpoint bypass routes, DNS state, and watchdog cleanup per session key. macOS sources remain untouched.

**Tech Stack:** C++17, CMake/CTest, pinned OpenVPN3 Windows agent source, JsonCpp, OpenSSL, Wintun/TAP, Windows Named Pipes, PowerShell/.NET, existing Qt/QML desktop client.

---

## Files and responsibilities

- Modify core/CMakeLists.txt: make the Windows test configuration self-contained, generate the patched Windows agent source from the pinned OpenVPN3 checkout, and build the generated source into openvpn_windows_service.
- Modify core/tests/CMakeLists.txt: register Windows agent fixture tests and keep the controller regression test buildable on Windows.
- Create core/tools/windows_session_policy.h: expose the Windows desktop default of unlimited active sessions as a small cross-platform-testable policy.
- Modify core/tools/windows_daemon_main.cpp: construct the controller with the shared desktop multi-session capacity.
- Modify core/tests/daemon_session_limit_tests.cpp: specify multi-session isolation and concurrency while retaining generic explicit-capacity tests.
- Create platform/windows/service/generate_multisession_agent.cmake: apply required, version-checked source transformations to the pinned Windows agent without modifying third_party/openvpn3.
- Create platform/windows/tests/windows_agent_source_fixture.cpp.txt: minimal source fixture containing every replacement boundary used by the generator.
- Create platform/windows/tests/windows_agent_patch_tests.cmake: CTest script that verifies successful generation and deliberate failure when a required boundary is absent.
- Create tests/smoke/windows-installed.ps1: real Windows named-pipe smoke test using profiles and saved Credential Manager credentials.
- Modify Windows and acceptance documentation to remove obsolete single-session claims and document the Windows multi-session build and test flow.
- Do not modify macOS adapter/helper/agent files or Qt behavior unless a test proves an existing platform-specific branch violates the shared protocol.

## Task 1: Make the Windows core test configuration reproducible

**Files:**
- Modify core/CMakeLists.txt around the top-level dependency setup and the existing find_package(Threads) call.
- Test the existing CMake configure command in the Windows worktree.

- [ ] Step 1: Reproduce the failing configure baseline

Run from the worktree:

~~~powershell
cmake -S core -B build/core-baseline
~~~

Expected before the fix: configuration reaches generation and fails because core/tests/CMakeLists.txt references Threads::Threads while that imported target was not created.

- [ ] Step 2: Add the smallest configuration fix

Move find_package(Threads REQUIRED) to the top-level core configuration, before add_subdirectory(tests), and remove the duplicate call from the OpenVPN3 dependency block. Keep linking Threads::Threads on every target that already uses it. Do not add platform-specific compiler flags or change OpenVPN3 dependency discovery in this step.

- [ ] Step 3: Re-run configure and record the next real dependency boundary

Run:

~~~powershell
Remove-Item -LiteralPath build/core-baseline -Recurse -Force
cmake -S core -B build/core-baseline
~~~

Expected: the Threads::Threads generation error is gone. If configuration now stops at a missing JsonCpp, OpenSSL, or OpenVPN3 dependency, record that as an environment prerequisite for the full Windows build; do not mask it with empty imported targets.

- [ ] Step 4: Commit the isolated build-system fix

~~~powershell
git add core/CMakeLists.txt
git commit -m "build: make core test threads dependency available on Windows"
~~~

## Task 2: Define the desired multi-session behavior with failing tests

**Files:**
- Modify core/tests/daemon_session_limit_tests.cpp.
- Test core/tools/windows_session_policy.h through the same target.

- [ ] Step 1: Add a test that proves the Windows desktop policy is unlimited

Include windows_session_policy.h and assert that windows_max_active_sessions() == 0. At this point the header/function is intentionally absent, so the test target must fail to compile. This pins the Windows daemon contract before implementation.

- [ ] Step 2: Add a test that proves two default desktop sessions are independent

Add a case using SessionController controller(ignore_notification, nullptr); that connects a and b with fixture:connected, asserts a two-item snapshot with both ids, disconnects only a, and asserts that b remains present and connected with its own traffic counters. Locate sessions by sessionId, not map order. Use the existing request() and command() helpers directly.

The required behavior is equivalent to:

~~~cpp
void multiple_default_sessions_are_independent() {
    SessionController controller(ignore_notification, nullptr);
    require(request(controller, connect_request("a"))["ok"].asBool(), "A 连接失败");
    require(request(controller, connect_request("b"))["ok"].asBool(), "B 连接失败");
    auto sessions = command(controller, "snapshot")["sessions"];
    require(sessions.size() == 2, "默认桌面能力没有保留两个会话");
    require(command(controller, "disconnect", "a")["ok"].asBool(), "A 断开失败");
    sessions = command(controller, "snapshot")["sessions"];
    require(sessions.size() == 1 && sessions[0]["sessionId"] == "b" &&
                sessions[0]["state"] == "connected" &&
                sessions[0]["downloadBytes"].asInt64() == 222,
            "断开 A 影响了 B");
}
~~~

- [ ] Step 3: Add a concurrent-connect isolation case

Use the existing barrier pattern from concurrent_connects_reserve_one_slot, construct the controller with the default unlimited capacity, and use eight distinct ids with fixture:slow-start. Assert all requests return ok, the snapshot contains all eight ids, and no session was replaced.

- [ ] Step 4: Run only the modified test target before changing production code

After Task 1 has produced a configured test tree, run:

~~~powershell
cmake --build build/core-baseline --config Release --target openvpn_daemon_session_limit_tests --parallel
ctest --test-dir build/core-baseline -R openvpn_daemon_session_limit_tests --output-on-failure
~~~

Expected RED: the new policy assertion fails because the policy header/function is not implemented. The existing default-controller behavior may remain green; that is acceptable because the actual bug is the Windows daemon's explicit constructor cap.

- [ ] Step 5: Commit the failing-test change only

~~~powershell
git add core/tests/daemon_session_limit_tests.cpp
git commit -m "test: specify independent Windows desktop sessions"
~~~

## Task 3: Remove the daemon-only Windows single-session guard

**Files:**
- Create core/tools/windows_session_policy.h.
- Modify core/tools/windows_daemon_main.cpp.
- Modify core/tests/daemon_session_limit_tests.cpp test registration and obsolete expectations.

- [ ] Step 1: Implement the desktop session-capacity policy

Create:

~~~cpp
#pragma once

#include <cstddef>

namespace vpn::daemon {
inline constexpr std::size_t windows_max_active_sessions() {
    return 0;
}
}
~~~

Use 0 for the existing SessionController convention meaning unlimited.

- [ ] Step 2: Change Windows daemon construction to the desktop default

Include the policy header and replace:

~~~cpp
controller_([this](const Json::Value& value) { broadcast(value); }, &credential_store_, 1)
~~~

with:

~~~cpp
controller_([this](const Json::Value& value) {
    broadcast(value);
}, &credential_store_, vpn::daemon::windows_max_active_sessions())
~~~

Update the adjacent comment so it states that the Windows agent is session-isolated and uses the shared desktop multi-session contract. Do not change SessionController's explicit nonzero limit mechanism; it remains available for mobile/single-capability tests.

- [ ] Step 3: Remove tests whose required behavior is the obsolete Windows error

Delete limit_message, require_limited, and Windows single-capacity cases from the fixture test file. Preserve explicit-capacity tests only when they validate generic SessionController(max_active_sessions = 1) behavior. Register the new independent and concurrent cases.

- [ ] Step 4: Run the controller target and core CTest suite

~~~powershell
cmake --build build/core-baseline --config Release --target openvpn_daemon_session_limit_tests --parallel
ctest --test-dir build/core-baseline -R openvpn_daemon_session_limit_tests --output-on-failure
ctest --test-dir build/core-baseline --output-on-failure
~~~

Expected: the policy and controller tests pass, generic explicit-capacity tests remain green, and no response contains the old Windows single-session message.

- [ ] Step 5: Commit the daemon behavior change

~~~powershell
git add core/tools/windows_session_policy.h core/tools/windows_daemon_main.cpp core/tests/daemon_session_limit_tests.cpp
git commit -m "feat: enable concurrent Windows VPN sessions"
~~~

## Task 4: Add the Windows agent per-session source transformation

**Files:**
- Create platform/windows/service/generate_multisession_agent.cmake.
- Create platform/windows/tests/windows_agent_source_fixture.cpp.txt.
- Create platform/windows/tests/windows_agent_patch_tests.cmake.

- [ ] Step 1: Write the generator fixture and failing patch test

The fixture must contain the exact upstream source boundaries required by the generator: agent state declarations, single-session TUN setup/destroy functions, watchdog cleanup, and service shutdown cleanup. The patch test must invoke the generator with INPUT_SOURCE and OUTPUT_SOURCE, assert success for the complete fixture, and use a second fixture with one boundary removed and WILL_FAIL TRUE to assert fail-fast behavior.

Run the test before implementing the generator:

~~~powershell
cmake -DINPUT_SOURCE=platform/windows/tests/windows_agent_source_fixture.cpp.txt -DOUTPUT_SOURCE=build/windows-agent-generated.cpp -P platform/windows/service/generate_multisession_agent.cmake
~~~

Expected RED: the generator file does not exist yet.

- [ ] Step 2: Implement required replacement checks

Follow the existing macOS generator pattern: read INPUT_SOURCE, define a replace_required(OLD_TEXT NEW_TEXT DESCRIPTION) helper, fail with a descriptive error when string(FIND) returns -1, and write only OUTPUT_SOURCE. Never edit the vendored OpenVPN3 source.

- [ ] Step 3: Implement session-keyed Windows state in the generated source

Use the exact OpenVPN3 Windows types from the pinned checkout and introduce logical state equivalent to:

~~~cpp
using SessionKey = std::string;
std::map<SessionKey, TunWin::SetupBase::Ptr> tuns;
std::map<SessionKey, std::string> session_bypass_hosts;
std::map<SessionKey, vpn::RoutePolicy> route_policies;
std::map<SessionKey, ActionList> remove_cmds_bypass_hosts;
std::map<DWORD, WatchdogThread::Ptr> watchdogs;
~~~

Use pid + ":" + sanitized session name as the key. Strip |route-mode= and |route-rules= before assigning adapter/session display names while preserving the markers for policy parsing. Implement has_pid_sessions(pid), destroy_tuns_for_pid(pid, os), and per-session destroy_tun(pid, name, os, from_watchdog) so service stop, watchdog cleanup, and explicit teardown are idempotent.

- [ ] Step 4: Apply the macOS route-policy pipeline to the current Windows session

In generated establish-tun code, build the requested vpn::RoutePolicy from captured routes and encoded route rules, apply exclude rules before validation, call vpn::vpn_route_policy_validate(active, candidate, mode), reject non-allowed decisions, remove compatible-mode conflicts from the candidate route list, and emit the existing Chinese diagnostic formats consumed by route_conflicts_from_log().

Apply domain rules to the current Windows DNS capture only. Store final installed policy under the current session key. Keep endpoint bypass add/remove commands per pid and endpoint; remove a shared endpoint only after its last session is destroyed.

- [ ] Step 5: Verify the generator against the fixture and pinned source

Run both fixture cases and configure with the actual pinned source:

~~~powershell
cmake -DINPUT_SOURCE=platform/windows/tests/windows_agent_source_fixture.cpp.txt -DOUTPUT_SOURCE=build/windows-agent-generated.cpp -P platform/windows/service/generate_multisession_agent.cmake
cmake -S core -B build/core-windows -G "Visual Studio 17 2022" -A x64 -DOPENVPN3_SOURCE_DIR="$PWD/third_party/openvpn3" -DXXHASH_ROOT_DIR="C:/deps/xxHash" -DASIO_INCLUDE_DIR="C:/deps/asio/include" -DFMT_INCLUDE_DIR="C:/deps/fmt/include" -DJSONCPP_INCLUDE_DIR="C:/deps/jsoncpp/include" -DLZ4_INCLUDE_DIR="C:/deps/lz4/include" -DOPENSSL_INCLUDE_DIR="C:/deps/openssl/include" -DOPENSSL_ROOT_DIR="C:/deps/openssl" -DLZ4_LIBRARY="C:/deps/lz4/lib/lz4.lib" -DFMT_LIBRARY="C:/deps/fmt/lib/fmt.lib" -DJSONCPP_LIBRARY="C:/deps/jsoncpp/lib/jsoncpp.lib"
~~~

Expected: the fixture success output contains the session maps and cleanup functions; the missing-boundary case fails; the pinned source either generates successfully or fails at a named source-boundary check rather than silently producing an unpatched agent.

- [ ] Step 6: Commit the generator and its tests

~~~powershell
git add platform/windows/service/generate_multisession_agent.cmake platform/windows/tests
git commit -m "feat: isolate Windows agent resources per session"
~~~

## Task 5: Wire the generated agent into the Windows service build

**Files:**
- Modify core/CMakeLists.txt Windows service block.
- Modify core/tests/CMakeLists.txt.

- [ ] Step 1: Register the generated Windows agent source

When OPENVPN3_SOURCE_DIR/openvpn/ovpnagent/win/ovpnagent.cpp exists, define VPN_WINDOWS_AGENT_GENERATED, add an add_custom_command invoking generate_multisession_agent.cmake, add an openvpn_windows_agent_source custom target, and make openvpn_windows_service depend on it. Replace the direct upstream source in the service target with the generated file. Keep OVPNAGENT_NAME=openvpn and existing Windows link libraries unchanged.

- [ ] Step 2: Register the CMake patch test

Add a CTest script entry for platform/windows/tests/windows_agent_patch_tests.cmake. Set the test working directory to the source tree and pass a unique build output path so parallel test runs do not share generated files.

- [ ] Step 3: Configure and build the Windows service

With the pinned OpenVPN3 source and dependency paths available:

~~~powershell
cmake -S core -B build/core-windows -G "Visual Studio 17 2022" -A x64 -DOPENVPN3_SOURCE_DIR="$PWD/third_party/openvpn3" -DXXHASH_ROOT_DIR="C:/deps/xxHash" -DASIO_INCLUDE_DIR="C:/deps/asio/include" -DFMT_INCLUDE_DIR="C:/deps/fmt/include" -DJSONCPP_INCLUDE_DIR="C:/deps/jsoncpp/include" -DLZ4_INCLUDE_DIR="C:/deps/lz4/include" -DOPENSSL_INCLUDE_DIR="C:/deps/openssl/include" -DOPENSSL_ROOT_DIR="C:/deps/openssl" -DLZ4_LIBRARY="C:/deps/lz4/lib/lz4.lib" -DFMT_LIBRARY="C:/deps/fmt/lib/fmt.lib" -DJSONCPP_LIBRARY="C:/deps/jsoncpp/lib/jsoncpp.lib"
cmake --build build/core-windows --config Release --parallel
~~~

Expected: the generated agent source is compiled into openvpn_windows_service, and daemon/service targets link without the old single-session guard.

- [ ] Step 4: Commit the CMake integration

~~~powershell
git add core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "build: generate the Windows multi-session agent"
~~~

## Task 6: Add real Windows acceptance automation

**Files:**
- Create tests/smoke/windows-installed.ps1.
- Modify tests/smoke/README.md.

- [ ] Step 1: Implement safe named-pipe JSON transport

Use .NET NamedPipeClientStream to connect to openvpn-ui, send one compact JSON object plus a newline, read newline-delimited responses, ignore type=sessionEvent notifications, and fail on { ok: false }. Provide configurable PipeName and timeout parameters.

- [ ] Step 2: Implement profile and credential handling without secrets in output

Read %APPDATA%\\OpenVPN\\profiles.json unless ProfilesPath is provided. Resolve managedFile relative to the profile index, otherwise use stored profile content. Require two imported profiles with saved credentials; send an empty credentials object so the daemon loads Credential Manager entries. Never print profile contents, usernames, passwords, or full daemon diagnostics.

- [ ] Step 3: Implement the two-session acceptance sequence

Before starting, call snapshot; refuse to run if existing sessions are present unless AllowReconnect is passed. Connect profiles A and B with unique temporary ids, poll until both are connected, verify independent durations and traffic counters, run Get-NetAdapter, Get-NetRoute -AddressFamily IPv4, and Get-DnsClientServerAddress, disconnect A, and assert B remains connected. Finally disconnect all temporary sessions and assert the snapshot is empty.

- [ ] Step 4: Add cleanup and optional restoration

Put temporary disconnects in a try/finally equivalent. If RestoreExisting is passed, reconnect captured sessions using original ids, names, profile ids, and route strategies. If cleanup fails, exit nonzero and print only session ids and resource counts.

- [ ] Step 5: Document the Windows test command

Add a command such as:

~~~powershell
.\tests\smoke\windows-installed.ps1 -ProfilesPath "$env:APPDATA\OpenVPN\profiles.json"
~~~

Document administrator requirements for the service, Wintun/TAP driver, two non-overlapping test profiles, saved Windows credentials, and the fact that real Windows validation cannot be replaced by macOS or fixture tests.

- [ ] Step 6: Commit the acceptance tool

~~~powershell
git add tests/smoke/windows-installed.ps1 tests/smoke/README.md
git commit -m "test: add Windows dual-session smoke acceptance"
~~~

## Task 7: Update Windows documentation without changing macOS interaction

**Files:**
- Modify README.md.
- Modify platform/windows/README.md.
- Modify packaging/windows/README.md.
- Modify docs/windows-acceptance.md.
- Modify docs/desktop-acceptance.md.

- [ ] Step 1: Replace obsolete capability statements

State that Windows and macOS both expose DESKTOP_MULTI; remove wording that the Windows agent owns only one TUN or that the expected second connection is rejected. Keep the unverified-real-machine disclaimer until the Windows acceptance script has actually passed.

- [ ] Step 2: Align the acceptance checklist with macOS semantics

Describe two simultaneous configurations, independent status/traffic, route conflict behavior, per-session teardown, service/daemon restart cleanup, and Wintun/TAP/DNS inspection. Keep existing Windows build prerequisites and PowerShell build commands.

- [ ] Step 3: Verify documentation consistency

Run:

~~~powershell
rg -n "单连接|只允许一条|不支持同时|single-session|one TUN|单隧道" README.md platform/windows packaging/windows docs
~~~

Expected: no obsolete capability claim remains; remaining mentions must be historical or test-description text explicitly explaining that the old guard was removed.

- [ ] Step 4: Commit the documentation update

~~~powershell
git add README.md platform/windows/README.md packaging/windows/README.md docs/windows-acceptance.md docs/desktop-acceptance.md
git commit -m "docs: document Windows desktop multi-session behavior"
~~~

## Task 8: Full verification and handoff

**Files:** Verify all changed files and the final Git diff. No additional source changes are expected in desktop/qt/.

- [ ] Step 1: Run portable controller and agent-patch tests

~~~powershell
cmake --build build/core-windows --config Release --parallel
ctest --test-dir build/core-windows -R "openvpn_daemon_session|windows_agent_patch" --output-on-failure
~~~

- [ ] Step 2: Run the full core and Qt suites

~~~powershell
ctest --test-dir build/core-windows --output-on-failure
cmake --build build/qt-windows --config Release --parallel
ctest --test-dir build/qt-windows --output-on-failure
~~~

If Qt dependencies are unavailable, report the exact configure/build command and missing dependency rather than claiming the UI suite passed.

- [ ] Step 3: Run the Windows real-machine smoke test

Run the documented PowerShell command on Windows 10/11 x64 with the service installed, Wintun available, and two authorized profiles. Capture pass/fail for each acceptance criterion without recording secrets.

- [ ] Step 4: Inspect final state before claiming completion

~~~powershell
git status --short --branch
git diff --check main..HEAD
rg -n "当前Windows版本暂不支持同时连接多条VPN|单连接预览" core desktop platform docs README.md
~~~

Confirm that macOS files are unchanged, the old Windows daemon cap is gone, all required tests have fresh output, and any unavailable Windows-only verification is explicitly listed as unverified.

- [ ] Step 5: Commit only after verification

Create the final integration commit only after all available checks pass and the real-machine result is recorded:

~~~powershell
git add core platform/windows tests/smoke README.md docs packaging/windows
git commit -m "feat: support macOS-compatible Windows multi-session VPNs"
~~~
