# Secure Wi-Fi and BLE Improv provisioning

This runbook provisions an ESP32-C6 Honeywell ActivLink gateway without putting
an SSID or Wi-Fi password in source code, build logs, release artifacts, or
GitHub Actions. BLE Improv sends the network credential directly to the device,
where ESPHome stores it in HMAC-backed encrypted NVS.

The firmware keeps three security profiles:

| Profile | Private material | Hardware Secure Boot | Purpose |
| --- | --- | --- | --- |
| `bootstrap` | API Noise key only | deliberately off | First USB boot; creates the NVS HMAC key and saves the API key. Wi-Fi still arrives through BLE Improv. |
| `production` | none | on | Public release; loads retained credentials and accepts BLE Improv onboarding. |
| `recovery` | API Noise key only | on | Same-key signed recovery after NVS damage. Wi-Fi still arrives through BLE Improv. Never publish it. |

Secure Boot authenticates code; it does not make application partitions secret.
The public production image therefore contains neither Wi-Fi credentials nor
the Home Assistant API key. The bootstrap and recovery images are private
because they contain the API key.

## Security invariants

- eFuse `BLOCK_KEY0` is reserved for the NVS `HMAC_UP` key;
- the default NVS partition starts at `0x390000` and is encrypted;
- every factory image stops before NVS;
- production and recovery are signed by the same RSA-3072 key trusted by the
  device's Secure Boot digest;
- production has Wi-Fi and BLE Improv enabled but no configured station;
- every signed application retains at least 32 KiB of OTA-slot headroom;
- BLE Improv requires the physical BOOT button before accepting credentials;
- a provisioning window expires after ten minutes and reopens only after a
  reset or power cycle; and
- USB Serial/JTAG logging remains available at 115200 baud.

The build verifier checks these properties against generated C++, sdkconfig,
partition tables, signed binaries, and the assembled factory image. A successful
ESPHome compile alone is not sufficient.

## Prepare private inputs

Install the pinned dependencies:

    cp secrets.example.yaml secrets.yaml
    uv sync

Generate independent values for the API and temporary bootstrap OTA credentials:

    openssl rand -base64 32
    openssl rand -hex 32

Put them in `secrets.yaml` as `api_encryption_key` and `ota_password`. Do not add
an SSID or Wi-Fi password. The `thread_tlv` example value is needed only by the
legacy Thread example and is ignored by Wi-Fi firmware.

Generate the deployment signing key once:

    uv run python -m espsecure generate-signing-key \
      --version 2 --scheme rsa3072 .firmware-signing-key.pem
    chmod 600 .firmware-signing-key.pem

Keep an offline backup. A locked device cannot accept firmware signed by a
replacement key, even over USB.

## Build the matched provisioning set

Choose a deployable version and build all three profiles together:

    uv run python scripts/prepare_provisioning.py 1.3.0

The command writes mode-0600 artifacts under `.provisioning/artifacts/` and
cryptographically verifies them. Only `production.*.bin` is public. Never
publish `bootstrap.*.bin`, `recovery.*.bin`, `secrets.yaml`, the provisioning
manifest, or the signing key.

## Provision a new, unlocked device

### 1. Flash the private bootstrap

This is the only workflow step that deliberately erases the full device. Do not
run it on a device that contains pairings or already has Secure Boot enabled.

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      flash-bootstrap --confirm-initial-erase

The first boot creates the per-device NVS encryption key in eFuse block 0 and
saves the API Noise key into encrypted NVS.

### 2. Provision Wi-Fi through BLE Improv

Use a BLE Improv client while near the gateway. The advertised name includes the
last three bytes of the Feather's MAC, for example
`honeywell-activlink-829248`.

1. Select that gateway in the Improv client.
2. Briefly press and release **BOOT** when authorization is requested.
3. Supply a 2.4 GHz SSID and password.
4. Confirm the USB log reports a Wi-Fi connection and an IP address.
5. Add or reconnect the ESPHome device in Home Assistant using the API key from
   `secrets.yaml`.

The button authorizes Improv for one minute. A normal short press also remains
the ActivLink pairing control, so it may briefly enter RF pairing mode during
onboarding; that mode times out harmlessly after 30 seconds.

### 3. Prove encrypted-NVS retention

Power-cycle the gateway. Confirm that it reconnects without another BLE session,
the log reports `Loaded saved Noise PSK`, and Home Assistant reconnects. Then
return the device to ROM download mode and record the checkpoint:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      verify-bootstrap --confirm-nvs-retained

The checkpoint is bound to the device MAC and exact artifact-manifest hash.

### 4. Install production and enable Secure Boot

This transition is irreversible. Verify the signing-key backup first:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      flash-production --confirm-irreversible-secure-boot

The write does not erase NVS. On first boot, the signed bootloader enables
Secure Boot V2, disables JTAG, and enables restricted Secure ROM Download mode.
Confirm Wi-Fi, the API, and an ActivLink event, then verify the final eFuse state:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX verify-production

## Move an existing locked Thread device to Wi-Fi

Build the new matched artifact set with the original RSA signing key. Put the
locked gateway in ROM download mode and restore production without erasing NVS:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      restore-production --confirm-preserve-nvs

The retained API key and ActivLink pairings survive. The obsolete Thread dataset
also remains encrypted in NVS but is unused by the Wi-Fi firmware. On boot, use
BLE Improv and the BOOT button to supply Wi-Fi credentials.

Never use `erase-flash`, `--erase-all`, ESP Web Tools, or an unguarded ESPHome
serial upload on a locked managed device.

## Change Wi-Fi later

If the saved network cannot be reached, the gateway starts BLE Improv after 90
seconds. Briefly press **BOOT**, then provide replacement credentials. Improv
must complete within the ten-minute provisioning window. Reset or power-cycle
the gateway to reopen an expired window.

No firmware rebuild, GitHub secret, private recovery image, or NVS erase is
needed for an ordinary SSID/password change.

## Recover after NVS damage

If the API key was lost but the HMAC key and Secure Boot state remain valid,
build with the original signing key and flash the private recovery image:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      flash-recovery --confirm-preserve-nvs

Provision Wi-Fi through BLE Improv, confirm the API key works, then replace the
private image with public production:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      restore-production --confirm-preserve-nvs

An encrypted NVS partition cannot be recreated if its `HMAC_UP` key in eFuse
block 0 is lost or replaced.

## GitHub Actions boundary

GitHub Actions receives only the firmware signing key used for public release
signing. Its build-only `secrets.yaml` contains public test values for schema
validation, not deployment Wi-Fi credentials. `scripts/firmware_security.py`
rejects a release if generated code contains a configured Wi-Fi station or any
OpenThread support/dataset.

## Relevant files

- `packages/wifi.yaml`: credential-free Wi-Fi, BLE Improv, physical
  authorization, and provisioning window;
- `honeywell-gateway.bootstrap.yaml`: private first-boot profile;
- `honeywell-gateway.release.yaml`: public production profile;
- `honeywell-gateway.recovery.yaml`: private same-key API recovery profile;
- `packages/security-bootstrap.yaml`: signed OTA plus HMAC-backed NVS;
- `packages/security-production.yaml`: NVS encryption plus hardware Secure Boot;
- `scripts/prepare_provisioning.py`: matched three-profile builder;
- `scripts/firmware_security.py`: generated-build and credential-boundary checks;
- `scripts/provision_device.py`: guarded flashing and eFuse-state checks.
