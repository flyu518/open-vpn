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

if [ -z "$daemon_source" ] || [ ! -f "$daemon_source" ]; then
	printf '%s\n' "用法：$0 /绝对路径/openvpn_daemon" >&2
	exit 2
fi

/bin/mkdir -p "$install_root" "$(dirname -- "$plist_destination")" "$log_directory"
/usr/bin/install -m 0755 "$daemon_source" "$daemon_destination"
/bin/cp "$script_dir/$label.plist" "$plist_destination"
/usr/bin/plutil -replace ProgramArguments.0 -string "$daemon_destination" "$plist_destination"
/usr/bin/plutil -replace StandardOutPath -string "$log_directory/daemon.log" "$plist_destination"
/usr/bin/plutil -replace StandardErrorPath -string "$log_directory/daemon-error.log" "$plist_destination"
/usr/bin/plutil -lint "$plist_destination" >/dev/null

echo "正在停止旧的 VPN 用户 daemon（如果存在）……"
/bin/launchctl bootout "gui/$uid/$legacy_label" >/dev/null 2>&1 || true
/bin/launchctl bootout "gui/$uid/$label" >/dev/null 2>&1 || true
/bin/launchctl bootstrap "gui/$uid" "$plist_destination"
/bin/launchctl enable "gui/$uid/$label"
/bin/launchctl kickstart -k "gui/$uid/$label"

echo "VPN 用户 daemon 已加载：$label"
echo "Unix socket：/tmp/openvpn-ui.sock"
echo "日志目录：$log_directory"
