import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "packaging/release"))

from submit_release_asset import ReleaseSubmissionError, submit_release_asset


COMMIT = "26bf5e2944db3c21f0a9ce7e1cf5723a60644c29"


class FakeGitHub:
    def __init__(self) -> None:
        self.releases = {}
        self.calls = []
        self.remote_commit = COMMIT
        self.create_race = False

    def __call__(self, command, **kwargs):
        self.calls.append(command)
        if command[:2] == ["gh", "api"]:
            return subprocess.CompletedProcess(
                command,
                0,
                json.dumps({"object": {"sha": self.remote_commit, "type": "commit"}}),
                "",
            )

        if command[:3] == ["gh", "release", "view"]:
            tag = command[3]
            release = self.releases.get(tag)
            if release is None:
                raise subprocess.CalledProcessError(1, command, stderr="not found")
            return subprocess.CompletedProcess(command, 0, json.dumps(release), "")

        if command[:3] == ["gh", "release", "create"]:
            tag = command[3]
            self.releases[tag] = {
                "tagName": tag,
                "isDraft": True,
                "isPrerelease": False,
                "url": f"https://github.com/flyu518/open-vpn/releases/tag/{tag}",
                "assets": [],
            }
            if self.create_race:
                self.create_race = False
                raise subprocess.CalledProcessError(1, command, stderr="already exists")
            return subprocess.CompletedProcess(command, 0, "", "")

        if command[:3] == ["gh", "release", "upload"]:
            tag = command[3]
            asset_spec = command[4]
            local_path, asset_name = asset_spec.rsplit("#", 1)
            content = Path(local_path).read_bytes()
            digest = hashlib.sha256(content).hexdigest()
            self.releases[tag]["assets"].append(
                {"name": asset_name, "digest": f"sha256:{digest}"}
            )
            return subprocess.CompletedProcess(command, 0, "", "")

        raise AssertionError(f"unexpected command: {command}")


class SubmitReleaseAssetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.version = json.loads((ROOT / "project-config.json").read_text(encoding="utf-8"))["version"]
        self.notes = self.directory / f"{self.version}-release-notes.md"
        self.notes.write_text(f"# OpenVPN {self.version}\n", encoding="utf-8")
        self.mac_package = self.directory / "mac.pkg"
        self.mac_package.write_bytes(b"accepted macOS installer")
        self.windows_package = self.directory / "windows.exe"
        self.windows_package.write_bytes(b"accepted Windows installer")
        self.github = FakeGitHub()

    @staticmethod
    def digest(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def submit(self, platform, path, github=None, candidate=COMMIT):
        return submit_release_asset(
            version=self.version,
            candidate_sha=candidate,
            platform=platform,
            package_path=path,
            accepted_sha256=self.digest(path),
            repo="flyu518/open-vpn",
            notes_file=self.notes,
            runner=github or self.github,
        )

    def test_first_platform_creates_draft_and_submits_only_its_asset(self):
        result = self.submit("macos-arm64", self.mac_package)

        self.assertEqual(result["asset_name"], f"OpenVPN-{self.version}-macos-arm64.pkg")
        self.assertTrue(self.github.releases[f"v{self.version}"]["isDraft"])
        self.assertEqual(
            [asset["name"] for asset in self.github.releases[f"v{self.version}"]["assets"]],
            [f"OpenVPN-{self.version}-macos-arm64.pkg"],
        )
        create_calls = [call for call in self.github.calls if "create" in call]
        self.assertEqual(len(create_calls), 1)

    def test_second_platform_reuses_draft_without_waiting_for_first(self):
        self.submit("macos-arm64", self.mac_package)
        self.submit("windows-x64", self.windows_package)

        release = self.github.releases[f"v{self.version}"]
        self.assertEqual(
            {asset["name"] for asset in release["assets"]},
            {
                f"OpenVPN-{self.version}-macos-arm64.pkg",
                f"OpenVPN-{self.version}-windows-x64.exe",
            },
        )
        self.assertEqual(len([call for call in self.github.calls if "create" in call]), 1)

    def test_concurrent_draft_creation_race_reuses_the_created_draft(self):
        self.github.create_race = True

        result = self.submit("macos-arm64", self.mac_package)

        self.assertEqual(result["status"], "uploaded")
        self.assertEqual(len([call for call in self.github.calls if "create" in call]), 1)

    def test_repeating_same_submission_is_idempotent(self):
        self.submit("macos-arm64", self.mac_package)
        self.submit("macos-arm64", self.mac_package)

        uploads = [call for call in self.github.calls if "upload" in call]
        self.assertEqual(len(uploads), 1)
        self.assertEqual(len(self.github.releases[f"v{self.version}"]["assets"]), 1)

    def test_rejects_package_when_acceptance_hash_does_not_match(self):
        with self.assertRaises(ReleaseSubmissionError):
            submit_release_asset(
                version=self.version,
                candidate_sha=COMMIT,
                platform="windows-x64",
                package_path=self.windows_package,
                accepted_sha256="0" * 64,
                repo="flyu518/open-vpn",
                notes_file=self.notes,
                runner=self.github,
            )

        self.assertEqual(self.github.calls, [])

    def test_rejects_tag_that_does_not_point_to_candidate(self):
        self.github.remote_commit = "0" * 40

        with self.assertRaises(ReleaseSubmissionError):
            self.submit("windows-x64", self.windows_package)

        self.assertFalse(self.github.releases)

    def test_refuses_to_replace_existing_different_asset(self):
        self.submit("windows-x64", self.windows_package)
        release = self.github.releases[f"v{self.version}"]
        release["assets"][0]["digest"] = "sha256:" + "0" * 64

        with self.assertRaises(ReleaseSubmissionError):
            self.submit("windows-x64", self.windows_package)

        self.assertEqual(len([call for call in self.github.calls if "upload" in call]), 1)

    def test_refuses_to_add_assets_to_published_release(self):
        self.submit("macos-arm64", self.mac_package)
        self.github.releases[f"v{self.version}"]["isDraft"] = False

        with self.assertRaises(ReleaseSubmissionError):
            self.submit("windows-x64", self.windows_package)

        self.assertEqual(len([call for call in self.github.calls if "upload" in call]), 1)


if __name__ == "__main__":
    unittest.main()
