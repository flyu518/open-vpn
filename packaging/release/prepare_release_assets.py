#!/usr/bin/env python3
"""Validate staged cross-platform installers and write shared release metadata."""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


class ReleaseAssetError(ValueError):
    """Raised when a release draft is incomplete or inconsistent."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _verify_sidecar(package: Path, digest: str) -> None:
    sidecar = package.with_name(package.name + ".sha256")
    if not sidecar.exists():
        return

    try:
        parts = sidecar.read_text(encoding="utf-8-sig").split()
    except UnicodeError as error:
        raise ReleaseAssetError(f"checksum sidecar is not valid text: {sidecar.name}") from error
    if not parts or parts[0].lower() != digest:
        raise ReleaseAssetError(f"checksum sidecar does not match {package.name}")
    if len(parts) > 1 and parts[-1].lstrip("*") != package.name:
        raise ReleaseAssetError(f"checksum sidecar names a different file: {sidecar.name}")


def prepare_release_assets(
    assets_dir: Path,
    version: str,
    candidate_sha: str,
    tag_sha: str,
    accepted_sha256: dict[str, str],
) -> list[Path]:
    """Validate required assets and write SHA256SUMS plus source provenance."""
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ReleaseAssetError("version must use three numeric components")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", candidate_sha):
        raise ReleaseAssetError("candidate SHA must be a full 40-character commit ID")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", tag_sha):
        raise ReleaseAssetError("tag commit SHA must be a full 40-character commit ID")
    if candidate_sha.lower() != tag_sha.lower():
        raise ReleaseAssetError("candidate SHA does not match the tag commit")
    if not assets_dir.is_dir():
        raise ReleaseAssetError(f"asset directory does not exist: {assets_dir}")

    required_names = {
        f"OpenVPN-{version}-windows-x64.exe",
        f"OpenVPN-{version}-macos-arm64.pkg",
    }
    optional_names = {f"OpenVPN-{version}-macos-x86_64.pkg"}
    allowed_names = required_names | optional_names

    allowed_asset_names = (
        allowed_names
        | {f"{name}.sha256" for name in allowed_names}
        | {"SHA256SUMS", "SOURCE-COMMIT.txt"}
    )
    unexpected_assets = sorted(
        path.name
        for path in assets_dir.iterdir()
        if path.is_file() and path.name not in allowed_asset_names
    )
    if unexpected_assets:
        raise ReleaseAssetError(
            "unexpected release asset(s): " + ", ".join(unexpected_assets)
        )

    package_files = sorted(
        path
        for path in assets_dir.iterdir()
        if path.is_file() and path.suffix.lower() in {".exe", ".pkg"}
        and path.name.startswith("OpenVPN-")
    )
    unexpected = [path.name for path in package_files if path.name not in allowed_names]
    if unexpected:
        raise ReleaseAssetError(
            "unexpected or wrong-version package asset(s): " + ", ".join(unexpected)
        )

    missing = sorted(name for name in required_names if not (assets_dir / name).is_file())
    if missing:
        raise ReleaseAssetError("missing required package asset(s): " + ", ".join(missing))

    included_names = required_names | (optional_names & {path.name for path in package_files})
    packages = sorted(
        (assets_dir / name for name in included_names), key=lambda path: path.name
    )
    package_names = {package.name for package in packages}
    orphaned_sidecars = sorted(
        name
        for name in optional_names | required_names
        if (assets_dir / f"{name}.sha256").is_file() and name not in package_names
    )
    if orphaned_sidecars:
        raise ReleaseAssetError(
            "checksum sidecar has no package: " + ", ".join(orphaned_sidecars)
        )
    if set(accepted_sha256) != package_names:
        missing_hashes = sorted(package_names - set(accepted_sha256))
        unknown_hashes = sorted(set(accepted_sha256) - package_names)
        details: list[str] = []
        if missing_hashes:
            details.append("missing accepted SHA-256 for " + ", ".join(missing_hashes))
        if unknown_hashes:
            details.append(
                "accepted SHA-256 supplied for absent asset "
                + ", ".join(unknown_hashes)
            )
        raise ReleaseAssetError("; ".join(details))

    digests: list[tuple[Path, str]] = []
    for package in packages:
        if package.stat().st_size == 0:
            raise ReleaseAssetError(f"package asset is empty: {package.name}")
        digest = _sha256(package)
        expected_digest = accepted_sha256[package.name]
        if not re.fullmatch(r"[0-9a-fA-F]{64}", expected_digest):
            raise ReleaseAssetError(f"accepted SHA-256 is invalid for {package.name}")
        if digest.lower() != expected_digest.lower():
            raise ReleaseAssetError(
                f"package does not match accepted SHA-256: {package.name}"
            )
        _verify_sidecar(package, digest)
        digests.append((package, digest))

    normalized_candidate = candidate_sha.lower()
    checksum_contents = "".join(
        f"{digest}  {package.name}\n" for package, digest in digests
    )
    source_contents = (
        f"Version: {version}\nCandidate commit: {normalized_candidate}\n"
    )

    (assets_dir / "SHA256SUMS").write_text(checksum_contents, encoding="utf-8")
    (assets_dir / "SOURCE-COMMIT.txt").write_text(source_contents, encoding="utf-8")
    return packages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-dir", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--candidate-sha", required=True)
    parser.add_argument("--tag-sha", required=True)
    parser.add_argument("--windows-sha256", required=True)
    parser.add_argument("--macos-arm64-sha256", required=True)
    parser.add_argument("--macos-x86-64-sha256")
    args = parser.parse_args()

    try:
        packages = prepare_release_assets(
            args.assets_dir,
            args.version,
            args.candidate_sha,
            args.tag_sha,
            {
                f"OpenVPN-{args.version}-windows-x64.exe": args.windows_sha256,
                f"OpenVPN-{args.version}-macos-arm64.pkg": args.macos_arm64_sha256,
                **(
                    {f"OpenVPN-{args.version}-macos-x86_64.pkg": args.macos_x86_64_sha256}
                    if args.macos_x86_64_sha256
                    else {}
                ),
            },
        )
    except (OSError, ReleaseAssetError) as error:
        print(f"Release asset validation failed: {error}", file=sys.stderr)
        return 1

    print("Validated package assets:")
    for package in packages:
        print(f"  {package.name}")
    print("Wrote SHA256SUMS and SOURCE-COMMIT.txt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
