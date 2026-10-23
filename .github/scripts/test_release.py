"""Check tag validation, installed-package verification, and GitHub publishing."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from release import distribution_target, package, publish, validate_version


class VersionValidationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name)
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.19)\n"
            "project(FooyinVimMotions VERSION 0.1.0 LANGUAGES CXX)\n"
        )

    def test_stable_short_prerelease_and_build_metadata(self):
        for tag in ("v0.1", "v0.1.0", "v0.1-rc.1", "v0.1.0-rc.1", "v0.1.0-0",
                    "v0.1.0-alpha-beta", "v0.1+build.01", "v0.1.0-rc.1+build.123"):
            with self.subTest(tag=tag):
                self.assertEqual(validate_version(self.source, f"refs/tags/{tag}"), (tag, "0.1.0"))

    def test_rejects_ambiguous_or_malformed_versions(self):
        for tag in ("0.1.0", "v0", "v00.1", "v0.01", "v0.1.", "v00.1.0", "v0.01.0",
                    "v0.1.00", "v0.1-01", "v0.1.0-01", "v0.1.0-rc.01", "v0.1.0-",
                    "v0.1.0+", "v0.1.0+build..1", "v0.1.0/extra", "v0.1.0\n", "v0.1;touch-file"):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, "Invalid version tag"):
                validate_version(self.source, f"refs/tags/{tag}")

    def test_manual_dispatch_requires_a_tag(self):
        for ref in ("refs/heads/master", "refs/heads/v0.1.0", "v0.1.0"):
            with self.subTest(ref=ref), self.assertRaisesRegex(ValueError, "existing version tag"):
                validate_version(self.source, ref)

    def test_version_mismatch(self):
        with self.assertRaisesRegex(ValueError, "CMakeLists.txt declares 0.1.0"):
            validate_version(self.source, "refs/tags/v0.2.0-rc.1")

    def test_short_version_does_not_match_nonzero_patch(self):
        (self.source / "CMakeLists.txt").write_text("project(FooyinVimMotions VERSION 0.1.1)\n")
        with self.assertRaisesRegex(ValueError, "CMakeLists.txt declares 0.1.1"):
            validate_version(self.source, "refs/tags/v0.1")

    def test_cmake_comments_and_multiline_project(self):
        (self.source / "CMakeLists.txt").write_text(
            "# project(FooyinVimMotions VERSION 9.9.9)\n"
            "project(\n  FooyinVimMotions\n  VERSION 0.1.0\n  LANGUAGES CXX\n)\n"
        )
        self.assertEqual(validate_version(self.source, "refs/tags/v0.1.0"), ("v0.1.0", "0.1.0"))

    def test_missing_project_version(self):
        (self.source / "CMakeLists.txt").write_text("project(FooyinVimMotions LANGUAGES CXX)\n")
        with self.assertRaisesRegex(ValueError, "Cannot read"):
            validate_version(self.source, "refs/tags/v0.1.0")


class ReleasePackagingTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vim release ")
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.source, self.build = root / "source", root / "build"
        self.fooyin_source, self.fooyin_build = root / "fooyin", root / "build-fooyin"
        for directory in (self.source, self.build, self.fooyin_source, self.fooyin_build):
            directory.mkdir()
        (self.source / "CMakeLists.txt").write_text("project(FooyinVimMotions VERSION 0.1.0)\n")
        (self.source / "LICENSE").write_text("test license\n")
        self.write_cache()
        (self.fooyin_build / "CMakeCache.txt").write_text("CMAKE_PROJECT_VERSION:STATIC=0.14.0\n")
        self.args = argparse.Namespace(
            source=self.source, build=self.build, fooyin_source=self.fooyin_source,
            fooyin_build=self.fooyin_build, output=root / "dist", tag="v0.1.0",
        )
        self.dependencies = "libfooyin_gui.so.0.0.0 => /host/libfooyin_gui.so.0.0.0\n"
        self.dynamic = "0x000000000000001d (RUNPATH) Library runpath: [$ORIGIN/..]\n"
        self.install_plugin = True
        self.loader_error = False
        self.loader_paths = []
        self.os_release = self.enterContext(patch("release.platform.freedesktop_os_release", return_value={
            "ID": "ubuntu", "VERSION_ID": "24.04", "PRETTY_NAME": "Ubuntu 24.04 LTS",
        }))
        self.enterContext(patch("release.platform.machine", return_value="x86_64"))
        self.enterContext(patch("release.run", side_effect=self.command_output))
        self.process = self.enterContext(patch("release.subprocess.run", side_effect=self.command))
        self.outputs = self.enterContext(patch("release.outputs"))

    def write_cache(self, version="0.1.0", build_type="Release"):
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_PROJECT_VERSION:STATIC={version}\nCMAKE_BUILD_TYPE:STRING={build_type}\n"
            "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/g++-13\n"
        )

    def command_output(self, *args, env=None):
        if args[0] == "git":
            return "4cb697bd19cfde409afca420b84d15bccc1ffb1e"
        if args[0] == "ldd":
            assert env is not None
            self.assertEqual(env["LD_LIBRARY_PATH"], str(self.fooyin_build / "run/lib/fooyin"))
            return self.dependencies
        if args[0] == "readelf":
            return self.dynamic
        if args[0] in ("pkg-config", "/usr/bin/g++-13", "cmake"):
            return "test-version"
        self.fail(f"Unexpected command: {args}")

    def command(self, args, **kwargs):
        if args[:2] == ["cmake", "--install"]:
            if self.install_plugin:
                plugin = Path(args[-1]) / "lib/fooyin/plugins/fyplugin_vimmotions.so"
                plugin.parent.mkdir(parents=True)
                plugin.write_bytes(b"test plugin")
        else:
            self.assertEqual(args, [str(self.build / "tests/test_pluginload")])
            self.assertTrue(kwargs["check"])
            runtime = kwargs["env"]
            self.assertEqual(runtime["QT_QPA_PLATFORM"], "offscreen")
            plugin = Path(runtime["VIM_MOTIONS_PLUGIN_PATH"])
            self.assertIn("extracted", plugin.parts)
            self.assertEqual(plugin.read_bytes(), b"test plugin")
            self.loader_paths.append(plugin)
            if self.loader_error:
                raise subprocess.CalledProcessError(1, args)
        return subprocess.CompletedProcess(args, 0)

    def test_archive_round_trip_checks_installed_plugin_and_checksum(self):
        package(self.args)
        metadata = json.loads((self.args.output / "build-metadata.json").read_text())
        artifact = metadata["artifact"]
        self.assertEqual(metadata["fooyin_version"], "0.14.0")
        self.assertEqual(metadata["plugin_version"], "0.1.0")
        self.assertEqual(metadata["distribution"], "ubuntu24.04")
        self.assertTrue(metadata["built_at"])
        archive = self.args.output / f"{artifact}.zip"
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        self.assertEqual(Path(str(archive) + ".sha256").read_text(), f"{checksum}  {archive.name}\n")
        with zipfile.ZipFile(archive) as zipped:
            self.assertIsNone(zipped.testzip())
            self.assertEqual(set(zipped.namelist()), {
                "linux-x64/fyplugin_vimmotions.so", "LICENSE", "INSTALL.txt",
                "build-metadata.json", "dependencies.txt", "elf-dynamic.txt",
            })
            self.assertEqual(zipped.read("linux-x64/fyplugin_vimmotions.so"), b"test plugin")
            self.assertEqual(json.loads(zipped.read("build-metadata.json")), metadata)
            notes = zipped.read("INSTALL.txt").decode()
            self.assertIn("Settings > Plugins > Install", notes)
            self.assertIn(f"{artifact}.zip.sha256", notes)
            self.assertIn("Built for Ubuntu 24.04 LTS x86-64", notes)
        self.assertEqual(len(self.loader_paths), 1)
        self.assertFalse(self.loader_paths[0].exists())
        self.outputs.assert_called_once_with(artifact=artifact)

    def test_packages_ubuntu_26_and_rolling_arch_without_version_id(self):
        for os_release, target in (
            ({"ID": "ubuntu", "VERSION_ID": "26.04", "PRETTY_NAME": "Ubuntu 26.04 LTS"}, "ubuntu26.04"),
            ({"ID": "arch", "PRETTY_NAME": "Arch Linux"}, "archlinux"),
        ):
            with self.subTest(target=target):
                self.os_release.return_value = os_release
                package(self.args)
                metadata = json.loads((self.args.output / "build-metadata.json").read_text())
                self.assertEqual(metadata["distribution"], target)
                self.assertIn(f"-linux-x86_64-{target}-fooyin-", metadata["artifact"])
                with zipfile.ZipFile(self.args.output / f"{metadata['artifact']}.zip") as zipped:
                    self.assertIn(f"Built for {os_release['PRETTY_NAME']} x86-64",
                                  zipped.read("INSTALL.txt").decode())

    def test_rejects_unsupported_distribution_or_architecture(self):
        for os_release, architecture in (
            ({"ID": "ubuntu", "VERSION_ID": "22.04"}, "x86_64"),
            ({"ID": "ubuntu"}, "x86_64"),
            ({"ID": "debian", "VERSION_ID": "13"}, "x86_64"),
            ({"ID": "arch"}, "aarch64"),
        ):
            with self.subTest(os_release=os_release, architecture=architecture):
                with self.assertRaisesRegex(ValueError, "Package targets require"):
                    distribution_target(os_release, architecture)

    def test_rejects_stale_or_non_release_builds_before_installing(self):
        for version, build_type in (("0.2.0", "Release"), ("0.1.0", "Debug")):
            with self.subTest(version=version, build_type=build_type):
                self.write_cache(version, build_type)
                with self.assertRaises(ValueError):
                    package(self.args)
        self.process.assert_not_called()

    def test_rejects_missing_plugin(self):
        self.install_plugin = False
        with self.assertRaisesRegex(ValueError, "Installed plugin is missing or empty"):
            package(self.args)
        self.outputs.assert_not_called()

    def test_rejects_missing_dependencies(self):
        self.dependencies = "libfooyin_core.so.0.0.0 => not found"
        with self.assertRaisesRegex(ValueError, "Unresolved plugin dependencies"):
            package(self.args)
        self.outputs.assert_not_called()

    def test_rejects_workspace_rpath(self):
        self.dynamic = f"(RUNPATH) Library runpath: [{self.fooyin_build}/run/lib/fooyin]"
        with self.assertRaisesRegex(ValueError, "leaks a workspace library path"):
            package(self.args)
        self.outputs.assert_not_called()

    def test_failed_load_does_not_mark_archive_verified(self):
        self.loader_error = True
        with self.assertRaises(subprocess.CalledProcessError):
            package(self.args)
        self.outputs.assert_not_called()
        self.assertFalse((self.args.output / "build-metadata.json").exists())
        self.assertEqual(list(self.args.output.glob("*.sha256")), [])


class ReleasePublishingTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        (self.directory / "CMakeLists.txt").write_text("project(FooyinVimMotions VERSION 0.1.0)\n")
        self.archive = self.directory / "vim-test.zip"
        self.archive.write_bytes(b"test archive")
        Path(str(self.archive) + ".sha256").write_text("test checksum")
        self.args = argparse.Namespace(
            source=self.directory, output=self.directory, repo="example/vim", tag="v0.1.0"
        )
        self.write_metadata()
        self.gh = self.enterContext(patch("release.subprocess.run"))
        self.gh.return_value = subprocess.CompletedProcess([], 1)
        self.enterContext(patch("release.run", return_value="https://example.com/release"))
        self.outputs = self.enterContext(patch("release.outputs"))

    def write_metadata(self):
        (self.directory / "build-metadata.json").write_text(
            json.dumps({"tag": self.args.tag, "artifact": "vim-test"})
        )

    def test_creates_release_with_both_assets_and_existing_tag(self):
        publish(self.args)
        command = self.gh.call_args.args[0]
        self.assertEqual(command[:4], ["gh", "release", "create", "v0.1.0"])
        self.assertIn(str(self.archive), command)
        self.assertIn(str(self.archive) + ".sha256", command)
        self.assertIn("--verify-tag", command)
        self.assertIn("--generate-notes", command)
        self.assertEqual(command[command.index("--title") + 1], "Vim Motions v0.1.0")
        self.assertEqual(command[command.index("--repo") + 1], "example/vim")
        self.assertNotIn("--prerelease", command)
        self.outputs.assert_called_once_with(url="https://example.com/release")

    def test_classifies_prerelease_without_confusing_build_metadata(self):
        for tag, prerelease in (("v0.1", False), ("v0.1-rc.1+build.2", True),
                                ("v0.1.0-rc.1+build.2", True), ("v0.1.0+build-with-hyphens", False)):
            with self.subTest(tag=tag):
                self.args.tag = tag
                self.write_metadata()
                publish(self.args)
                command = self.gh.call_args.args[0]
                self.assertEqual(command[3], tag)
                self.assertEqual("--prerelease" in command, prerelease)

    def test_rerun_updates_existing_release_assets(self):
        self.gh.return_value = subprocess.CompletedProcess([], 0)
        publish(self.args)
        command = self.gh.call_args.args[0]
        self.assertEqual(command[:4], ["gh", "release", "upload", "v0.1.0"])
        self.assertIn("--clobber", command)

    def test_publishes_all_distribution_assets_in_one_release(self):
        self.args.output = self.directory / "dist"
        expected = []
        for target in ("ubuntu24.04", "ubuntu26.04", "archlinux"):
            directory = self.args.output / f"release-linux-{target}"
            directory.mkdir(parents=True)
            artifact = f"vim-{target}"
            (directory / "build-metadata.json").write_text(
                json.dumps({"tag": self.args.tag, "artifact": artifact})
            )
            for suffix in (".zip", ".zip.sha256"):
                asset = directory / f"{artifact}{suffix}"
                asset.write_bytes(b"test asset")
                expected.append(str(asset))
        publish(self.args)
        self.assertEqual(self.gh.call_count, 2)
        command = self.gh.call_args.args[0]
        self.assertEqual(command[:4], ["gh", "release", "create", "v0.1.0"])
        for asset in expected:
            self.assertEqual(command.count(asset), 1)

    def test_checks_every_build_before_publishing(self):
        directory = self.directory / "second-build"
        directory.mkdir()
        manifest = directory / "build-metadata.json"
        manifest.write_text(json.dumps({"tag": "v0.1.0", "artifact": "missing"}))
        with self.assertRaisesRegex(ValueError, "missing or empty"):
            publish(self.args)
        manifest.write_text(json.dumps({"tag": "v0.2.0", "artifact": "missing"}))
        with self.assertRaisesRegex(ValueError, "does not match"):
            publish(self.args)
        manifest.write_text(json.dumps({"tag": "v0.1.0", "artifact": "vim-test"}))
        (directory / "vim-test.zip").write_bytes(b"duplicate")
        with self.assertRaisesRegex(ValueError, "Duplicate release asset"):
            publish(self.args)
        self.gh.assert_not_called()

    def test_rejects_empty_output_before_publishing(self):
        self.args.output = self.directory / "empty"
        with self.assertRaisesRegex(ValueError, "No packaged builds"):
            publish(self.args)
        self.gh.assert_not_called()

    def test_missing_assets_or_wrong_tag_fail_before_publishing(self):
        self.args.tag = "v0.1.0-rc.1"
        with self.assertRaisesRegex(ValueError, "does not match"):
            publish(self.args)
        self.args.tag = "v0.1.0"
        self.archive.unlink()
        with self.assertRaisesRegex(ValueError, "missing or empty"):
            publish(self.args)
        self.gh.assert_not_called()

    def test_failed_publish_does_not_report_a_release_url(self):
        self.gh.side_effect = [
            subprocess.CompletedProcess([], 1),
            subprocess.CalledProcessError(1, ["gh", "release", "create"]),
        ]
        with self.assertRaises(subprocess.CalledProcessError):
            publish(self.args)
        self.outputs.assert_not_called()


if __name__ == "__main__":
    unittest.main()
