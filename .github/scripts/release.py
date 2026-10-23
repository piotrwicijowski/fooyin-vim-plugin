#!/usr/bin/env python3
"""Validate version tags, package Linux builds, and publish them with the GitHub CLI."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import zipfile


NUMBER = r"(?:0|[1-9][0-9]*)"
PRERELEASE = r"(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)"
VERSION_TAG = re.compile(
    rf"v(?P<version>{NUMBER}\.{NUMBER}(?:\.{NUMBER})?)"
    rf"(?:-{PRERELEASE}(?:\.{PRERELEASE})*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
)


def validate_version(source, ref):
    if not ref.startswith("refs/tags/"):
        raise ValueError("Select an existing version tag, e.g. --ref v0.1.0 for gh workflow run.")
    tag = ref.removeprefix("refs/tags/")
    match = VERSION_TAG.fullmatch(tag)
    if not match:
        raise ValueError(
            f"Invalid version tag {tag!r}: expected vMAJOR.MINOR or vMAJOR.MINOR.PATCH "
            "with optional SemVer prerelease/build suffixes."
        )
    cmake = re.sub(r"#[^\n]*", "", (source / "CMakeLists.txt").read_text())
    project = re.search(
        r"\bproject\s*\(\s*FooyinVimMotions\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)(?:\s|\))",
        cmake,
        re.IGNORECASE,
    )
    if not project:
        raise ValueError("Cannot read FooyinVimMotions project VERSION from CMakeLists.txt.")
    version = match["version"]
    if version.count(".") == 1:
        version += ".0"
    if version != project[1]:
        raise ValueError(
            f"Tag {tag!r} has version {version}, but CMakeLists.txt declares {project[1]}. "
            "Update the project version before tagging."
        )
    return tag, version


def run(*args, env=None):
    return subprocess.check_output(args, text=True, env=env).strip()


def outputs(**values):
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as stream:
            for key, value in values.items():
                stream.write(f"{key}={value}\n")


def cache_value(build, name):
    match = re.search(
        rf"^{re.escape(name)}:[^=]+=(.*)$", (build / "CMakeCache.txt").read_text(), re.MULTILINE
    )
    if not match:
        raise ValueError(f"Missing {name} in {build}/CMakeCache.txt")
    return match[1]


def distribution_target(os_release, architecture):
    if architecture == "x86_64":
        if os_release["ID"] == "ubuntu" and os_release.get("VERSION_ID") in ("24.04", "26.04"):
            return f"ubuntu{os_release['VERSION_ID']}"
        if os_release["ID"] == "arch":
            return "archlinux"
    raise ValueError("Package targets require Ubuntu 24.04, Ubuntu 26.04, or Arch Linux on x86_64.")


def package(args):
    source, build = args.source.resolve(), args.build.resolve()
    fooyin_source, fooyin_build = args.fooyin_source.resolve(), args.fooyin_build.resolve()
    tag, version = validate_version(source, f"refs/tags/{args.tag}")
    if cache_value(build, "CMAKE_PROJECT_VERSION") != version:
        raise ValueError("Configured plugin version does not match the release tag; reconfigure first.")
    if cache_value(build, "CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("Release artifacts require CMAKE_BUILD_TYPE=Release.")

    fooyin_commit = run("git", "-C", str(fooyin_source), "rev-parse", "HEAD")
    os_release = platform.freedesktop_os_release()
    target = distribution_target(os_release, platform.machine())
    artifact = f"fooyin-vim-{tag}-linux-x86_64-{target}-fooyin-{fooyin_commit[:12]}"
    metadata = {
        "artifact": artifact,
        "tag": tag,
        "plugin_version": version,
        "plugin_commit": run("git", "-C", str(source), "rev-parse", "HEAD"),
        "fooyin_commit": fooyin_commit,
        "fooyin_version": cache_value(fooyin_build, "CMAKE_PROJECT_VERSION"),
        "os": os_release["PRETTY_NAME"],
        "distribution": target,
        "built_at": datetime.now(timezone.utc).isoformat(),
        "architecture": platform.machine(),
        "build_type": cache_value(build, "CMAKE_BUILD_TYPE"),
        "compiler": run(cache_value(build, "CMAKE_CXX_COMPILER"), "--version").splitlines()[0],
        "cmake": run("cmake", "--version").splitlines()[0],
        "libraries": {
            name: run("pkg-config", "--modversion", name)
            for name in ("Qt6Core", "Qt6Widgets", "libavcodec", "libavformat", "libavfilter",
                         "libavutil", "libswresample", "taglib", "icu-uc", "libarchive", "zlib")
        },
    }
    metadata_text = json.dumps(metadata, indent=2) + "\n"
    runtime = os.environ.copy()
    runtime["QT_QPA_PLATFORM"] = "offscreen"
    runtime["LD_LIBRARY_PATH"] = str(fooyin_build / "run/lib/fooyin")

    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / f"{artifact}.zip"
    with tempfile.TemporaryDirectory(prefix="vim-package-", dir=build.parent) as temporary:
        temporary = Path(temporary)
        stage = temporary / artifact
        subprocess.run(["cmake", "--install", str(build), "--prefix", str(stage)], check=True)
        plugin = stage / "lib/fooyin/plugins/fyplugin_vimmotions.so"
        if not plugin.is_file() or plugin.stat().st_size == 0:
            raise ValueError(f"Installed plugin is missing or empty: {plugin}")
        dependencies = run("ldd", str(plugin), env=runtime)
        if "not found" in dependencies:
            raise ValueError(f"Unresolved plugin dependencies:\n{dependencies}")
        dynamic = run("readelf", "--dynamic", str(plugin))
        for line in dynamic.splitlines():
            if "(RPATH)" in line or "(RUNPATH)" in line:
                if str(build.parent) in line or str(fooyin_build) in line or str(source) in line:
                    raise ValueError(f"Installed plugin leaks a workspace library path: {line}")
        # fooyin selects and strips this platform prefix during ZIP installation.
        bundle = temporary / "bundle"
        (bundle / "linux-x64").mkdir(parents=True)
        shutil.copy2(plugin, bundle / "linux-x64/fyplugin_vimmotions.so")
        shutil.copy2(source / "LICENSE", bundle / "LICENSE")
        (bundle / "build-metadata.json").write_text(metadata_text)
        (bundle / "dependencies.txt").write_text(dependencies + "\n")
        (bundle / "elf-dynamic.txt").write_text(dynamic + "\n")
        (bundle / "INSTALL.txt").write_text(
            f"Vim Motions {tag}\n\n"
            f"Built for {metadata['os']} x86-64, fooyin commit {fooyin_commit}\n"
            f"(fooyin project version {metadata['fooyin_version']}), Qt {metadata['libraries']['Qt6Core']}.\n"
            "Use a fooyin build from this revision and a compatible compiler/runtime,\n"
            "Qt, and FFmpeg installation. Exact build versions are in build-metadata.json;\n"
            "the required shared libraries are listed in dependencies.txt.\n\n"
            "Choose the archive for your distribution. Arch Linux builds target the\n"
            "fully updated rolling distribution at the recorded build time.\n\n"
            f"1. Verify: sha256sum -c {artifact}.zip.sha256\n"
            "2. In fooyin, open Settings > Plugins > Install and select the ZIP file\n"
            "   directly, without extracting it.\n"
            "3. Restart fooyin when prompted. Configure bindings in\n"
            "   Settings > Plugins > Vim Motions > Configure.\n\n"
            "If you previously copied the plugin manually, close fooyin and remove the\n"
            "old loose fyplugin_vimmotions.so before installing this bundle.\n\n"
            "For manual installation, extract the ZIP and copy linux-x64/fyplugin_vimmotions.so\n"
            "to <fooyin-prefix>/lib/fooyin/plugins/ with fooyin closed. Alternatively, set\n"
            "FOOYIN_PLUGIN_PATH to the extracted linux-x64 directory when launching fooyin.\n"
        )
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zipped:
            for path in sorted(bundle.rglob("*")):
                if path.is_file():
                    zipped.write(path, arcname=path.relative_to(bundle).as_posix())
        # Verify embedded installer metadata and load the actual ZIP payload.
        extracted = temporary / "extracted"
        with zipfile.ZipFile(archive) as zipped:
            zipped.extractall(extracted)
        runtime["VIM_MOTIONS_PLUGIN_PATH"] = str(
            extracted / "linux-x64/fyplugin_vimmotions.so"
        )
        subprocess.run([str(build / "tests/test_pluginload")], env=runtime, check=True, timeout=60)

    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + ".sha256").write_text(f"{checksum}  {archive.name}\n")
    (args.output / "build-metadata.json").write_text(metadata_text)
    outputs(artifact=artifact)
    print(f"Packaged and verified {archive}")


def publish(args):
    tag, _ = validate_version(args.source, f"refs/tags/{args.tag}")
    manifests = sorted(args.output.rglob("build-metadata.json"))
    if not manifests:
        raise ValueError("No packaged builds found in the output directory.")
    files = []
    asset_names = set()
    for manifest in manifests:
        metadata = json.loads(manifest.read_text())
        if metadata["tag"] != tag:
            raise ValueError("Packaged build does not match the release tag.")
        archive = manifest.parent / f"{metadata['artifact']}.zip"
        for filename in (archive, Path(str(archive) + ".sha256")):
            if not filename.is_file() or filename.stat().st_size == 0:
                raise ValueError(f"Release asset is missing or empty: {filename}")
            if filename.name in asset_names:
                raise ValueError(f"Duplicate release asset name: {filename.name}")
            asset_names.add(filename.name)
            files.append(str(filename))

    repo = ["--repo", args.repo]
    existing = subprocess.run(
        ["gh", "release", "view", tag, *repo], capture_output=True, text=True
    )
    if existing.returncode == 0:
        command = ["gh", "release", "upload", tag, *files, *repo, "--clobber"]
    else:
        command = [
            "gh", "release", "create", tag, *files, *repo, "--verify-tag",
            "--title", f"Vim Motions {tag}", "--generate-notes",
        ]
        if "-" in tag.split("+", 1)[0]:
            command.append("--prerelease")
    subprocess.run(command, check=True)
    url = run("gh", "release", "view", tag, *repo, "--json", "url", "--jq", ".url")
    outputs(url=url)
    print(f"Published {url}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    validate = commands.add_parser("validate")
    validate.add_argument("--source", type=Path, required=True)
    validate.add_argument("--ref", required=True)
    archive = commands.add_parser("package")
    for name in ("source", "build", "fooyin-source", "fooyin-build", "output"):
        archive.add_argument(f"--{name}", type=Path, required=True)
    archive.add_argument("--tag", required=True)
    release = commands.add_parser("publish")
    release.add_argument("--source", type=Path, required=True)
    release.add_argument("--tag", required=True)
    release.add_argument("--output", type=Path, required=True)
    release.add_argument("--repo", required=True)
    args = parser.parse_args()
    try:
        if args.command == "validate":
            tag, version = validate_version(args.source, args.ref)
            outputs(tag=tag, version=version)
            print(f"Validated {tag}: CMake project version {version}")
        elif args.command == "package":
            package(args)
        else:
            publish(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Release build failed: {error}\n")


if __name__ == "__main__":
    main()
