#!/bin/bash
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cmake_bin=${1:-cmake}
bash -n "$repo_root/packaging/macos/build-release.sh"
bash -n "$repo_root/packaging/macos/build-pkg.sh"
release_version=$("$cmake_bin" -DOPENVPN_CONFIG_KEY=version -P "$repo_root/cmake/ProjectConfig.cmake")
test -n "$release_version"

if result=$(OPENVPN_CMAKE="$cmake_bin" bash "$repo_root/packaging/macos/build-release.sh" \
    unused unused "$release_version.invalid" unused 2>&1); then
    echo 'Mismatched macOS release version was accepted' >&2
    exit 1
fi
[[ "$result" == *'differs from project-config.json'* ]]
if result=$(OPENVPN_CMAKE="$cmake_bin" OPENVPN_VERSION="$release_version.invalid" \
    bash "$repo_root/packaging/macos/build-pkg.sh" unused unused unused 2>&1); then
    echo 'Mismatched macOS PKG version was accepted' >&2
    exit 1
fi
[[ "$result" == *'differs from project-config.json'* ]]
echo 'macOS packaging syntax and early version rejection tests passed'
