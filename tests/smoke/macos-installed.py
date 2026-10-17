#!/usr/bin/env python3
"""验证已安装服务的真实双连接；只使用安全存储凭据，不记录配置或密码。"""
import argparse
import json
from pathlib import Path
import socket
import time
import uuid


def request(payload):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(15)
        connection.connect("/tmp/openvpn-ui.sock")
        connection.sendall(json.dumps(payload).encode() + b"\n")
        stream = connection.makefile("rb")
        while True:
            line = stream.readline()
            if not line:
                raise RuntimeError("后台服务在响应前关闭连接")
            value = json.loads(line)
            if value.get("type") == "sessionEvent":
                continue
            if not value.get("ok"):
                raise RuntimeError(value.get("message", "请求失败"))
            return value


def sessions():
    return {value["sessionId"]: value for value in request({"command": "snapshot"})["sessions"]}


def wait_connected(ids, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        current = sessions()
        if all(current.get(key, {}).get("state") == "connected" for key in ids):
            return current
        for key in ids:
            value = current.get(key, {})
            if value.get("state") == "failed":
                raise RuntimeError(value.get("error", "VPN 连接失败"))
        time.sleep(0.5)
    raise RuntimeError("VPN 连接超时")


def profile_contents(profile, index_path):
    if profile.get("managedFile"):
        return (index_path.parent / "profiles" / profile["managedFile"]).read_text()
    return profile.get("profile") or Path(profile["path"]).read_text()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", type=Path, default=Path.home() / "Library/Application Support/OpenVPN/profiles.json")
    parser.add_argument("--timeout", type=int, default=130)
    parser.add_argument("--allow-reconnect", action="store_true", help="用户确认可短暂断开后恢复已有连接")
    parser.add_argument("--restore-imported", action="store_true", help="安装验收后恢复原先已导入的配置（需用户已授权）")
    args = parser.parse_args()
    baseline = sessions()
    if baseline and not args.allow_reconnect:
        raise RuntimeError("当前存在用户会话，停止测试以避免影响现有连接")
    profiles = json.loads(args.profiles.read_text())["profiles"]
    if len(profiles) < 2:
        raise RuntimeError("需要两份已导入且有已保存凭据的配置")
    ids = []
    original_profiles = {profile["id"]: profile for profile in profiles}
    if any(key not in original_profiles for key in baseline):
        raise RuntimeError("存在无法从本地索引恢复的会话，未开始断开测试")
    restore = baseline or ({profile["id"]: {"name": profile.get("name", profile["id"])} for profile in profiles[:2]}
                           if args.restore_imported else {})
    try:
        for key in baseline:
            request({"command": "disconnect", "sessionId": key})
        for index, profile in enumerate(profiles[:2]):
            session_id = "acceptance-" + uuid.uuid4().hex[:12]
            ids.append(session_id)
            contents = profile_contents(profile, args.profiles)
            request({"command": "connect", "sessionId": session_id,
                     "profileId": profile["id"], "name": f"验收连接 {index + 1}",
                     "profile": contents, "credentials": {},
                     "rememberCredentials": False,
                     "routeStrategy": profile.get("routeStrategy", {"conflictMode": "compatible"})})
            wait_connected(ids, args.timeout)
            print(f"真实连接 {index + 1} 已建立，已保存凭据可用", flush=True)
        time.sleep(3)
        current = sessions()
        for key in ids:
            value = current[key]
            if value.get("durationSeconds", 0) <= 0 or value.get("downloadBytes", 0) <= 0 or value.get("uploadBytes", 0) <= 0:
                raise RuntimeError("已连接会话的计时或流量没有更新")
        print("两个 VPN 同时连接，持续时间和上下行计数均大于零", flush=True)
        request({"command": "disconnect", "sessionId": ids[0]})
        current = sessions()
        if ids[0] in current or current[ids[1]]["state"] != "connected":
            raise RuntimeError("断开第一条后，第二条连接受到了影响")
        print("第一条独立断开，第二条继续保持连接", flush=True)
    finally:
        for key in ids:
            request({"command": "disconnect", "sessionId": key})
        if sessions():
            raise RuntimeError("测试结束后仍有会话未清理")
        print("测试会话已全部断开，配置和已保存凭据未修改", flush=True)
        restored = []
        for key, previous in restore.items():
            profile = original_profiles[key]
            contents = profile_contents(profile, args.profiles)
            request({"command": "connect", "sessionId": key,
                     "profileId": profile["id"], "name": previous.get("name", profile.get("name", key)),
                     "profile": contents, "credentials": {}, "rememberCredentials": False,
                     "routeStrategy": profile.get("routeStrategy", {"conflictMode": "compatible"})})
            restored.append(key)
            wait_connected(restored, args.timeout)
        if restored:
            print(f"已恢复原先的 {len(restored)} 条连接", flush=True)


if __name__ == "__main__":
    main()
