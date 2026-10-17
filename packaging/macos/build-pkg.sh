#!/bin/sh

set -eu
umask 022
export COPYFILE_DISABLE=1

app_source="${1:-}"
helper_source="${2:-}"
daemon_source="${3:-}"
output_path="${4:-}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
version="${OPENVPN_VERSION:-0.2.13}"

if [ -z "$app_source" ] || [ ! -d "$app_source" ] ||
	[ -z "$helper_source" ] || [ ! -f "$helper_source" ] ||
	[ -z "$daemon_source" ] || [ ! -f "$daemon_source" ]; then
	echo "用法：$0 /绝对路径/OpenVPN.app /绝对路径/openvpn_macos_agent /绝对路径/openvpn_daemon [输出.pkg]" >&2
	exit 2
fi

# 防止把旧版 helper（内部仍使用 /var/run/vpn.sock）误打进新安装包。
# 安装脚本和当前源码统一使用 /var/run/openvpn.sock。
helper_strings=$(/usr/bin/strings "$helper_source" 2>/dev/null || true)
if ! printf '%s\n' "$helper_strings" | /usr/bin/grep -Fxq '/var/run/openvpn.sock' ||
	printf '%s\n' "$helper_strings" | /usr/bin/grep -Fxq '/var/run/vpn.sock'; then
	echo "错误：helper 不是当前 OpenVPN 构建产物，socket 路径必须是 /var/run/openvpn.sock。" >&2
	echo "请从当前 open-vpn/core 重新构建 openvpn_macos_agent 后再打包。" >&2
	exit 2
fi
daemon_strings=$(/usr/bin/strings "$daemon_source" 2>/dev/null || true)
if ! printf '%s\n' "$daemon_strings" | /usr/bin/grep -Fxq '/var/run/openvpn.sock' ||
	printf '%s\n' "$daemon_strings" | /usr/bin/grep -Fxq '/var/run/vpn.sock'; then
	echo "错误：daemon 与当前 helper 不兼容，请从当前源码重新构建 openvpn_daemon。" >&2
	exit 2
fi

case "$version" in
	*[!0-9A-Za-z._-]*)
		echo "错误：版本号只能包含字母、数字、点、下划线和短横线。" >&2
		exit 2
		;;
esac

bundle_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
	"$app_source/Contents/Info.plist" 2>/dev/null || true)
if [ -z "$bundle_version" ] || [ "$bundle_version" != "$version" ]; then
	echo "错误：安装包版本 $version 与应用 Bundle 版本 ${bundle_version:-未知} 不一致。" >&2
	exit 2
fi

if [ -z "$output_path" ]; then
	output_path="$project_root/packaging/macos/dist/OpenVPN-$version.pkg"
fi

stage_directory=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/openvpn-pkg.XXXXXX")
cleanup() {
	/bin/rm -rf "$stage_directory"
}
trap cleanup EXIT INT TERM

/bin/mkdir -p \
	"$stage_directory/Applications" \
	"$stage_directory/Library/Application Support/OpenVPN" \
	"$stage_directory/Library/LaunchAgents" \
	"$stage_directory/Library/LaunchDaemons" \
	"$stage_directory/Library/PrivilegedHelperTools"

echo "正在整理 OpenVPN.app、daemon 和 helper……"
/usr/bin/ditto --noextattr --norsrc "$app_source" "$stage_directory/Applications/OpenVPN.app"
/usr/bin/install -m 0755 "$helper_source" \
	"$stage_directory/Library/PrivilegedHelperTools/com.openvpn.helper"
/usr/bin/install -m 0755 "$daemon_source" \
	"$stage_directory/Library/Application Support/OpenVPN/openvpn_daemon"
/usr/bin/install -m 0644 "$script_dir/com.openvpn.helper.plist" \
	"$stage_directory/Library/LaunchDaemons/com.openvpn.helper.plist"
/usr/bin/install -m 0644 "$script_dir/com.openvpn.daemon.package.plist" \
	"$stage_directory/Library/LaunchAgents/com.openvpn.daemon.plist"

# 服务同样需要携带 OpenSSL、LZ4、fmt、jsoncpp，不能依赖用户安装 Homebrew。
if [ -n "${OPENVPN_PACKAGE_ARCH:-}" ]; then
	python3 "$script_dir/deploy-runtime.py" "$stage_directory" \
		--architecture "$OPENVPN_PACKAGE_ARCH"
else
	python3 "$script_dir/deploy-runtime.py" "$stage_directory"
fi

# 记录打包来源，便于确认安装载荷与验收产物一致。
/usr/bin/shasum -a 256 "$stage_directory/Library/PrivilegedHelperTools/com.openvpn.helper" \
	"$stage_directory/Library/Application Support/OpenVPN/openvpn_daemon"

/bin/mkdir -p "$(dirname -- "$output_path")"
echo "正在生成安装包：$output_path"
/usr/bin/pkgbuild \
	--root "$stage_directory" \
	--component-plist "$script_dir/com.openvpn.component.plist" \
	--scripts "$script_dir/pkg-scripts" \
	--identifier com.openvpn.desktop \
	--version "$version" \
	--install-location / \
	--ownership recommended \
	"$output_path"

echo "OpenVPN 安装包已生成：$output_path"
