"""用真实 Unix socket 验证安装程序不会删除活动 socket，含 macOS 路径别名。"""
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest


class SocketGuardTests(unittest.TestCase):
    def test_live_alias_is_preserved_and_closed_socket_is_removed(self):
        source = (Path(__file__).parents[1] / "pkg-scripts/postinstall").read_text()
        body = source.split("remove_stale_socket() {", 1)[1].split("\n}\n", 1)[0]
        script = 'set -eu\nremove_stale_socket() {' + body + '\n}\nremove_stale_socket "$1"\n'
        with tempfile.TemporaryDirectory(dir="/private/tmp", prefix="vpn-sock-") as tmp:
            physical = Path(tmp) / "service.sock"
            alias = Path("/tmp") / Path(tmp).name / "service.sock"
            with socket.socket(socket.AF_UNIX) as listener:
                listener.bind(str(physical))
                listener.listen(8)
                result = subprocess.run(["/bin/sh", "-c", script, "socket-guard", str(alias)], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(physical.exists(), result.stdout + result.stderr)
            result = subprocess.run(["/bin/sh", "-c", script, "socket-guard", str(alias)], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(physical.exists())


if __name__ == "__main__":
    unittest.main()
