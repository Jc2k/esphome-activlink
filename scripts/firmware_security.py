#!/usr/bin/env python3
"""Build-time security checks shared by release and provisioning workflows."""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass
from enum import Enum
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

import yaml


ROOT = Path(__file__).resolve().parent.parent
SIGNING_KEY = ROOT / ".firmware-signing-key.pem"


class FirmwareProfile(str, Enum):
    BOOTSTRAP = "bootstrap"
    PRODUCTION = "production"
    MIGRATION = "migration"


BUILD_DIRECTORIES = {
    FirmwareProfile.BOOTSTRAP: ROOT
    / ".esphome"
    / "build"
    / "honeywell-activlink-bootstrap",
    FirmwareProfile.PRODUCTION: ROOT
    / ".esphome"
    / "build"
    / "honeywell-activlink-production",
    FirmwareProfile.MIGRATION: ROOT
    / ".esphome"
    / "build"
    / "honeywell-activlink-migration",
}


@dataclass(frozen=True)
class VerifiedBuild:
    profile: FirmwareProfile
    ota_firmware: Path
    factory_firmware: Path
    partition_table: Path
    partition_sha256: str
    nvs_offset: int


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sdkconfig(path: Path) -> dict[str, str]:
    """Parse both assigned and '# CONFIG_X is not set' sdkconfig lines."""
    options: dict[str, str] = {}
    unset_pattern = re.compile(r"^# (CONFIG_[A-Z0-9_]+) is not set$")
    for line in path.read_text().splitlines():
        if match := unset_pattern.fullmatch(line):
            options[match.group(1)] = "n"
        elif line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            options[key] = value.strip('"')
    return options


def _require(options: dict[str, str], key: str, expected: str) -> None:
    actual = options.get(key, "n")
    matches = actual == expected
    if actual.lower().startswith("0x") and expected.lower().startswith("0x"):
        try:
            matches = int(actual, 0) == int(expected, 0)
        except ValueError:
            matches = False
    if not matches:
        raise RuntimeError(f"security check failed: {key}={actual!r}, expected {expected!r}")


def _check_sdkconfig(profile: FirmwareProfile, sdkconfig: Path) -> None:
    options = parse_sdkconfig(sdkconfig)
    common = {
        "CONFIG_PARTITION_TABLE_OFFSET": "0xc000",
        "CONFIG_NVS_ENCRYPTION": "y",
        "CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC": "y",
        "CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID": "0",
        "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES": "y",
        "CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME": "y",
        "CONFIG_SECURE_FLASH_ENC_ENABLED": "n",
    }
    for key, value in common.items():
        _require(options, key, value)

    if profile is FirmwareProfile.BOOTSTRAP:
        _require(options, "CONFIG_SECURE_BOOT", "n")
        _require(options, "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT", "y")
        _require(options, "CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT", "y")
        return

    production = {
        "CONFIG_SECURE_BOOT": "y",
        "CONFIG_SECURE_BOOT_V2_ENABLED": "y",
        "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT": "n",
        "CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT": "n",
        "CONFIG_SECURE_BOOT_FLASH_BOOTLOADER_DEFAULT": "y",
        "CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE": "y",
        "CONFIG_SECURE_BOOT_INSECURE": "n",
        "CONFIG_SECURE_BOOT_ALLOW_JTAG": "n",
        "CONFIG_SECURE_BOOT_V2_ALLOW_EFUSE_RD_DIS": "n",
        "CONFIG_SECURE_BOOT_ENABLE_AGGRESSIVE_KEY_REVOKE": "n",
    }
    for key, value in production.items():
        _require(options, key, value)


def _parse_size(value: str) -> int:
    value = value.strip()
    suffixes = {"K": 1024, "M": 1024 * 1024}
    if value[-1:].upper() in suffixes:
        return int(value[:-1], 0) * suffixes[value[-1:].upper()]
    return int(value, 0)


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def partition_layout(
    csv_path: Path, partition_table_offset: int = 0x8000
) -> dict[str, tuple[int, int]]:
    """Resolve implicit ESP-IDF partition offsets from an ESPHome CSV."""
    cursor = partition_table_offset + 0x1000
    layout: dict[str, tuple[int, int]] = {}
    for raw_line in csv_path.read_text().splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            raise RuntimeError(f"malformed partition row in {csv_path}: {raw_line!r}")
        name, part_type, _subtype, offset_text, size_text = fields[:5]
        alignment = 0x10000 if part_type == "app" else 0x1000
        offset = int(offset_text, 0) if offset_text else _align(cursor, alignment)
        size = _parse_size(size_text)
        if offset < cursor:
            raise RuntimeError(f"partition {name!r} overlaps its predecessor")
        layout[name] = (offset, size)
        cursor = offset + size
    return layout


