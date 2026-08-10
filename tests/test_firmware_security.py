from pathlib import Path
import sys

import pytest
import yaml
from esphome import yaml_util


ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scripts"))

from firmware_security import (  # noqa: E402
    parse_sdkconfig,
    partition_layout,
    validate_private_inputs,
    validate_thread_dataset_tlv,
)
from components.honeywell_activlink.api_resource_patch import (  # noqa: E402
    patch_api_resource,
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


class SecretsLoader(yaml.SafeLoader):
    """Parse package YAML without resolving deployment-only secrets."""


SecretsLoader.add_constructor(
    "!secret", lambda loader, node: loader.construct_scalar(node)
)
SecretsLoader.add_constructor(
    "!include", lambda loader, node: loader.construct_scalar(node)
)


def test_shared_firmware_uses_stable_mac_suffixed_hostname() -> None:
    config = yaml_util.load_yaml(ROOT / "packages" / "core.yaml")

    assert config["esphome"]["name"] == "honeywell-activlink"
    assert config["esphome"]["name_add_mac_suffix"] is True


@pytest.mark.parametrize("profile", ["thread.yaml", "thread-retained.yaml"])
def test_gateway_is_an_always_listening_thread_leaf(profile: str) -> None:
    source = (ROOT / "packages" / profile).read_text()
    config = yaml.load(source, Loader=SecretsLoader)

    assert config["openthread"]["device_type"] == "MTD"
    # Omitting poll_period makes ESPHome configure an MTD with its receiver on
    # while idle, rather than a sleepy end device.
    assert "poll_period" not in config["openthread"]


def test_thread_diagnostics_expose_selected_srp_server() -> None:
    config = yaml_util.load_yaml(ROOT / "packages" / "thread-diagnostics.yaml")

    assert config["thread_diagnostics"]["srp_server"]["name"] == "Thread SRP Server"


def test_production_keeps_usb_console_logging_available() -> None:
    source = (ROOT / "honeywell-gateway.release.yaml").read_text()
    config = yaml.load(source, Loader=SecretsLoader)

    assert config["logger"]["baud_rate"] == 115200


def test_wifi_uses_physically_authorized_ble_improv_without_credentials() -> None:
    source = (ROOT / "packages" / "wifi.yaml").read_text()
    config = yaml.load(source, Loader=SecretsLoader)

    assert "ssid" not in config["wifi"]
    assert "password" not in config["wifi"]
    assert "networks" not in config["wifi"]
    assert config["esp32_improv"]["authorizer"] == "activlink_pairing_control"
    assert config["provisioning"]["timeout"] == "10min"


def test_activlink_rmt_queue_can_absorb_repeated_bursts() -> None:
    config = yaml_util.load_yaml(ROOT / "packages" / "core.yaml")

    # The CC1101's configured frequency compensator can track one quarter of
    # the receive bandwidth. 325 kHz covers the 65--73 kHz offset observed in
    # European ActivLink captures; the normal GPIO filter must not swallow
    # marginal but real symbol transitions.
    assert str(config["cc1101"]["filter_bandwidth"]) == "325kHz"
    assert str(config["remote_receiver"]["filter"]) == "50us"
    # ESPHome reserves four bytes per receive symbol for every queued capture.
    # Keep a full ActivLink transmission while leaving several capture windows
    # in the software ring; 4096 symbols allowed only two and overflowed live.
    assert config["remote_receiver"]["receive_symbols"] == 1024
    assert str(config["remote_receiver"]["buffer_size"]) == "40kb"


def test_activlink_diagnostics_expose_burst_decoder_state() -> None:
    config = yaml_util.load_yaml(ROOT / "packages" / "diagnostics.yaml")
    activlink = config["honeywell_activlink"]

    assert activlink["candidate_rows"]["name"] == "ActivLink Candidate Rows"
    assert (
        activlink["last_capture_summary"]["name"]
        == "ActivLink Last Rejected Capture"
    )
    assert activlink["radio_recoveries"]["name"] == "ActivLink Radio Recoveries"

    core = yaml_util.load_yaml(ROOT / "packages" / "core.yaml")
    assert core["honeywell_activlink"]["radio_id"] == "activlink_radio"


def test_runtime_subdevice_api_patch_matches_pinned_esphome() -> None:
    api_root = Path(__import__("esphome").__file__).parent / "components" / "api"

    header = patch_api_resource("api_pb2.h", (api_root / "api_pb2.h").read_text())
    connection = patch_api_resource(
        "api_connection.cpp", (api_root / "api_connection.cpp").read_text()
    )

    assert "StaticVector<DeviceInfo, ESPHOME_DEVICE_COUNT> devices{};" in header
    assert "resp.devices.emplace_next()" in connection


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
        f'api_encryption_key: "{api_key}"\n'
        'ota_password: "a-test-password-with-entropy"\n'
    )
    with pytest.raises(ValueError):
        validate_private_inputs(secrets, key)
