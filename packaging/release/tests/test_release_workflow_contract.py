import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class ReleaseWorkflowContractTests(unittest.TestCase):
    def test_macos_workflow_only_stages_build_artifacts(self) -> None:
        workflow = (ROOT / ".github/workflows/release-macos.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("actions/upload-artifact@v4", workflow)
        self.assertNotIn("softprops/action-gh-release@v2", workflow)
        self.assertNotIn("gh release", workflow)
        self.assertIn("refs/tags/v{0}", workflow)

    def test_shared_finalizer_checks_candidate_and_required_assets(self) -> None:
        workflow = (ROOT / ".github/workflows/publish-release.yml").read_text(
            encoding="utf-8"
        )

        for required in (
            "workflow_dispatch:",
            "candidate_sha:",
            "windows_sha256:",
            "macos_arm64_sha256:",
            "WINDOWS_SHA256:",
            "MACOS_ARM64_SHA256:",
            "acceptance_confirmed:",
            "prepare_release_assets.py",
            "gh release download",
            "--windows-sha256",
            "--macos-arm64-sha256",
            'git show "$tag:project-config.json"',
            "isDraft",
            '"draft":False',
            '"prerelease":False',
            "releases/tags/$tag",
            "--input -",
        ):
            with self.subTest(required=required):
                self.assertIn(required, workflow)

        validator = (ROOT / "packaging/release/prepare_release_assets.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("windows-x64.exe", validator)
        self.assertIn("macos-arm64.pkg", validator)

    def test_release_notes_are_concise_download_instructions(self) -> None:
        notes = (ROOT / "docs/releases/1.0.0-release-notes.md").read_text(
            encoding="utf-8"
        )
        self.assertEqual(
            notes,
            "# OpenVPN 1.0.0\n\n"
            "## 下载\n\n"
            "- Windows x64：`OpenVPN-1.0.0-windows-x64.exe`\n"
            "- macOS Apple Silicon：`OpenVPN-1.0.0-macos-arm64.pkg`\n\n"
            "`SHA256SUMS` 提供安装包的 SHA-256 校验值。"
            "`SOURCE-COMMIT.txt` 标明本版本对应的源码提交。\n",
        )


if __name__ == "__main__":
    unittest.main()