def _verify_signature(image: Path, signing_key: Path) -> None:
    subprocess.run(
        [
            sys.executable,
            "-m",
            "espsecure",
            "verify-signature",
            "--version",
            "2",
            "--keyfile",
            str(signing_key),
            str(image),
        ],
        cwd=ROOT,
        check=True,
    )


def verify_build(
    profile: FirmwareProfile,
    signing_key: Path = SIGNING_KEY,
) -> VerifiedBuild:
    """Prove the built images have the security and storage invariants we rely on."""
    build_root = BUILD_DIRECTORIES[profile]
    output = build_root / "build"
    sdkconfig = build_root / "sdkconfig.honeywell-activlink"
    ota_firmware = output / "firmware.ota.bin"
    factory_firmware = output / "firmware.factory.bin"
    application = output / "honeywell-activlink.bin"
    bootloader = output / "bootloader" / "bootloader.bin"
    partition_csv = build_root / "partitions.csv"
    partition_bin = output / "partition_table" / "partition-table.bin"
    defines = build_root / "src" / "esphome" / "core" / "defines.h"
    generated_main = build_root / "src" / "main.cpp"
    flasher_args = output / "flasher_args.json"

    required_files = [
        signing_key,
        sdkconfig,
        ota_firmware,
        factory_firmware,
        application,
        bootloader,
        partition_csv,
        partition_bin,
        defines,
        generated_main,
        flasher_args,
    ]
    for path in required_files:
        if not path.is_file():
            raise FileNotFoundError(f"missing build input: {path}")

    _check_sdkconfig(profile, sdkconfig)
    sdkconfig_options = parse_sdkconfig(sdkconfig)
    _verify_signature(ota_firmware, signing_key)
    if sha256_file(application) != sha256_file(ota_firmware):
        raise RuntimeError("factory and OTA builds do not use the same signed application")
    if profile is not FirmwareProfile.BOOTSTRAP:
        _verify_signature(bootloader, signing_key)

    definitions = defines.read_text()
    generated_code = generated_main.read_text()
    has_tlv = "USE_OPENTHREAD_TLVS" in definitions
    is_forced = "USE_OPENTHREAD_FORCE_DATASET" in definitions
    if "USE_API_NOISE_PSK_FROM_YAML" in definitions:
        raise RuntimeError(
            "API key must be saved to and loaded from encrypted NVS, not configured in api:"
        )
    if not has_tlv:
        raise RuntimeError("security check failed: build has no compiled Thread TLV")
    if profile is FirmwareProfile.PRODUCTION:
        if '#define USE_OPENTHREAD_TLVS "00"' not in definitions or is_forced:
            raise RuntimeError("production build does not use the fail-closed Thread sentinel")
        if "USE_API_USER_DEFINED_ACTIONS" not in definitions:
            raise RuntimeError("production build is missing the signed recovery action")
        if "->set_api_encryption_key(" in generated_code:
            raise RuntimeError("production build embeds a private API encryption key")
    elif profile is FirmwareProfile.MIGRATION:
        if not is_forced or '#define USE_OPENTHREAD_TLVS "00"' in definitions:
            raise RuntimeError("migration build does not force the private Thread dataset")
        if "->set_api_encryption_key(" not in generated_code:
            raise RuntimeError("migration build cannot restore the saved API encryption key")
    elif is_forced:
        raise RuntimeError("bootstrap build must not force the dataset after its first boot")
    elif "->set_api_encryption_key(" not in generated_code:
        raise RuntimeError("bootstrap build cannot persist the API encryption key")

    flash_map = json.loads(flasher_args.read_text()).get("flash_files", {})
    if "0x0" not in flash_map or "0x10000" not in flash_map:
        raise RuntimeError("factory flash map omits the bootloader or primary application")

    table_offset = int(sdkconfig_options.get("CONFIG_PARTITION_TABLE_OFFSET", "0x8000"), 0)
    layout = partition_layout(partition_csv, table_offset)
    if "nvs" not in layout:
        raise RuntimeError("partition table has no NVS partition")
    nvs_offset, _ = layout["nvs"]
    if factory_firmware.stat().st_size > nvs_offset:
        raise RuntimeError(
            "factory image reaches the NVS partition; a production flash would erase credentials"
        )

    factory = factory_firmware.read_bytes()
    expected_regions = {
        0: bootloader.read_bytes(),
        table_offset: partition_bin.read_bytes(),
        layout["app0"][0]: application.read_bytes(),
    }
    for offset, expected in expected_regions.items():
        if factory[offset : offset + len(expected)] != expected:
            raise RuntimeError(
                f"factory image does not contain the verified build region at {offset:#x}"
            )

    return VerifiedBuild(
        profile=profile,
        ota_firmware=ota_firmware,
        factory_firmware=factory_firmware,
        partition_table=partition_bin,
        partition_sha256=sha256_file(partition_bin),
        nvs_offset=nvs_offset,
    )


