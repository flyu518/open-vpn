#!/usr/bin/env python3
"""部署服务依赖并检查整个安装载荷，禁止依赖开发机上的 Qt/Homebrew。"""

import argparse
from functools import lru_cache
import os
from pathlib import Path
import re
import shutil
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def dependencies(binary):
    identities = {line.strip() for line in run("/usr/bin/otool", "-D", str(binary)).splitlines()
                  if line.strip() and not line.rstrip().endswith(":" )}
    return [value for value in dict.fromkeys(re.findall(r"^\s+(.+?) \(compatibility version", run("/usr/bin/otool", "-L", str(binary)), re.M))
            if value not in identities]


def system_library(path):
    return path.startswith(("/usr/lib/", "/System/Library/"))


@lru_cache(maxsize=None)
def architectures(path):
    return set(run("/usr/bin/lipo", "-archs", str(path)).strip().split())


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def rpaths(binary, executable):
    values = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset", run("/usr/bin/otool", "-l", str(binary)))
    return [Path(value.replace("@loader_path", str(binary.parent)).replace("@executable_path", str(executable.parent))) for value in values]


def resolve(dependency, binary, executable):
    if dependency.startswith("@rpath/"):
        suffix = dependency[len("@rpath/"):]
        for base in [*rpaths(binary, executable), *rpaths(executable, executable)]:
            candidate = base / suffix
            if candidate.is_file():
                return candidate.resolve()
        raise RuntimeError(f"无法解析动态库：{binary}: {dependency}")
    candidate = Path(dependency.replace("@loader_path", str(binary.parent)).replace("@executable_path", str(executable.parent)))
    if not candidate.is_file():
        raise RuntimeError(f"缺少动态库：{binary}: {dependency}")
    return candidate.resolve()


def deploy_services(root):
    library_dir = root / "Library/Application Support/OpenVPN/lib"
    library_dir.mkdir(parents=True, exist_ok=True)
    executables = [root / "Library/PrivilegedHelperTools/com.openvpn.helper", root / "Library/Application Support/OpenVPN/openvpn_daemon"]
    copied = {}
    visited = set()

    def visit(source, destination, executable):
        if destination in visited:
            return
        visited.add(destination)
        for dependency in dependencies(source):
            if system_library(dependency):
                continue
            resolved = resolve(dependency, source, executable)
            if resolved == source.resolve():  # dylib 的 LC_ID_DYLIB
                continue
            name = resolved.name
            if name in copied and copied[name] != resolved:
                raise RuntimeError(f"同名动态库来源不一致：{name}")
            target = library_dir / name
            if name not in copied:
                copied[name] = resolved
                shutil.copyfile(resolved, target)
                target.chmod(0o755)
                visit(resolved, target, executable)
                run("/usr/bin/install_name_tool", "-id", f"@rpath/{name}", str(target))
                run("/usr/bin/codesign", "--force", "--sign", "-", str(target))
            relative = os.path.relpath(target, destination.parent)
            run("/usr/bin/install_name_tool", "-change", dependency, "@loader_path/" + relative, str(destination))

    for executable in executables:
        visit(executable, executable, executable)
        run("/usr/bin/codesign", "--force", "--sign", "-", str(executable))
    print(f"服务依赖部署完成：{len(copied)} 个动态库")


def verify(root, expected_architecture=None):
    root = root.resolve()
    app = root / "Applications/OpenVPN.app"
    app_executable = app / "Contents/MacOS/openvpn_qt"
    count = 0
    for binary in root.rglob("*"):
        if not macho(binary):
            continue
        count += 1
        if expected_architecture and expected_architecture not in architectures(binary):
            raise RuntimeError(
                f"架构不包含安装包目标 {expected_architecture}：{binary}: "
                f"{', '.join(sorted(architectures(binary)))}")
        executable = app_executable if binary.is_relative_to(app) else binary
        for dependency in dependencies(binary):
            if system_library(dependency):
                continue
            if dependency.startswith("/"):
                raise RuntimeError(f"安装载荷仍依赖开发机路径：{binary}: {dependency}")
            resolved = resolve(dependency, binary, executable)
            if not resolved.is_relative_to(root):
                raise RuntimeError(f"动态库未包含在安装载荷中：{binary}: {dependency}")
            if not architectures(binary).issubset(architectures(resolved)):
                raise RuntimeError(f"动态库架构不匹配：{binary}: {dependency}")
    if count == 0:
        raise RuntimeError("没有找到可验证的 Mach-O 产物")
    print(f"运行时检查通过：{count} 个 Mach-O 文件，所有非系统依赖均随包提供")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--architecture", choices=("x86_64", "arm64"))
    args = parser.parse_args()
    if not args.verify_only:
        deploy_services(args.stage)
    verify(args.stage, args.architecture)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        if isinstance(error, subprocess.CalledProcessError) and error.output:
            print(error.output.strip())
        raise SystemExit(f"打包失败：{error}") from error
