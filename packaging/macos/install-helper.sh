#!/bin/sh

set -eu

helper_source="${1:-}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
plist_source="$script_dir/com.vpn.helper.plist"
helper_destination="/Library/PrivilegedHelperTools/com.vpn.helper"
plist_destination="/Library/LaunchDaemons/com.vpn.helper.plist"
log_directory="/var/log/vpn"
label="com.vpn.helper"

if [ "$(id -u)" -ne 0 ]; then
    echo "错误：安装 macOS VPN helper 需要管理员权限，请使用 sudo 执行。" >&2
    exit 1
fi

if [ -z "$helper_source" ] || [ ! -f "$helper_source" ]; then
    echo "用法：sudo $0 /绝对路径/vpn_macos_agent" >&2
    exit 2
fi

if ! /usr/bin/plutil -lint "$plist_source" >/dev/null; then
    echo "错误：launchd 配置文件校验失败：$plist_source" >&2
    exit 3
fi

echo "正在停止旧的 VPN helper（如果存在）……"
/bin/launchctl bootout system/"$label" >/dev/null 2>&1 || true

echo "正在创建日志目录……"
/bin/mkdir -p "$log_directory"
/usr/sbin/chown root:wheel "$log_directory"
/bin/chmod 0750 "$log_directory"

echo "正在安装高权限 helper……"
/usr/bin/install -o root -g wheel -m 0755 "$helper_source" "$helper_destination"
/usr/bin/install -o root -g wheel -m 0644 "$plist_source" "$plist_destination"

echo "正在加载 launchd 服务……"
/bin/launchctl bootstrap system "$plist_destination"
/bin/launchctl enable system/"$label"
/bin/launchctl kickstart -k system/"$label"

echo "macOS VPN helper 已加载：$label"
echo "Unix socket：/var/run/vpn.sock"
echo "日志目录：$log_directory"
