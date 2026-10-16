#!/bin/sh

set -eu

daemon_source="${1:-}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
label="com.openvpn.daemon"
legacy_label="com.vpn.daemon"
uid=$(id -u)
install_root="$HOME/Library/Application Support/OpenVPN"
daemon_destination="$install_root/openvpn_daemon"
plist_destination="$HOME/Library/LaunchAgents/$label.plist"
log_directory="$HOME/Library/Logs/OpenVPN"
socket_path="/tmp/openvpn-ui.sock"

if [ -z "$daemon_source" ] || [ ! -f "$daemon_source" ]; then
	printf '%s\n' "用法：$0 /绝对路径/openvpn_daemon" >&2
	exit 2
fi

/bin/mkdir -p "$install_root" "$(dirname -- "$plist_destination")" "$log_directory"
/usr/bin/install -m 0755 "$daemon_source" "$daemon_destination"
/bin/cp "$script_dir/$label.plist" "$plist_destination"
# ProgramArguments 是数组，使用 keypath ``ProgramArguments.0`` 在不同
# macOS 版本上可能被解释成“追加一个数组元素”，导致模板占位符残留，
# launchd 随后会用错误参数启动 daemon 并反复退出。一次性替换整个数组，
# 确保安装后的 plist 只包含真实可执行文件和 socket 参数。
/usr/bin/plutil -replace ProgramArguments -xml \
    "<array><string>$daemon_destination</string><string>--socket</string><string>$socket_path</string></array>" \
    "$plist_destination"
/usr/bin/plutil -replace StandardOutPath -string "$log_directory/daemon.log" "$plist_destination"
/usr/bin/plutil -replace StandardErrorPath -string "$log_directory/daemon-error.log" "$plist_destination"
/usr/bin/plutil -lint "$plist_destination" >/dev/null

echo "正在停止旧的 VPN 用户 daemon（如果存在）……"
/bin/launchctl bootout "gui/$uid/$legacy_label" >/dev/null 2>&1 || true
/bin/launchctl bootout "gui/$uid/$label" >/dev/null 2>&1 || true
/bin/rm -f "$socket_path"
/bin/launchctl bootstrap "gui/$uid" "$plist_destination"
/bin/launchctl enable "gui/$uid/$label"
/bin/launchctl kickstart -k "gui/$uid/$label"

attempt=0
while [ "$attempt" -lt 10 ] && [ ! -S "$socket_path" ]; do
	/bin/sleep 1
	attempt=$((attempt + 1))
done
if [ ! -S "$socket_path" ]; then
	echo "错误：OpenVPN 用户 daemon 未创建 Unix socket：$socket_path" >&2
	echo "请检查日志：$log_directory/daemon-error.log" >&2
	exit 1
fi
if ! /usr/bin/printf '%s\n' '{"command":"ping"}' |
	/usr/bin/nc -U "$socket_path" 2>/dev/null |
	/usr/bin/grep -q '"ok"[[:space:]]*:[[:space:]]*true'; then
	echo "错误：OpenVPN 用户 daemon socket 无法响应 ping：$socket_path" >&2
	echo "请检查日志：$log_directory/daemon-error.log" >&2
	exit 1
fi

echo "VPN 用户 daemon 已加载：$label"
echo "Unix socket：$socket_path"
echo "日志目录：$log_directory"
