#!/bin/sh

set -eu
umask 022

app_source="${1:-}"
helper_source="${2:-}"
daemon_source="${3:-}"
output_path="${4:-}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
version="${OPENVPN_VERSION:-0.1.0}"

if [ -z "$app_source" ] || [ ! -d "$app_source" ] ||
	[ -z "$helper_source" ] || [ ! -f "$helper_source" ] ||
	[ -z "$daemon_source" ] || [ ! -f "$daemon_source" ]; then
	echo "用法：$0 /绝对路径/OpenVPN.app /绝对路径/openvpn_macos_agent /绝对路径/openvpn_daemon [输出.pkg]" >&2
	exit 2
fi

case "$version" in
	*[!0-9A-Za-z._-]*)
		echo "错误：版本号只能包含字母、数字、点、下划线和短横线。" >&2
		exit 2
		;;
esac

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
/usr/bin/ditto "$app_source" "$stage_directory/Applications/OpenVPN.app"
/usr/bin/install -m 0755 "$helper_source" \
	"$stage_directory/Library/PrivilegedHelperTools/com.openvpn.helper"
/usr/bin/install -m 0755 "$daemon_source" \
	"$stage_directory/Library/Application Support/OpenVPN/openvpn_daemon"
/usr/bin/install -m 0644 "$script_dir/com.openvpn.helper.plist" \
	"$stage_directory/Library/LaunchDaemons/com.openvpn.helper.plist"
/usr/bin/install -m 0644 "$script_dir/com.openvpn.daemon.package.plist" \
	"$stage_directory/Library/LaunchAgents/com.openvpn.daemon.plist"

/bin/mkdir -p "$(dirname -- "$output_path")"
echo "正在生成安装包：$output_path"
/usr/bin/pkgbuild \
	--root "$stage_directory" \
	--scripts "$script_dir/pkg-scripts" \
	--identifier com.openvpn.desktop \
	--version "$version" \
	--install-location / \
	--ownership recommended \
	"$output_path"

echo "OpenVPN 安装包已生成：$output_path"
