"""编译真实的小型 Mach-O 依赖链，验证运行时搬迁和遗漏检测。"""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("runtime", Path(__file__).parents[1] / "deploy-runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimeTests(unittest.TestCase):
    def test_missing_rpath_is_rejected_even_when_library_was_copied(self):
        with tempfile.TemporaryDirectory(prefix="openvpn-rpath-test-") as tmp:
            root = Path(tmp)
            directory = root / "Applications/OpenVPN.app/Contents"
            (directory / "Frameworks").mkdir(parents=True)
            (directory / "MacOS").mkdir()
            (root / "leaf.c").write_text("int leaf(void) { return 42; }\n")
            (root / "main.c").write_text("extern int leaf(void); int main(void) { return leaf() != 42; }\n")
            library = directory / "Frameworks/libleaf.dylib"
            executable = directory / "MacOS/openvpn_qt"
            subprocess.run(["/usr/bin/cc", "-dynamiclib", str(root / "leaf.c"), "-o", str(library), "-Wl,-install_name,@rpath/libleaf.dylib"], check=True)
            command = ["/usr/bin/cc", str(root / "main.c"), str(library), "-o", str(executable)]
            subprocess.run(command, check=True)
            with self.assertRaisesRegex(RuntimeError, "无法解析动态库"):
                runtime.verify(root)
            subprocess.run(command + ["-Wl,-rpath,@executable_path/../Frameworks"], check=True)
            runtime.verify(root)
            subprocess.run([str(executable)], check=True)

    def test_deploy_transitive_dependencies_and_run_without_source_libraries(self):
        with tempfile.TemporaryDirectory(prefix="openvpn-runtime-test-") as tmp:
            base = Path(tmp)
            source = base / "开发 依赖"
            source.mkdir()
            (source / "base.c").write_text("int leaf(void) { return 42; }\n")
            (source / "middle.c").write_text("extern int leaf(void); int answer(void) { return leaf(); }\n")
            (source / "main.c").write_text("extern int answer(void); int main(void) { return answer() != 42; }\n")
            leaf = source / "libleaf.dylib"
            middle = source / "libmiddle.dylib"
            subprocess.run(["/usr/bin/cc", "-dynamiclib", str(source / "base.c"), "-o", str(leaf), "-Wl,-headerpad_max_install_names"], check=True)
            subprocess.run(["/usr/bin/cc", "-dynamiclib", str(source / "middle.c"), str(leaf), "-o", str(middle), "-Wl,-headerpad_max_install_names"], check=True)
            stage = base / "stage"
            targets = [stage / "Library/PrivilegedHelperTools/com.openvpn.helper", stage / "Library/Application Support/OpenVPN/openvpn_daemon"]
            for target in targets:
                target.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(["/usr/bin/cc", str(source / "main.c"), str(middle), "-o", str(target), "-Wl,-headerpad_max_install_names"], check=True)
            with self.assertRaisesRegex(RuntimeError, "开发机路径"):
                runtime.verify(stage)
            runtime.deploy_services(stage)
            source.rename(base / "不可用的开发依赖")
            runtime.verify(stage)
            for target in targets:
                subprocess.run([str(target)], check=True)
            (stage / "Library/Application Support/OpenVPN/lib/libleaf.dylib").unlink()
            with self.assertRaisesRegex(RuntimeError, "缺少动态库|无法解析动态库"):
                runtime.verify(stage)


if __name__ == "__main__":
    unittest.main()
