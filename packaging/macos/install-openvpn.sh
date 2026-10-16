#!/bin/sh

set -eu

app_source="${1:-}"
helper_source="${2:-}"
daemon_source="${3:-}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
uid=$(id -u)

if [ "$(id -u)" -eq 0 ]; then
	printf '%s\n' "错误：请用普通用户执行此脚本，脚本会在需要时单独请求 sudo。" >&2
	exit 1
fi

if [ -z "$app_source" ] || [ ! -d "$app_source" ] ||
	[ -z "$helper_source" ] || [ ! -f "$helper_source" ] ||
	[ -z "$daemon_source" ] || [ ! -f "$daemon_source" ]; then
	printf '%s\n' "用法：$0 /绝对路径/OpenVPN.app /绝对路径/openvpn_macos_agent /绝对路径/openvpn_daemon" >&2
	exit 2
fi

echo "正在安装 OpenVPN.app……"
/usr/bin/sudo /usr/bin/ditto "$app_source" "/Applications/OpenVPN.app"

echo "正在安装高权限 OpenVPN helper（会请求一次管理员密码）……"
/usr/bin/sudo "$script_dir/install-helper.sh" "$helper_source"

echo "正在安装当前用户的 OpenVPN daemon……"
"$script_dir/install-daemon.sh" "$daemon_source"

echo "正在检查 OpenVPN 服务……"
/bin/launchctl print "system/com.openvpn.helper" >/dev/null
/bin/launchctl print "gui/$uid/com.openvpn.daemon" >/dev/null
if [ ! -S /tmp/openvpn-ui.sock ]; then
	echo "错误：OpenVPN 用户 daemon 已加载，但 socket 尚未出现：/tmp/openvpn-ui.sock" >&2
	exit 1
fi
if [ ! -S /var/run/openvpn.sock ]; then
	echo "错误：OpenVPN helper 已加载，但 socket 尚未出现：/var/run/openvpn.sock" >&2
	exit 1
fi

echo "OpenVPN 安装完成。"
echo "应用：/Applications/OpenVPN.app"
echo "用户 socket：/tmp/openvpn-ui.sock"
echo "helper socket：/var/run/openvpn.sock"
