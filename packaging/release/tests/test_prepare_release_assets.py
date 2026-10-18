import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from prepare_release_assets import ReleaseAssetError, prepare_release_assets


class PrepareReleaseAssetsTests(unittest.TestCase):
    candidate = "26bf5e2944db3c21f0a9ce7e1cf5723a60644c29"
    version = "1.0.0"

    def create_file(self, directory: Path, name: str, content: bytes) -> Path:
        path = directory / name
        path.write_bytes(content)
        return path

    def create_required_assets(self, directory: Path) -> tuple[Path, Path]:
        windows = self.create_file(
            directory, "OpenVPN-1.0.0-windows-x64.exe", b"windows installer"
        )
        macos = self.create_file(
            directory, "OpenVPN-1.0.0-macos-arm64.pkg", b"mac installer"
        )
        return windows, macos

    @staticmethod
    def accepted_hashes(*packages: Path) -> dict[str, str]:
        return {
            package.name: hashlib.sha256(package.read_bytes()).hexdigest()
            for package in packages
        }

    def test_refuses_to_finalize_when_windows_asset_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary)
            self.create_file(assets, "OpenVPN-1.0.0-macos-arm64.pkg", b"mac installer")

            with self.assertRaisesRegex(ReleaseAssetError, "windows-x64"):
                prepare_release_assets(
                    assets, self.version, self.candidate, self.candidate,
                    self.accepted_hashes(),
                )

            self.assertFalse((assets / "SHA256SUMS").exists())

    def test_refuses_to_finalize_when_candidate_does_not_match_tag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary)
            windows, macos = self.create_required_assets(assets)

            with self.assertRaisesRegex(ReleaseAssetError, "tag commit"):
                prepare_release_assets(
                    assets, self.version, self.candidate, "0" * 40,
                    self.accepted_hashes(windows, macos),
                )

            self.assertFalse((assets / "SHA256SUMS").exists())

    def test_writes_checksums_for_required_and_optional_packages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary)
            windows, macos_arm = self.create_required_assets(assets)
            macos_intel = self.create_file(
                assets, "OpenVPN-1.0.0-macos-x86_64.pkg", b"intel package"
            )

            prepare_release_assets(
                assets, self.version, self.candidate, self.candidate,
                self.accepted_hashes(windows, macos_arm, macos_intel),
            )

            expected_files = [windows, macos_arm, macos_intel]
            expected_lines = [
                f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}"
                for path in sorted(expected_files, key=lambda item: item.name)
            ]
            self.assertEqual(
                (assets / "SHA256SUMS").read_text(encoding="utf-8").splitlines(),
                expected_lines,
            )
            self.assertEqual(
                (assets / "SOURCE-COMMIT.txt").read_text(encoding="utf-8"),
                f"Version: {self.version}\nCandidate commit: {self.candidate}\n",
            )

    def test_rejects_an_empty_required_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary)
            windows, macos = self.create_required_assets(assets)
            windows.write_bytes(b"")

            with self.assertRaisesRegex(ReleaseAssetError, "empty"):
                prepare_release_assets(
                    assets, self.version, self.candidate, self.candidate,
                    self.accepted_hashes(windows, macos),
                )

            self.assertFalse((assets / "SHA256SUMS").exists())

    def test_refuses_package_that_differs_from_accepted_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary)
            windows, macos = self.create_required_assets(assets)

            with self.assertRaisesRegex(ReleaseAssetError, "accepted SHA-256"):
                prepare_release_assets(
                    assets,
                    self.version,
                    self.candidate,
                    self.candidate,
                    {**self.accepted_hashes(windows, macos), windows.name: "0" * 64},
                )

            self.assertFalse((assets / "SHA256SUMS").exists())

    def test_optional_intel_package_must_also_be_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary)
            windows, macos_arm = self.create_required_assets(assets)
            self.create_file(assets, "OpenVPN-1.0.0-macos-x86_64.pkg", b"intel package")

            with self.assertRaisesRegex(ReleaseAssetError, "macos-x86_64"):
                prepare_release_assets(
                    assets,
                    self.version,
                    self.candidate,
                    self.candidate,
                    self.accepted_hashes(windows, macos_arm),
                )

    def test_rejects_unexpected_archive_asset(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assets = Path(temporary)
            windows, macos = self.create_required_assets(assets)
            self.create_file(assets, "OpenVPN-1.0.0-windows-x64.zip", b"not installer")

            with self.assertRaisesRegex(ReleaseAssetError, "unexpected release asset"):
                prepare_release_assets(
                    assets,
                    self.version,
                    self.candidate,
                    self.candidate,
                    self.accepted_hashes(windows, macos),
                )


if __name__ == "__main__":
    unittest.main()
