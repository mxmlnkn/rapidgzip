#!/usr/bin/env python3
"""Check that the rapidgzip source version matches the release tag."""

import argparse
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).absolute().parents[2]
HEADER = ROOT / "src" / "rapidgzip" / "rapidgzip.hpp"
PYPROJECT = ROOT / "python" / "rapidgzip" / "pyproject.toml"
CHANGELOG = ROOT / "python" / "rapidgzip" / "CHANGELOG.md"
TAG_PREFIX = "rapidgzip-v"


def versionFromHeader() -> str:
    text = HEADER.read_text(encoding="utf-8")
    parts = {}
    for part in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"RAPIDGZIP_VERSION_{part}\s*\{{\s*(\d+)\s*\}}", text)
        if match is None:
            raise ValueError(f"Could not find RAPIDGZIP_VERSION_{part} in {HEADER}")
        parts[part] = match.group(1)
    return ".".join(parts[part] for part in ("MAJOR", "MINOR", "PATCH"))


def versionFromPyproject() -> str:
    with PYPROJECT.open("rb") as file:
        return tomllib.load(file)["project"]["version"]


def versionFromChangelog() -> str:
    match = re.search(r"^# Version ([\d.]+)", CHANGELOG.read_text(encoding="utf-8"), re.MULTILINE)
    if match is None:
        raise ValueError(f"Could not find a version heading in {CHANGELOG}")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Check that the rapidgzip version matches the release tag.")
    parser.add_argument("tag", nargs="?", help="Release tag, e.g., rapidgzip-v0.16.1")
    parser.add_argument(
        "--check-changelog",
        action="store_true",
        help="Also check the version heading of python/rapidgzip/CHANGELOG.md.",
    )
    args = parser.parse_args()

    headerVersion = versionFromHeader()
    pyprojectVersion = versionFromPyproject()
    print(f"Header:    {headerVersion}")
    print(f"Pyproject: {pyprojectVersion}")

    if headerVersion != pyprojectVersion:
        print("The version in the C++ header and pyproject.toml do not match.", file=sys.stderr)
        return 1

    if args.tag is None:
        return 0

    if not args.tag.startswith(TAG_PREFIX):
        print(f"The tag '{args.tag}' does not start with '{TAG_PREFIX}'.", file=sys.stderr)
        return 1

    tagVersion = args.tag[len(TAG_PREFIX):]
    print(f"Tag:       {tagVersion}")

    versions = {headerVersion, tagVersion}
    if args.check_changelog:
        changelogVersion = versionFromChangelog()
        print(f"Changelog: {changelogVersion}")
        versions.add(changelogVersion)

    if len(versions) != 1:
        print("The source version, tag, and optionally changelog version do not match.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