# Length constraints for standard Thread Active Operational Dataset TLVs. A
# dataset may legitimately be partial: OpenThread requires only the Network Key
# to attach, then retrieves and persists the complete dataset from its parent.
_DATASET_TLV_LENGTHS: dict[int, tuple[int, int]] = {
    0: (3, 3),  # Channel
    1: (2, 2),  # PAN ID
    2: (8, 8),  # Extended PAN ID
    3: (1, 16),  # Network Name
    4: (16, 16),  # PSKc
    5: (16, 16),  # Network Key
    7: (8, 8),  # Mesh Local Prefix
    12: (3, 4),  # Security Policy (version-dependent flags length)
    14: (8, 8),  # Active Timestamp
    53: (3, 35),  # Channel Mask
}
_REQUIRED_BOOTSTRAP_TLVS = {5}  # Network Key


def validate_thread_dataset_tlv(value: object) -> None:
    """Reject placeholders, malformed TLVs, and datasets without a Network Key."""
    if not isinstance(value, str) or not value:
        raise ValueError("thread_tlv must be a non-empty hex string")
    try:
        raw = bytes.fromhex(value)
    except ValueError as error:
        raise ValueError("thread_tlv is not valid hexadecimal") from error
    if len(raw) > 254:
        raise ValueError("thread_tlv exceeds OpenThread's 254-byte dataset limit")

    found: dict[int, int] = {}
    cursor = 0
    while cursor < len(raw):
        if cursor + 2 > len(raw):
            raise ValueError("thread_tlv ends inside a TLV header")
        tlv_type, length = raw[cursor], raw[cursor + 1]
        cursor += 2
        if cursor + length > len(raw):
            raise ValueError("thread_tlv ends inside a TLV value")
        if tlv_type in found:
            raise ValueError(f"thread_tlv contains duplicate type {tlv_type}")
        found[tlv_type] = length
        cursor += length

    missing = sorted(_REQUIRED_BOOTSTRAP_TLVS - set(found))
    if missing:
        raise ValueError(f"thread_tlv is missing required types {missing}")
    for tlv_type, length in found.items():
        if tlv_type not in _DATASET_TLV_LENGTHS:
            continue
        minimum, maximum = _DATASET_TLV_LENGTHS[tlv_type]
        if not minimum <= length <= maximum:
            raise ValueError(
                f"thread_tlv type {tlv_type} has length {length}, expected {minimum}..{maximum}"
            )


def validate_private_inputs(
    secrets_file: Path = ROOT / "secrets.yaml",
    signing_key: Path = SIGNING_KEY,
) -> None:
    """Validate private provisioning inputs without ever printing their values."""
    if not signing_key.is_file():
        raise FileNotFoundError(f"missing firmware signing key: {signing_key}")
    if signing_key.stat().st_mode & 0o077:
        raise PermissionError(f"firmware signing key must be mode 0600: {signing_key}")
    if not secrets_file.is_file():
        raise FileNotFoundError(f"missing private secrets file: {secrets_file}")
    secrets = yaml.safe_load(secrets_file.read_text())
    if not isinstance(secrets, dict):
        raise ValueError("secrets.yaml must contain a mapping")
    validate_thread_dataset_tlv(secrets.get("thread_tlv"))

    api_key = secrets.get("api_encryption_key")
    if not isinstance(api_key, str):
        raise ValueError("api_encryption_key must be a base64 string")
    try:
        decoded_api_key = base64.b64decode(api_key, validate=True)
    except (binascii.Error, ValueError) as error:
        raise ValueError("api_encryption_key is not valid base64") from error
    if len(decoded_api_key) != 32 or not any(decoded_api_key):
        raise ValueError("api_encryption_key must be a nonzero 32-byte key")

    ota_password = secrets.get("ota_password")
    if not isinstance(ota_password, str) or len(ota_password) < 16:
        raise ValueError("ota_password must be a non-placeholder value of at least 16 characters")


def assert_matching_partitions(builds: list[VerifiedBuild]) -> None:
    fingerprints = {build.partition_sha256 for build in builds}
    offsets = {build.nvs_offset for build in builds}
    if len(fingerprints) != 1 or len(offsets) != 1:
        raise RuntimeError("bootstrap, production, and migration partition layouts differ")
