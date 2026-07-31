#!/usr/bin/env python3
"""Build versioned firmware and prepare GitHub Release and Pages assets."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from firmware_security import FirmwareProfile, verify_build


ROOT = Path(__file__).resolve().parent.parent
SOURCE_CONFIG = ROOT / "honeywell-gateway.release.yaml"
SIGNING_KEY = ROOT / ".firmware-signing-key.pem"
REPOSITORY_URL = "https://github.com/Jc2k/esphome-activlink"
PAGES_URL = "https://unrouted.uk/esphome-activlink"
VERSION_PATTERN = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$"
)


def versioned_config(version: str) -> Path:
    """Create a temporary root config with the release version substituted."""
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError(f"invalid semantic version: {version!r}")

    source = SOURCE_CONFIG.read_text()
    marker = 'firmware_version: "0.0.0-dev"'
    if source.count(marker) != 1:
        raise RuntimeError(f"expected exactly one {marker!r} in {SOURCE_CONFIG.name}")

    handle = tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=ROOT,
        prefix=".release-",
        suffix=".yaml",
        delete=False,
    )
    with handle:
        handle.write(source.replace(marker, f'firmware_version: "{version}"'))
    return Path(handle.name)


def build_firmware(version: str) -> tuple[Path, Path]:
    """Compile and verify the hardware-Secure-Boot release images."""
    if not SIGNING_KEY.is_file():
        raise FileNotFoundError(
            f"missing firmware signing key: {SIGNING_KEY}; see README.md"
        )

    config = versioned_config(version)
    try:
        subprocess.run(
            [sys.executable, "-m", "esphome", "compile", str(config)],
            cwd=ROOT,
            check=True,
        )
    finally:
        config.unlink(missing_ok=True)

    # Do not publish merely because ESPHome returned successfully. Prove that
    # NVS encryption, hardware Secure Boot V2, Secure Download mode, the signed
    # bootloader/app, the fail-closed Thread sentinel, and the NVS boundary are
    # all present in the artifacts that will actually be released.
    build = verify_build(FirmwareProfile.PRODUCTION, SIGNING_KEY)
    return build.ota_firmware, build.factory_firmware


def prepare_assets(version: str, ota_firmware: Path, factory_firmware: Path) -> None:
    """Create release downloads and a deployable GitHub Pages tree."""
    release_assets = ROOT / "release-assets"
    site = ROOT / "site"
    shutil.rmtree(release_assets, ignore_errors=True)
    shutil.rmtree(site, ignore_errors=True)

    version_directory = site / "firmware" / f"v{version}"
    version_directory.mkdir(parents=True)
    release_assets.mkdir()

    firmware_name = "firmware.bin"
    factory_firmware_name = "firmware.factory.bin"
    pages_firmware = version_directory / firmware_name
    shutil.copy2(ota_firmware, pages_firmware)
    shutil.copy2(ota_firmware, release_assets / firmware_name)
    shutil.copy2(factory_firmware, version_directory / factory_firmware_name)
    shutil.copy2(factory_firmware, release_assets / factory_firmware_name)

    digest = hashlib.md5(ota_firmware.read_bytes(), usedforsecurity=False).hexdigest()
    manifest = {
        "name": "ESPHome Honeywell ActivLink gateway",
        "version": version,
        "builds": [
            {
                "chipFamily": "ESP32-C6",
                "ota": {
                    "md5": digest,
                    "path": f"{PAGES_URL}/firmware/v{version}/{firmware_name}",
                    "release_url": f"{REPOSITORY_URL}/releases/tag/v{version}",
                    "summary": f"ESPHome ActivLink gateway v{version}",
                },
            }
        ],
    }
    manifest_text = json.dumps(manifest, indent=2) + "\n"
    (site / "firmware" / "manifest.json").write_text(manifest_text)
    (release_assets / "manifest.json").write_text(manifest_text)
    (site / ".nojekyll").touch()
    (site / "index.html").write_text(
        "<!doctype html>\n"
        '<html lang="en"><meta charset="utf-8">\n'
        "<title>ESPHome ActivLink firmware</title>\n"
        "<h1>ESPHome ActivLink firmware</h1>\n"
        f"<p>Latest release: <a href=\"{REPOSITORY_URL}/releases/tag/v{version}\">"
        f"v{version}</a></p>\n"
        f'<p><a href="firmware/v{version}/{factory_firmware_name}">'
        "Hardware-Secure-Boot factory firmware</a></p>\n"
        "<p>Provision a private bootstrap image first. This public image "
        "intentionally contains no Thread dataset.</p>\n"
        '<p><a href="firmware/manifest.json">OTA manifest</a></p>\n'
    )


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} VERSION")
    version = sys.argv[1]
    ota_firmware, factory_firmware = build_firmware(version)
    prepare_assets(version, ota_firmware, factory_firmware)


if __name__ == "__main__":
    main()
