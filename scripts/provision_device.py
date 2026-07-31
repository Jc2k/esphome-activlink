#!/usr/bin/env python3
"""Guard the irreversible ESP32-C6 bootstrap-to-Secure-Boot transition."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
import sys

from firmware_security import ROOT, sha256_file


DEFAULT_MANIFEST = ROOT / ".provisioning" / "artifacts" / "manifest.json"
DEFAULT_STATE = ROOT / ".provisioning" / "device-state.json"


@dataclass(frozen=True)
class SecurityInfo:
    flags: int
    key_purposes: dict[int, str]
    raw: str

    @property
    def secure_boot(self) -> bool:
        return bool(self.flags & (1 << 0))

    @property
    def secure_download(self) -> bool:
        return bool(self.flags & (1 << 2))


def run_tool(arguments: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", *arguments],
        cwd=ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )


def security_info(port: str) -> SecurityInfo:
    result = run_tool(
        [
            "esptool",
            "--chip",
            "esp32c6",
            "--port",
            port,
            "--no-stub",
            "get-security-info",
        ],
        capture=True,
    )
    output = result.stdout
    flags_match = re.search(r"^Flags:\s+(0x[0-9a-fA-F]+)", output, re.MULTILINE)
    if not flags_match:
        raise RuntimeError("could not parse esptool security flags")
    purposes = {
        int(index): purpose.strip()
        for index, purpose in re.findall(
            r"^\s*BLOCK_KEY([0-5])\s+-\s+(.+)$", output, re.MULTILINE
        )
    }
    if len(purposes) != 6:
        raise RuntimeError("could not parse all six ESP32-C6 eFuse key purposes")
    return SecurityInfo(int(flags_match.group(1), 16), purposes, output)


def device_mac(port: str) -> str:
    result = run_tool(
        ["esptool", "--chip", "esp32c6", "--port", port, "--no-stub", "read-mac"],
        capture=True,
    )
    # ESP32-C6 output may label the 802.15.4 EUI-64 as MAC and print a separate
    # six-byte BASE MAC. Prefer the stable base address, but accept either form.
    match = re.search(
        r"^BASE MAC:\s+((?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2})$",
        result.stdout,
        re.MULTILINE,
    )
    if not match:
        match = re.search(
            r"^MAC:\s+((?:[0-9a-fA-F]{2}:){5,7}[0-9a-fA-F]{2})$",
            result.stdout,
            re.MULTILINE,
        )
    if not match:
        raise RuntimeError("could not parse the ESP32-C6 MAC address")
    return match.group(1).lower()


def load_manifest(path: Path) -> tuple[dict[str, object], str]:
    manifest = json.loads(path.read_text())
    if manifest.get("schema") != 1 or manifest.get("chip") != "ESP32-C6":
        raise RuntimeError(f"unsupported provisioning manifest: {path}")
    if manifest.get("nvs_hmac_key_id") != 0:
        raise RuntimeError("manifest does not reserve eFuse KEY0 for NVS")
    manifest_hash = sha256_file(path)
    profiles = manifest.get("profiles")
    if not isinstance(profiles, dict):
        raise RuntimeError("manifest has no firmware profiles")
    for profile in ("bootstrap", "production", "migration"):
        profile_data = profiles.get(profile)
        if not isinstance(profile_data, dict):
            raise RuntimeError(f"manifest has no {profile} profile")
        for image_type in ("ota", "factory"):
            metadata = profile_data.get(image_type)
            if not isinstance(metadata, dict) or not isinstance(metadata.get("file"), str):
                raise RuntimeError(f"manifest has no {profile} {image_type} image")
            image = path.parent / Path(metadata["file"]).name
            if not image.is_file():
                raise FileNotFoundError(image)
            if image.stat().st_size != metadata.get("size") or sha256_file(image) != metadata.get("sha256"):
                raise RuntimeError(f"artifact hash/size mismatch: {image}")
    production_factory = profiles["production"]["factory"]
    if production_factory["size"] > manifest.get("nvs_offset", 0):
        raise RuntimeError("production factory image overlaps NVS")
    return manifest, manifest_hash


def load_state(path: Path) -> dict[str, object]:
    if not path.is_file():
        return {}
    state = json.loads(path.read_text())
    return state if isinstance(state, dict) else {}


def save_state(path: Path, **values: object) -> None:
    state = load_state(path)
    state.update(values)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n")
    path.chmod(0o600)


def require_key0(info: SecurityInfo, allowed: set[str]) -> None:
    purpose = info.key_purposes[0]
    if purpose not in allowed:
        raise RuntimeError(
            f"eFuse BLOCK_KEY0 purpose is {purpose!r}; expected one of {sorted(allowed)}"
        )


def artifact_path(manifest_path: Path, manifest: dict[str, object], profile: str) -> Path:
    filename = manifest["profiles"][profile]["factory"]["file"]
    return manifest_path.parent / Path(filename).name


def write_factory(port: str, image: Path, *, erase_all: bool) -> None:
    command = [
        "esptool",
        "--chip",
        "esp32c6",
        "--port",
        port,
        "--no-stub",
        "write-flash",
    ]
    if erase_all:
        command.append("--erase-all")
    command.extend(["0x0", str(image)])
    run_tool(command)


def command_status(args: argparse.Namespace) -> None:
    print(security_info(args.port).raw, end="")


def command_flash_bootstrap(args: argparse.Namespace) -> None:
    if not args.confirm_initial_erase:
        raise RuntimeError("refusing full erase without --confirm-initial-erase")
    manifest, manifest_hash = load_manifest(args.manifest)
    info = security_info(args.port)
    if info.secure_boot:
        raise RuntimeError("refusing bootstrap: hardware Secure Boot is already enabled")
    require_key0(info, {"USER/EMPTY", "HMAC_UP"})
    mac = device_mac(args.port)
    write_factory(args.port, artifact_path(args.manifest, manifest, "bootstrap"), erase_all=True)
    save_state(
        args.state,
        stage="bootstrap_flashed",
        device_mac=mac,
        manifest_sha256=manifest_hash,
        version=manifest["version"],
    )
    print("Bootstrap flashed. Let it boot, attach to Thread, then power-cycle it once.")


def command_verify_bootstrap(args: argparse.Namespace) -> None:
    if not args.confirm_dataset_retained:
        raise RuntimeError("refusing checkpoint without --confirm-dataset-retained")
    _manifest, manifest_hash = load_manifest(args.manifest)
    state = load_state(args.state)
    if state.get("stage") != "bootstrap_flashed" or state.get("manifest_sha256") != manifest_hash:
        raise RuntimeError("bootstrap receipt is missing or belongs to another artifact set")
    if state.get("device_mac") != device_mac(args.port):
        raise RuntimeError("connected device does not match the bootstrap receipt")
    info = security_info(args.port)
    if info.secure_boot:
        raise RuntimeError("Secure Boot became enabled before the production checkpoint")
    require_key0(info, {"HMAC_UP"})
    save_state(args.state, stage="bootstrap_verified")
    print("Checkpoint recorded: encrypted NVS key exists and the dataset survived a reboot.")


def command_flash_production(args: argparse.Namespace) -> None:
    if not args.confirm_irreversible_secure_boot:
        raise RuntimeError(
            "refusing production flash without --confirm-irreversible-secure-boot"
        )
    manifest, manifest_hash = load_manifest(args.manifest)
    state = load_state(args.state)
    if state.get("stage") != "bootstrap_verified" or state.get("manifest_sha256") != manifest_hash:
        raise RuntimeError("the verified-bootstrap checkpoint for this artifact set is missing")
    if state.get("device_mac") != device_mac(args.port):
        raise RuntimeError("connected device does not match the verified bootstrap receipt")
    info = security_info(args.port)
    if info.secure_boot:
        raise RuntimeError("initial production transition was already completed")
    require_key0(info, {"HMAC_UP"})
    # Deliberately no --erase-all: the image stops before the NVS offset.
    write_factory(args.port, artifact_path(args.manifest, manifest, "production"), erase_all=False)
    save_state(args.state, stage="production_flashed")
    print("Production flashed without erasing NVS. Its first boot now enables Secure Boot.")


def command_verify_production(args: argparse.Namespace) -> None:
    _manifest, manifest_hash = load_manifest(args.manifest)
    state = load_state(args.state)
    if state.get("stage") != "production_flashed" or state.get("manifest_sha256") != manifest_hash:
        raise RuntimeError("production-flash receipt is missing or belongs to another artifact set")
    info = security_info(args.port)
    if not info.secure_boot:
        raise RuntimeError("hardware Secure Boot is not enabled")
    if not info.secure_download:
        raise RuntimeError("Secure ROM Download mode is not enabled")
    require_key0(info, {"HMAC_UP"})
    if not any(purpose.startswith("SECURE_BOOT_DIGEST") for purpose in info.key_purposes.values()):
        raise RuntimeError("no Secure Boot public-key digest is present in eFuse")
    save_state(args.state, stage="complete")
    print("Provisioning complete: encrypted NVS, hardware Secure Boot, and Secure Download are enabled.")


def command_flash_migration(args: argparse.Namespace) -> None:
    if not args.confirm_preserve_nvs:
        raise RuntimeError("refusing migration flash without --confirm-preserve-nvs")
    manifest, _manifest_hash = load_manifest(args.manifest)
    info = security_info(args.port)
    if not info.secure_boot:
        raise RuntimeError("use the initial bootstrap workflow before hardware Secure Boot")
    if not info.secure_download:
        raise RuntimeError("Secure ROM Download mode is not enabled")
    require_key0(info, {"HMAC_UP"})
    write_factory(args.port, artifact_path(args.manifest, manifest, "migration"), erase_all=False)
    print("Signed migration image flashed without erasing NVS; it will replace the dataset on boot.")


def command_restore_production(args: argparse.Namespace) -> None:
    if not args.confirm_preserve_nvs:
        raise RuntimeError("refusing production restore without --confirm-preserve-nvs")
    manifest, _manifest_hash = load_manifest(args.manifest)
    info = security_info(args.port)
    if not info.secure_boot:
        raise RuntimeError("this command is only for an already locked device")
    if not info.secure_download:
        raise RuntimeError("Secure ROM Download mode is not enabled")
    require_key0(info, {"HMAC_UP"})
    write_factory(args.port, artifact_path(args.manifest, manifest, "production"), erase_all=False)
    print("Signed public production image restored without erasing encrypted NVS.")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--port", required=True, help="serial port for the ESP32-C6")
    result.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    result.add_argument("--state", type=Path, default=DEFAULT_STATE)
    commands = result.add_subparsers(dest="command", required=True)

    status = commands.add_parser("status", help="read ROM security information")
    status.set_defaults(handler=command_status)

    bootstrap = commands.add_parser("flash-bootstrap", help="erase and flash the private bootstrap")
    bootstrap.add_argument("--confirm-initial-erase", action="store_true")
    bootstrap.set_defaults(handler=command_flash_bootstrap)

    verify_bootstrap = commands.add_parser(
        "verify-bootstrap", help="record the NVS/dataset checkpoint"
    )
    verify_bootstrap.add_argument("--confirm-dataset-retained", action="store_true")
    verify_bootstrap.set_defaults(handler=command_verify_bootstrap)

    production = commands.add_parser(
        "flash-production", help="preserve NVS and start the irreversible Secure Boot transition"
    )
    production.add_argument("--confirm-irreversible-secure-boot", action="store_true")
    production.set_defaults(handler=command_flash_production)

    verify_production = commands.add_parser(
        "verify-production", help="prove the final hardware security state"
    )
    verify_production.set_defaults(handler=command_verify_production)

    migration = commands.add_parser(
        "flash-migration", help="USB recovery for a locked device with an obsolete dataset"
    )
    migration.add_argument("--confirm-preserve-nvs", action="store_true")
    migration.set_defaults(handler=command_flash_migration)

    restore = commands.add_parser(
        "restore-production",
        help="replace a migration image on a locked device while preserving NVS",
    )
    restore.add_argument("--confirm-preserve-nvs", action="store_true")
    restore.set_defaults(handler=command_restore_production)
    return result


def main() -> None:
    args = parser().parse_args()
    try:
        args.handler(args)
    except (FileNotFoundError, PermissionError, RuntimeError, ValueError) as error:
        raise SystemExit(f"ERROR: {error}") from error


if __name__ == "__main__":
    main()
