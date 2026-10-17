#!/bin/bash
# 从同一工作树生成并验证完整安装包，不复用历史名称的二进制。
set -euo pipefail

qt_root="${1:?用法：build-release.sh Qt目录 构建目录 版本号 xxHash目录 [架构] [Homebrew前缀]}"
build_root="${2:?需要构建目录}"
release_version="${3:?需要版本号}"
xxhash_root="${4:?需要xxHash目录}"
release_arch="${5:-x86_64}"
brew_prefix="${6:-/usr/local}"
case "$release_arch" in
    x86_64|arm64) ;;
    *) echo "架构必须是 x86_64 或 arm64：$release_arch" >&2; exit 2 ;;
esac
cmake_bin="${OPENVPN_CMAKE:-cmake}"
cmake_bin=$(command -v "$cmake_bin")
ctest_bin="${cmake_bin%/*}/ctest"
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(cd -- "$script_dir/../.." && pwd)

"$cmake_bin" -S "$repo_root/core" -B "$build_root/core" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$release_arch" \
    -DOPENVPN_BREW_PREFIX="$brew_prefix" \
    -DOPENSSL_ROOT_DIR="$brew_prefix/opt/openssl" \
    -DOPENVPN3_SOURCE_DIR="$repo_root/third_party/openvpn3" \
    -DXXHASH_ROOT_DIR="$xxhash_root"
"$cmake_bin" --build "$build_root/core" --parallel 4
"$ctest_bin" --test-dir "$build_root/core" --output-on-failure

"$cmake_bin" -S "$repo_root/desktop/qt" -B "$build_root/qt" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$release_arch" \
    -DCMAKE_PREFIX_PATH="$qt_root" -DOPENVPN_APP_VERSION="$release_version"
"$cmake_bin" --build "$build_root/qt" --parallel 4
QT_QPA_PLATFORM=offscreen "$ctest_bin" --test-dir "$build_root/qt" --output-on-failure

app="$build_root/qt/openvpn_qt.app"
deploy_args=()
# 只部署本程序使用的平台、图标、图片和 TLS 插件，避免携带无用数据库驱动。
for plugin in platforms/libqcocoa.dylib iconengines/libqsvgicon.dylib \
    imageformats/libqsvg.dylib imageformats/libqjpeg.dylib imageformats/libqgif.dylib \
    imageformats/libqico.dylib tls/libqsecuretransportbackend.dylib; do
    /bin/mkdir -p "$app/Contents/PlugIns/$(dirname -- "$plugin")"
    /usr/bin/install -m 0755 "$qt_root/plugins/$plugin" "$app/Contents/PlugIns/$plugin"
    deploy_args+=("-executable=$app/Contents/PlugIns/$plugin")
done
"$qt_root/bin/macdeployqt" "$app" -no-plugins -no-strip \
    "-qmldir=$repo_root/desktop/qt/qml" "${deploy_args[@]}"

OPENVPN_VERSION="$release_version" OPENVPN_PACKAGE_ARCH="$release_arch" \
    "$script_dir/build-pkg.sh" "$app" \
    "$build_root/core/openvpn_macos_agent" "$build_root/core/openvpn_daemon" \
    "$build_root/OpenVPN-$release_version-macos-$release_arch.pkg"
echo "已生成并验证：$build_root/OpenVPN-$release_version-macos-$release_arch.pkg"
