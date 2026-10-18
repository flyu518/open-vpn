#!/usr/bin/env python3
"""Submit one already-accepted platform installer to the shared GitHub Release."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Callable, Sequence


ROOT = Path(__file__).resolve().parents[2]
PLATFORMS = {
    "windows-x64": ".exe",
    "macos-arm64": ".pkg",
    "macos-x86_64": ".pkg",
}
Runner = Callable[..., subprocess.CompletedProcess[str]]


class ReleaseSubmissionError(ValueError):
    """Raised when an accepted installer cannot be safely submitted."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _run(runner: Runner, command: Sequence[str]) -> str:
    result = runner(
        list(command), check=True, capture_output=True, text=True
    )
    return result.stdout.strip()


def _verify_remote_tag(
    runner: Runner, tag: str, candidate_sha: str, repo: str
) -> None:
    endpoint = f"repos/{repo}/git/ref/tags/{tag}"
    try:
        reference = json.loads(_run(runner, ["gh", "api", endpoint]))
    except subprocess.CalledProcessError as error:
        raise ReleaseSubmissionError(f"could not read remote tag {tag}") from error
    except json.JSONDecodeError as error:
        raise ReleaseSubmissionError("GitHub CLI returned invalid tag data") from error

    target = reference.get("object", {})
    for _ in range(5):
        if target.get("type") == "commit":
            tag_commit = str(target.get("sha", "")).lower()
            break
        if target.get("type") != "tag" or not target.get("sha"):
            raise ReleaseSubmissionError(f"remote tag {tag} does not point to a commit")
        try:
            tag_object = json.loads(
                _run(runner, ["gh", "api", f"repos/{repo}/git/tags/{target['sha']}"])
            )
        except subprocess.CalledProcessError as error:
            raise ReleaseSubmissionError(f"could not resolve remote tag {tag}") from error
        except json.JSONDecodeError as error:
            raise ReleaseSubmissionError("GitHub CLI returned invalid annotated-tag data") from error
        target = tag_object.get("object", {})
    else:
        raise ReleaseSubmissionError(f"remote tag {tag} has too many nested tag objects")

    if tag_commit != candidate_sha.lower():
        raise ReleaseSubmissionError(
            f"remote tag {tag} points to {tag_commit}, not {candidate_sha}"
        )


def _view_release(
    runner: Runner, tag: str, repo: str
) -> dict | None:
    command = [
        "gh", "release", "view", tag,
        "--json", "tagName,isDraft,isPrerelease,url,assets",
        "--repo", repo,
    ]
    try:
        output = _run(runner, command)
    except subprocess.CalledProcessError:
        return None
    try:
        return json.loads(output)
    except json.JSONDecodeError as error:
        raise ReleaseSubmissionError("GitHub CLI returned invalid Release data") from error


