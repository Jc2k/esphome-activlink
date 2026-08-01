from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import provision_device  # noqa: E402


def test_security_flag_mapping() -> None:
    info = provision_device.SecurityInfo(
        flags=(1 << 0) | (1 << 2),
        key_purposes={0: "HMAC_UP", 1: "SECURE_BOOT_DIGEST0"},
        raw="",
    )
    assert info.secure_boot
    assert info.secure_download


def test_security_report_parser(monkeypatch) -> None:
    purposes = ["HMAC_UP", "SECURE_BOOT_DIGEST0"] + ["USER/EMPTY"] * 4
    report = "Flags: 0x00000005 (0b101)\n" + "".join(
        f"  BLOCK_KEY{index} - {purpose}\n"
        for index, purpose in enumerate(purposes)
    )

    def fake_run_tool(_arguments, *, capture=False):
        assert capture
        return subprocess.CompletedProcess(args=[], returncode=0, stdout=report)

    monkeypatch.setattr(provision_device, "run_tool", fake_run_tool)
    info = provision_device.security_info("ignored")
    assert info.secure_boot
    assert info.secure_download
    assert info.key_purposes[0] == "HMAC_UP"


def test_device_mac_is_normalized(monkeypatch) -> None:
    def fake_run_tool(_arguments, *, capture=False):
        assert capture
        return subprocess.CompletedProcess(
            args=[], returncode=0, stdout="MAC: AA:BB:CC:DD:EE:FF\n"
        )

    monkeypatch.setattr(provision_device, "run_tool", fake_run_tool)
    assert provision_device.device_mac("ignored") == "aa:bb:cc:dd:ee:ff"


def test_device_mac_prefers_base_address_over_eui64(monkeypatch) -> None:
    def fake_run_tool(_arguments, *, capture=False):
        assert capture
        return subprocess.CompletedProcess(
            args=[],
            returncode=0,
            stdout=(
                "MAC:                AA:BB:CC:DD:EE:FF:00:11\n"
                "BASE MAC:           22:33:44:55:66:77\n"
            ),
        )

    monkeypatch.setattr(provision_device, "run_tool", fake_run_tool)
    assert provision_device.device_mac("ignored") == "22:33:44:55:66:77"


def test_initial_erase_uses_stub(monkeypatch, tmp_path) -> None:
    commands = []
    monkeypatch.setattr(provision_device, "run_tool", lambda command: commands.append(command))
    image = tmp_path / "bootstrap.factory.bin"

    provision_device.write_factory("port", image, erase_all=True)

    assert "--erase-all" in commands[0]
    assert "--no-stub" not in commands[0]


def test_nvs_preserving_write_uses_rom_mode(monkeypatch, tmp_path) -> None:
    commands = []
    monkeypatch.setattr(provision_device, "run_tool", lambda command: commands.append(command))
    image = tmp_path / "production.factory.bin"

    provision_device.write_factory("port", image, erase_all=False)

    assert "--erase-all" not in commands[0]
    assert "--no-stub" in commands[0]
