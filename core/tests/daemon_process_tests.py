"""Exercise disposable child daemons only; never use installed sockets or profiles."""

import argparse
import concurrent.futures
import contextlib
import json
import os
import pathlib
import signal
import socket
import subprocess
import tempfile
import time
import unittest


class Client:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(8)
        try:
            self.socket.connect(path)
        except Exception:
            self.socket.close()
            raise
        self.pending = b""
        self.events = []

    def close(self):
        self.socket.close()

    def send(self, value):
        self.socket.sendall(json.dumps(value).encode() + b"\n")

    def frame(self):
        while b"\n" not in self.pending:
            chunk = self.socket.recv(65536)
            if not chunk:
                raise AssertionError("daemon 在响应前关闭了客户端连接")
            self.pending += chunk
        line, self.pending = self.pending.split(b"\n", 1)
        value = json.loads(line)
        if not isinstance(value, dict):
            raise AssertionError("daemon 输出了非对象类型的帧")
        return value

    def reply(self):
        while True:
            value = self.frame()
            if value.get("type") == "sessionEvent":
                self.events.append(value)
            else:
                return value

    def request(self, value):
        self.send(value)
        return self.reply()


class ChildDaemon:
    def __init__(self, binary):
        # /tmp keeps sockaddr_un paths short even on macOS.
        self.directory = tempfile.TemporaryDirectory(prefix="ovpn-ipc-", dir="/tmp")
        self.path = os.path.join(self.directory.name, "daemon.sock")
        self.log = tempfile.TemporaryFile()
        self.process = subprocess.Popen(
            [binary, "--socket", self.path], stdout=self.log, stderr=self.log
        )
        deadline = time.monotonic() + 8
        while True:
            if self.process.poll() is not None or time.monotonic() > deadline:
                self.close()
                raise AssertionError("daemon 子进程未能启动")
            if pathlib.Path(self.path).exists():
                try:
                    with self.client() as client:
                        if client.request({"command": "ping"}).get("ok") is True:
                            break
                except (ConnectionRefusedError, FileNotFoundError):
                    pass
            time.sleep(0.01)

    @contextlib.contextmanager
    def client(self):
        client = Client(self.path)
        try:
            yield client
        finally:
            client.close()

    def close(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
        try:
            code = self.process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise AssertionError("daemon 关闭时发生悬挂")
        finally:
            self.log.seek(0)
            log = self.log.read().decode(errors="replace")
            self.log.close()
            self.directory.cleanup()
        if code != 0:
            raise AssertionError(f"daemon 异常退出，退出码 {code}：{log[-4000:]}")

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class ProductionIPC(unittest.TestCase):
    def test_malformed_json_and_field_types_leave_daemon_alive(self):
        base = {"command": "connect", "sessionId": "test", "profile": "invalid-profile",
                "credentials": {"username": "synthetic", "password": "synthetic"}}
        cases = [b"[\n", b"[]\n", b"null\n", b"true\n", b"42\n", b'"text"\n',
                 b'{"command":[]}\n', b'{"command":"ping"} trailing\n',
                 b'{"command":"disconnect","sessionId":{}}\n',
                 b'{"command":"forgetCredentials","profileId":[]}\n']
        for key, value in [("sessionId", []), ("profile", {}), ("credentials", []),
                           ("credentials", {"password": []}), ("name", {}),
                           ("profileId", []), ("rememberCredentials", []),
                           ("routeStrategy", []),
                           ("routeStrategy", {"conflictMode": []})]:
            cases.append(json.dumps(dict(base, **{key: value})).encode() + b"\n")
        for payload in cases:
            with self.subTest(payload=payload), ChildDaemon(DAEMON) as daemon:
                with daemon.client() as client:
                    client.socket.sendall(payload)
                    self.assertIs(client.reply()["ok"], False)
                    self.assertIs(client.request({"command": "ping"})["ok"], True)
                with daemon.client() as client:
                    self.assertEqual(client.request({"command": "snapshot"})["sessions"], [])

    def test_interrupted_clients_do_not_stop_daemon(self):
        with ChildDaemon(DAEMON) as daemon, daemon.client() as healthy:
            for payload in [json.dumps({"command": "x" * 900000}).encode() + b"\n",
                            (b"[\n" * 10000)]:
                with daemon.client() as broken:
                    try:
                        broken.socket.sendall(payload)
                    except BrokenPipeError:
                        # A non-reading peer may be evicted by the send timeout.
                        # Only this deliberately broken client may fail.
                        pass
                for _ in range(30):
                    self.assertIs(healthy.request({"command": "ping"})["ok"], True)
                    time.sleep(0.02)
            with daemon.client() as partial:
                partial.socket.sendall(b'{"command":')
            self.assertIs(healthy.request({"command": "ping"})["ok"], True)

    def test_invalid_profile_failure_and_missing_disconnect_are_isolated(self):
        with ChildDaemon(DAEMON) as daemon, daemon.client() as client:
            self.assertIs(client.request({"command": "connect", "sessionId": "invalid",
                "profile": "not-an-openvpn-profile", "credentials": {
                    "username": "synthetic", "password": "synthetic"}})["ok"], False)
            self.assertIs(client.request({"command": "disconnect", "sessionId": "absent"})["ok"], True)
            self.assertIs(client.request({"command": "ping"})["ok"], True)
            self.assertEqual(client.request({"command": "snapshot"})["sessions"], [])

    def test_fragmented_and_coalesced_frames(self):
        with ChildDaemon(DAEMON) as daemon, daemon.client() as client:
            client.socket.sendall(b'{"comm')
            client.socket.sendall(b'and":"ping"}\n{"command":"snapshot"}\n')
            self.assertIs(client.reply()["ok"], True)
            self.assertEqual(client.reply()["sessions"], [])

    def test_shutdown_with_idle_and_partial_clients(self):
        with ChildDaemon(DAEMON) as daemon:
            clients = []
            try:
                for _ in range(16):
                    client = Client(daemon.path)
                    clients.append(client)
                    self.assertIs(client.request({"command": "ping"})["ok"], True)
                    client.socket.sendall(b'{"command":')
                daemon.process.send_signal(signal.SIGTERM)
                self.assertEqual(daemon.process.wait(timeout=5), 0)
                self.assertFalse(pathlib.Path(daemon.path).exists())
            finally:
                for client in clients:
                    client.close()

    def test_oversized_partial_frame_only_disconnects_its_sender(self):
        with ChildDaemon(DAEMON) as daemon, daemon.client() as healthy:
            with daemon.client() as oversized:
                try:
                    oversized.socket.sendall(b"x" * (1024 * 1024 + 8192))
                except BrokenPipeError:
                    pass
                try:
                    self.assertEqual(oversized.socket.recv(1), b"")
                except ConnectionResetError:
                    pass
            self.assertIs(healthy.request({"command": "ping"})["ok"], True)


def connect_request(session_id, profile="fixture:connected"):
    return {"command": "connect", "sessionId": session_id, "profile": profile,
            "credentials": {"username": "synthetic", "password": "synthetic"}}


class FixtureIPC(unittest.TestCase):
    """Real daemon/controller, link-time inert engine + credential store in a child."""

    def test_multi_session_state_stats_and_embedded_route_warning(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            self.assertIs(client.request(connect_request("a"))["ok"], True)
            self.assertIs(client.request(connect_request("b", "fixture:warning"))["ok"], True)
            sessions = {s["sessionId"]: s for s in client.request({"command": "snapshot"})["sessions"]}
            self.assertEqual(sessions["a"]["state"], "connected")
            self.assertEqual(sessions["a"]["downloadBytes"], 111)
            self.assertEqual(sessions["b"]["downloadBytes"], 222)
            self.assertEqual(sessions["b"]["uploadBytes"], 22)
            self.assertEqual(sessions["a"]["warnings"], [])
            self.assertEqual(len(sessions["b"]["warnings"]), 1)
            conflicts = sessions["b"].get("routeConflicts", [])
            self.assertEqual(len(conflicts), 2)
            self.assertEqual(conflicts[0]["peerSessionId"], "a")
            self.assertEqual(conflicts[0]["candidateRoute"], "10.20.0.0/16")
            self.assertEqual(conflicts[0]["activeRoute"], "10.0.0.0/8")
            self.assertEqual(conflicts[0]["action"], "skipped")
            self.assertIs(client.request({"command": "disconnect", "sessionId": "a"})["ok"], True)
            remaining = client.request({"command": "snapshot"})["sessions"]
            self.assertEqual([s["sessionId"] for s in remaining], ["b"])
            self.assertEqual(remaining[0]["downloadBytes"], 222)
            self.assertEqual(remaining[0]["routeConflicts"], conflicts)

    def test_strict_conflict_keeps_peer_and_route_details(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            self.assertIs(client.request(connect_request("a"))["ok"], True)
            client.request(connect_request("b", "fixture:strict-conflict"))
            sessions = {s["sessionId"]: s for s in client.request({"command": "snapshot"})["sessions"]}
            self.assertEqual(sessions["b"]["state"], "failed")
            conflicts = sessions["b"].get("routeConflicts", [])
            self.assertEqual(len(conflicts), 1)
            self.assertEqual(conflicts[0]["peerSessionId"], "a")
            self.assertEqual(conflicts[0]["action"], "blocked")

    def test_external_peer_is_not_mistaken_for_local_same_id(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            client.request(connect_request("a"))
            client.request(connect_request("b", "fixture:external-conflict"))
            sessions = {s["sessionId"]: s for s in client.request({"command": "snapshot"})["sessions"]}
            conflict = sessions["b"]["routeConflicts"][0]
            self.assertEqual(conflict["peerSessionId"], "")
            self.assertIs(conflict["external"], True)
            self.assertEqual(conflict["peerName"], "其他 VPN 客户端")

    def test_failure_is_not_overwritten_by_disconnected_or_elapsed_duration(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            result = client.request(connect_request("failed", "fixture:failure"))
            self.assertIs(result["ok"], True)
            first = client.request({"command": "snapshot"})["sessions"][0]
            self.assertEqual(first["state"], "failed")
            self.assertNotIn("synthetic-secret", first["error"])
            time.sleep(1.1)
            second = client.request({"command": "snapshot"})["sessions"][0]
            self.assertEqual(first["durationSeconds"], second["durationSeconds"])

    def test_disconnect_exception_does_not_stop_other_sessions(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            self.assertIs(client.request(connect_request("a"))["ok"], True)
            self.assertIs(client.request(connect_request("bad", "fixture:stop-error"))["ok"], True)
            self.assertIs(client.request({"command": "disconnect", "sessionId": "bad"})["ok"], False)
            self.assertIs(client.request({"command": "ping"})["ok"], True)
            self.assertEqual([s["sessionId"] for s in client.request({"command": "snapshot"})["sessions"]], ["a"])

    def test_concurrent_connect_disconnect_same_id(self):
        with ChildDaemon(FIXTURE) as daemon:
            def requests(connecting):
                with daemon.client() as client:
                    for _ in range(40):
                        value = connect_request("race", "fixture:slow-start") if connecting else {
                            "command": "disconnect", "sessionId": "race"}
                        self.assertIn("ok", client.request(value))
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                list(pool.map(requests, [True, False, True, False]))
            with daemon.client() as client:
                self.assertIs(client.request({"command": "ping"})["ok"], True)

    def test_response_and_broadcast_frames_do_not_interleave(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            self.assertIs(client.request(connect_request("burst", "fixture:burst"))["ok"], True)
            for _ in range(24):
                self.assertIs(client.request({"command": "x" * 196608})["ok"], False)
            self.assertTrue(client.events)
            for event in client.events:
                self.assertEqual(event["sessionId"], "burst")
            self.assertIs(client.request({"command": "ping"})["ok"], True)

    def test_nonreading_client_does_not_hang_shutdown(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            client.send(connect_request("burst", "fixture:burst"))
            time.sleep(0.1)
            daemon.process.send_signal(signal.SIGTERM)
            self.assertEqual(daemon.process.wait(timeout=5), 0)

    def test_idle_client_threads_are_joined_before_daemon_destruction(self):
        with ChildDaemon(FIXTURE) as daemon, daemon.client() as client:
            self.assertIs(client.request({"command": "ping"})["ok"], True)
            daemon.process.send_signal(signal.SIGTERM)
            # The fixture entrypoint checks actual Mach thread count AFTER the
            # production daemon destructor, while this client is still open.
            self.assertEqual(daemon.process.wait(timeout=5), 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--daemon", required=True)
    parser.add_argument("--fixture-daemon", required=True)
    parser.add_argument("--suite", choices=["production", "fixture"], required=True)
    args, remaining = parser.parse_known_args()
    DAEMON, FIXTURE = args.daemon, args.fixture_daemon
    suite = ProductionIPC if args.suite == "production" else FixtureIPC
    unittest.main(argv=[__file__] + remaining, defaultTest=suite.__name__, verbosity=2)