def submit_release_asset(
    *,
    version: str,
    candidate_sha: str,
    platform: str,
    package_path: Path,
    accepted_sha256: str,
    repo: str,
    notes_file: Path,
    runner: Runner = subprocess.run,
) -> dict[str, str]:
    """Create/reuse a draft and upload one platform package without overwriting."""
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ReleaseSubmissionError("version must use three numeric components")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", candidate_sha):
        raise ReleaseSubmissionError("candidate SHA must be a full 40-character commit ID")
    if platform not in PLATFORMS:
        raise ReleaseSubmissionError(f"unsupported platform: {platform}")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", accepted_sha256):
        raise ReleaseSubmissionError("accepted SHA-256 must contain 64 hexadecimal characters")
    if not package_path.is_file():
        raise ReleaseSubmissionError(f"installer does not exist: {package_path}")
    if package_path.suffix.lower() != PLATFORMS[platform]:
        raise ReleaseSubmissionError(
            f"{platform} requires a {PLATFORMS[platform]} installer"
        )
    if not notes_file.is_file():
        raise ReleaseSubmissionError(f"release notes do not exist: {notes_file}")

    package_digest = _sha256(package_path)
    if package_digest.lower() != accepted_sha256.lower():
        raise ReleaseSubmissionError(
            f"installer SHA-256 {package_digest} does not match the accepted SHA-256"
        )

    config_path = ROOT / "project-config.json"
    try:
        configured_version = json.loads(config_path.read_text(encoding="utf-8"))["version"]
    except (OSError, KeyError, json.JSONDecodeError) as error:
        raise ReleaseSubmissionError(f"cannot read shared version from {config_path}") from error
    if version != configured_version:
        raise ReleaseSubmissionError(
            f"version {version} does not match project-config.json ({configured_version})"
        )

    tag = f"v{version}"
    _verify_remote_tag(runner, tag, candidate_sha, repo)

    release = _view_release(runner, tag, repo)
    if release is None:
        create = [
            "gh", "release", "create", tag,
            "--draft",
            "--title", f"OpenVPN {version}",
            "--notes-file", str(notes_file.resolve()),
            "--repo", repo,
        ]
        try:
            _run(runner, create)
        except subprocess.CalledProcessError as create_error:
            # Another platform may have created the shared draft at the same time.
            release = _view_release(runner, tag, repo)
            if release is None:
                raise ReleaseSubmissionError(
                    f"could not create or find Release {tag}"
                ) from create_error
        else:
            release = _view_release(runner, tag, repo)

    if release is None:
        raise ReleaseSubmissionError(f"could not find Release {tag} after creation")
    if release.get("tagName") != tag:
        raise ReleaseSubmissionError("the existing Release uses a different tag")
    if release.get("isDraft") is not True:
        raise ReleaseSubmissionError(f"Release {tag} is already published")
    if release.get("isPrerelease") is True:
        raise ReleaseSubmissionError(f"Release {tag} is not a standard Release")

    suffix = PLATFORMS[platform]
    asset_name = f"OpenVPN-{version}-{platform}{suffix}"
    assets = release.get("assets", [])
    existing = next((asset for asset in assets if asset.get("name") == asset_name), None)
    expected_digest = f"sha256:{package_digest.lower()}"
    if existing is not None:
        if (existing.get("digest") or "").lower() != expected_digest:
            raise ReleaseSubmissionError(
                f"Release already contains {asset_name} with a different or unverifiable digest"
            )
        return {
            "asset_name": asset_name,
            "digest": expected_digest,
            "release_url": release.get("url", ""),
            "status": "already-present",
        }

    upload = [
        "gh", "release", "upload", tag,
        f"{package_path.resolve()}#{asset_name}",
        "--repo", repo,
    ]
    try:
        _run(runner, upload)
    except subprocess.CalledProcessError as upload_error:
        # Treat a concurrent identical upload as success, but never clobber it.
        refreshed = _view_release(runner, tag, repo)
        concurrent = next(
            (asset for asset in (refreshed or {}).get("assets", [])
             if asset.get("name") == asset_name),
            None,
        )
        if (concurrent or {}).get("digest", "").lower() != expected_digest:
            raise ReleaseSubmissionError(f"could not upload {asset_name}") from upload_error

    return {
        "asset_name": asset_name,
        "digest": expected_digest,
        "release_url": release.get("url", ""),
        "status": "uploaded",
    }


def _read_version() -> str:
    config = json.loads((ROOT / "project-config.json").read_text(encoding="utf-8"))
    return str(config["version"])


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-sha", required=True, help="full SHA of the accepted source commit")
    parser.add_argument("--platform", required=True, choices=sorted(PLATFORMS))
    parser.add_argument("--package", required=True, type=Path, help="the exact installed and accepted package")
    parser.add_argument("--sha256", required=True, help="SHA-256 recorded during package acceptance")
    parser.add_argument("--repo", default="flyu518/open-vpn", help="GitHub owner/repository")
    args = parser.parse_args(argv)

    try:
        version = _read_version()
        notes_file = ROOT / "docs" / "releases" / f"{version}-release-notes.md"
        result = submit_release_asset(
            version=version,
            candidate_sha=args.candidate_sha,
            platform=args.platform,
            package_path=args.package,
            accepted_sha256=args.sha256,
            repo=args.repo,
            notes_file=notes_file,
        )
    except (ReleaseSubmissionError, OSError, subprocess.CalledProcessError) as error:
        print(f"Release asset submission failed: {error}", file=sys.stderr)
        return 1

    print(f"{result['status']}: {result['asset_name']} ({result['digest']})")
    if result["release_url"]:
        print(result["release_url"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
