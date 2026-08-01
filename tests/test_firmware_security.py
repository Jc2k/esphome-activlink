from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from firmware_security import (  # noqa: E402
    parse_sdkconfig,
    partition_layout,
    validate_private_inputs,
    validate_thread_dataset_tlv,
)


# Public example from OpenThread's dataset CLI documentation, never a private
# network credential. It exercises every component required by our validator.
COMPLETE_DATASET = (
    "0e080000000000010000000300001035060004001fffe00208e227ac6a7f24052f"
    "0708fdb753eb517cb4d3051062b2442a928d9ea3b947a1618fc4085a030f4f7065"
    "6e5468726561642d393837330102987304105330d857354330133c05e1fd7ae81a91"
    "0c0402a0f7f8"
)

# Partial datasets are valid OpenThread bootstrap credentials. Only the Network
# Key is required to attach; the node obtains the complete Active Dataset from
# its parent. These are public test values, not deployment credentials.
PARTIAL_DATASET = (
    "0e08000000000001000000030000100208e227ac6a7f24052f"
    "051062b2442a928d9ea3b947a1618fc4085a030f4f70656e5468726561642d39383733"
    "01029873"
)


def test_complete_thread_dataset_is_accepted() -> None:
    validate_thread_dataset_tlv(COMPLETE_DATASET)


def test_partial_thread_dataset_with_network_key_is_accepted() -> None:
    validate_thread_dataset_tlv(PARTIAL_DATASET)


@pytest.mark.parametrize(
    "dataset",
    [
        "replace-me",
        "00",
        "0e08ff",
        "0e080000000000000000",
    ],
)
def test_placeholder_or_incomplete_dataset_is_rejected(dataset: str) -> None:
    with pytest.raises(ValueError):
        validate_thread_dataset_tlv(dataset)


def test_partition_layout_preserves_expected_nvs_boundary(tmp_path: Path) -> None:
    table = tmp_path / "partitions.csv"
    table.write_text(
        "otadata, data, ota, , 0x2000,\n"
        "phy_init, data, phy, , 0x1000,\n"
        "app0, app, ota_0, , 0x1C0000,\n"
        "app1, app, ota_1, , 0x1C0000,\n"
        "nvs, data, nvs, , 0x70000,\n"
    )
    layout = partition_layout(table)
    assert layout["app0"] == (0x10000, 0x1C0000)
    assert layout["app1"] == (0x1D0000, 0x1C0000)
    assert layout["nvs"] == (0x390000, 0x70000)


def test_sdkconfig_parser_records_disabled_options(tmp_path: Path) -> None:
    sdkconfig = tmp_path / "sdkconfig"
    sdkconfig.write_text(
        "CONFIG_SECURE_BOOT=y\n"
        "# CONFIG_SECURE_BOOT_INSECURE is not set\n"
        'CONFIG_SECURE_BOOT_SIGNING_KEY="private.pem"\n'
    )
    assert parse_sdkconfig(sdkconfig) == {
        "CONFIG_SECURE_BOOT": "y",
        "CONFIG_SECURE_BOOT_INSECURE": "n",
        "CONFIG_SECURE_BOOT_SIGNING_KEY": "private.pem",
    }


def test_private_inputs_accept_realistic_non_placeholder_values(tmp_path: Path) -> None:
    key = tmp_path / "signing.pem"
    key.write_text("test-only")
    key.chmod(0o600)
    secrets = tmp_path / "secrets.yaml"
    secrets.write_text(
        f'thread_tlv: "{COMPLETE_DATASET}"\n'
        'api_encryption_key: "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE="\n'
        'ota_password: "a-test-password-with-entropy"\n'
    )
    validate_private_inputs(secrets, key)


@pytest.mark.parametrize(
    "api_key",
    [
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "not-base64",
    ],
)
def test_private_inputs_reject_placeholder_api_keys(
    tmp_path: Path, api_key: str
) -> None:
    key = tmp_path / "signing.pem"
    key.write_text("test-only")
    key.chmod(0o600)
    secrets = tmp_path / "secrets.yaml"
    secrets.write_text(
        f'thread_tlv: "{COMPLETE_DATASET}"\n'
        f'api_encryption_key: "{api_key}"\n'
        'ota_password: "a-test-password-with-entropy"\n'
    )
    with pytest.raises(ValueError):
        validate_private_inputs(secrets, key)
