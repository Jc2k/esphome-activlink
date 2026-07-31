#!/usr/bin/env python3
"""Build the matched private bootstrap, public production, and migration set."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from firmware_security import (
    FirmwareProfile,
    ROOT,
    SIGNING_KEY,
    assert_matching_partitions,
    sha256_file,
    validate_private_inputs,
    verify_build,
)


CONFIGS = {
    FirmwareProfile.BOOTSTRAP: ROOT / "honeywell-gateway.bootstrap.yaml",
    FirmwareProfile.PRODUCTION: ROOT / "honeywell-gateway.release.yaml",
    FirmwareProfile.MIGRATION: ROOT / "honeywell-gateway.migration.yaml",
}
VERSION_PATTERN = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
VERSION_MARKER = 'firmware_version: "0.0.0-dev"'


def versioned_config(source: Path, version: str) -> Path:
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError(f"version must be dotted numeric, got {version!r}")
    text = source.read_text()
    if text.count(VERSION_MARKER) != 1:
        raise RuntimeError(f"expected one version marker in {source.name}")
    handle = tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=ROOT,
        prefix=f".private-build-{source.stem}-",
        suffix=".yaml",
        delete=False,
    )
    with handle:
        handle.write(text.replace(VERSION_MARKER, f'firmware_version: "{version}"'))
    return Path(handle.name)


def build_profile(profile: FirmwareProfile, version: str):
    config = versioned_config(CONFIGS[profile], version)
    try:
        subprocess.run(
            [sys.executable, "-m", "esphome", "compile", str(config)],
            cwd=ROOT,
            check=True,
        )
    finally:
        config.unlink(missing_ok=True)
    return verify_build(profile, SIGNING_KEY)


def artifact_metadata(path: Path) -> dict[str, object]:
    return {
        "file": path.name,
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
        "md5": hashlib.md5(path.read_bytes(), usedforsecurity=False).hexdigest(),
    }


def prepare(version: str) -> Path:
    validate_private_inputs()
    builds = [build_profile(profile, version) for profile in FirmwareProfile]
    assert_matching_partitions(builds)

    artifact_dir = ROOT / ".provisioning" / "artifacts"
    shutil.rmtree(artifact_dir, ignore_errors=True)
    artifact_dir.mkdir(parents=True, mode=0o700)

    manifest_profiles: dict[str, dict[str, object]] = {}
    for build in builds:
        ota_name = f"{build.profile.value}.ota.bin"
        factory_name = f"{build.profile.value}.factory.bin"
        ota_target = artifact_dir / ota_name
        factory_target = artifact_dir / factory_name
        shutil.copy2(build.ota_firmware, ota_target)
        shutil.copy2(build.factory_firmware, factory_target)
        ota_target.chmod(0o600)
        factory_target.chmod(0o600)
        manifest_profiles[build.profile.value] = {
            "private": build.profile is not FirmwareProfile.PRODUCTION,
            "ota": artifact_metadata(ota_target),
            "factory": artifact_metadata(factory_target),
        }

    manifest = {
        "schema": 1,
        "version": version,
        "chip": "ESP32-C6",
        "nvs_hmac_key_id": 0,
        "nvs_offset": builds[0].nvs_offset,
        "partition_table_sha256": builds[0].partition_sha256,
        "profiles": manifest_profiles,
    }
    manifest_path = artifact_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    manifest_path.chmod(0o600)
    return manifest_path


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} VERSION")
    manifest = prepare(sys.argv[1])
    print(f"Verified provisioning set written to {manifest.parent}")
    print("Private bootstrap and migration artifacts must never be published.")


if __name__ == "__main__":
    main()
