# Secure Thread provisioning and dataset rotation

This is the production provisioning runbook for an ESP32-C6 Honeywell ActivLink
gateway. It puts a private Thread Active Operational Dataset into encrypted NVS,
replaces the private bootstrap with a public production image, and enables
hardware Secure Boot V2 only after the retained data has been proved to work.

The public production binary contains neither the Thread dataset nor the Home
Assistant API key. Improv is not involved: it cannot provision an OpenThread
dataset, and the Apple border routers do not provide a custom Thread Joiner
extension point.

## Security result and limits

The completed device has these properties:

- eFuse key block 0 contains a random, per-device `HMAC_UP` key. It is
  read-protected and is used by ESP-IDF to encrypt the default NVS partition.
- the Thread dataset and saved ESPHome API Noise PSK reside in encrypted NVS;
- Secure Boot V2 verifies the RSA-3072-signed second-stage bootloader and app;
- ESPHome rejects OTA apps signed by a different key and rejects lower firmware
  versions;
- JTAG is disabled and the ROM is left in Secure Download mode; and
- a normal production release has no reusable Thread credential embedded in it.

This design does **not** enable full flash encryption. Secure Boot provides code
authenticity, not secrecy for application partitions. A private bootstrap or
migration binary contains the dataset in plaintext and must be handled as a
credential. Initial provisioning overwrites the bootstrap application. After an
online migration, production must be installed twice to scrub both alternating
OTA slots. Full protection against physical flash removal would require a
separately designed flash-encryption workflow.

The irreversible operations are performed by firmware on boot, not by this
repository calling `espefuse` directly. Nevertheless, burning an HMAC key,
enabling Secure Boot, disabling JTAG, and restricting ROM download mode are
permanent eFuse changes.

## The three firmware profiles

| Profile | Dataset | Hardware Secure Boot configuration | Purpose |
| --- | --- | --- | --- |
| `bootstrap` | private TLV compiled in | deliberately off | First USB boot; burns the NVS HMAC key and saves the dataset/API key in encrypted NVS. |
| `production` | invalid `00` fail-closed sentinel | on | Public release; uses the existing encrypted dataset and saved API key. |
| `migration` | private TLV compiled in, `force_dataset: true` | on | Same-key signed recovery image that replaces a retained dataset. Never publish it. |

The one-byte production sentinel is intentional. ESPHome first asks OpenThread
for the active dataset in NVS and ignores the compiled value when one exists. On
a blank device, OpenThread rejects `00` as a malformed dataset and startup
fails. A public production image therefore cannot silently create or join a
known fallback Thread network.

All three profiles use the same partition table and signing key. The partition
table is moved to `0xC000` because the signed Secure Boot bootloader does not fit
before the ESP-IDF default `0x8000` location:

| Region | Offset | Size |
| --- | ---: | ---: |
| bootloader | `0x000000` | at most `0x00C000` |
| partition table | `0x00C000` | `0x001000` |
| OTA data | `0x00D000` | `0x002000` |
| PHY init | `0x00F000` | `0x001000` |
| app0 | `0x010000` | `0x1C0000` |
| app1 | `0x1D0000` | `0x1C0000` |
| encrypted NVS | `0x390000` | `0x070000` |

The build verifier rejects mismatched partition tables and any factory image
that reaches `0x390000`. Factory writes can therefore replace bootloader,
partition metadata, and app0 without touching NVS.

## Prepare the private artifact set

Use a trusted workstation with an encrypted disk. Keep the signing key backed up
offline; a device with Secure Boot enabled cannot accept a recovery image made
with a replacement key.

1. Install the locked dependencies:

       uv sync --locked

2. Create the RSA-3072 key once, if this deployment does not already have one:

       uv run python -m espsecure generate-signing-key \
         --version 2 --scheme rsa3072 .firmware-signing-key.pem
       chmod 600 .firmware-signing-key.pem

3. Copy `secrets.example.yaml` to `secrets.yaml`. Set `thread_tlv` to the
   hex-encoded Active Operational Dataset exported for the Thread network.
   Apple/Home Assistant may export a partial dataset; this is valid as long as
   it contains the Thread Network Key. OpenThread uses the supplied fields to
   attach, then retrieves and persists the complete Active Dataset from its
   parent. The builder validates the TLV framing, known field lengths, and
   presence of the Network Key without printing any private field.
   If Home Assistant already has an API encryption key for this device, retain
   that value. Otherwise generate a 32-byte Noise PSK in ESPHome's required
   Base64 representation:

       openssl rand -base64 32

   Generate a separate 256-bit password for the bootstrap's temporary ESPHome
   OTA endpoint. Hex avoids quoting and character-set ambiguity:

       openssl rand -hex 32

   Paste the first output as the quoted `api_encryption_key` value and the
   second as the quoted `ota_password` value. Never reuse one value for the
   other. Home Assistant must be configured with the same API encryption key;
   changing an existing key requires updating or reconfiguring its ESPHome
   integration. The OTA password is used only by the private bootstrap profile;
   production HTTP OTA relies on the firmware signature instead.

   Do not paste any private value into a command line argument, issue, log, or
   commit. The builder requires exactly 32 nonzero decoded bytes for the API
   key and rejects OTA passwords shorter than 16 characters. It also rejects
   the example's all-zero API key because it would not provide the intended
   authentication.

4. Build a matched set. The version must be three numeric components and should
   be the production version that the device will run:

       uv run python scripts/prepare_provisioning.py 1.0.0

The builder validates the TLV structure without printing it, checks key-file
permissions, compiles all three profiles, cryptographically verifies each app
signature and each Secure Boot bootloader signature, checks every relevant
`sdkconfig` setting, and proves the partition tables match. It writes mode-0600
files and their SHA-256/MD5 metadata under `.provisioning/artifacts/`.

Both `.provisioning/` and the private ESPHome build directories contain
credential-bearing binaries and are ignored by Git. They are still secrets:
restrict backups and delete them after provisioning if they are not needed for
recovery. Secure deletion cannot be guaranteed on SSDs, which is why the build
volume itself should be encrypted. Keep `secrets.yaml` and the signing key in
your normal secret-management process.

## Initial device procedure

Use stable USB power and a direct data cable. Do this only on a new device or a
device whose current contents can be erased. Replace the port below with the
actual serial device. Global options deliberately precede the subcommand.

### 1. Inspect the untouched security state

Put the ESP32-C6 in ROM download mode and run:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX status

Do not continue if Secure Boot is unexpectedly enabled or eFuse key block 0 has
an unrelated purpose.

### 2. Flash and boot the private bootstrap

This is the **only** workflow step that erases the whole flash. Because the
ESP32-C6 ROM does not implement chip-wide erase, the guard uses esptool's
temporary RAM stub for this pre-lock operation; later writes use ROM
`--no-stub` mode:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      flash-bootstrap --confirm-initial-erase

Let the device boot normally. On this first boot ESP-IDF generates the
per-device HMAC key in eFuse block 0, read-protects it, initializes encrypted
NVS, and OpenThread saves the compiled dataset there. The custom component also
saves the API Noise PSK in NVS.

Confirm all of the following before proceeding:

- the log shows the device attaching to the intended Thread network;
- Home Assistant connects using the expected encrypted native API key;
- a doorbell event can be received; and
- after a full power cycle, the log says `Found existing dataset, ignoring
  config` and the device attaches again.

The power cycle matters: it proves that the running dataset came back from NVS,
not merely from the bootstrap image.

### 3. Record the pre-lock checkpoint

Return the device to ROM download mode and explicitly attest that the runtime
checks above passed:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      verify-bootstrap --confirm-dataset-retained

The guard also verifies that eFuse block 0 now has purpose `HMAC_UP` and that
Secure Boot is still off. Its mode-0600 receipt is bound to the device MAC and
the exact artifact-manifest hash, so it cannot authorize a different board or
build set. It refuses to create the checkpoint otherwise. When provisioning
devices in parallel, pass a distinct `--state` path for each one.

### 4. Flash production and enable Secure Boot

This is the irreversible transition. Verify the signing-key backups and use
stable power:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      flash-production --confirm-irreversible-secure-boot

There is intentionally no erase flag. The signed factory image stops before
NVS, so the encrypted dataset and API key survive. On the next normal boot the
signed bootloader burns its RSA public-key digest into the next available eFuse
key block, enables Secure Boot V2, disables JTAG, and enables Secure ROM Download
mode. The prior HMAC key remains in block 0.

Once it boots, confirm Thread attachment, the log line `Loaded saved Noise PSK`,
Home Assistant connectivity, and a doorbell event. Then return to ROM download
mode for the final read-only proof:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX verify-production

The command requires the Secure Boot and Secure Download flag bits, `HMAC_UP` in
key block 0, and a `SECURE_BOOT_DIGEST` in another key block. No further eFuse
write is performed.

After this point, never use `erase-flash`, `--erase-all`, ESP Web Tools, or a
generic factory flasher. Secure Download mode requires `--no-stub`, and only
images signed by the retained key can boot. Use the guarded commands below.

## Normal production updates

Public releases are built from `honeywell-gateway.release.yaml`. They contain
the fail-closed dataset sentinel and load the real dataset/API key from encrypted
NVS. The Home Assistant update entity installs the signed OTA binary from the
public manifest. The signing key and version checks are enforced on the device.

Do not publish anything from `.provisioning/artifacts/`. Only the independent
`release-assets/` production output made by `scripts/prepare_release.py` belongs
on the public firmware site.

## Changing the Thread dataset later

There are two materially different cases.

### Same Thread network: use a Pending Operational Dataset

Thread's supported credential-rotation mechanism is a Pending Operational
Dataset. The network commissioner distributes it while the old network is
still working and activates it at the chosen delay. OpenThread persists the
result automatically; no private firmware or public release change is needed.

Use this whenever the border-router/controller ecosystem exposes it. Apple may
manage such changes internally, but it does not generally expose arbitrary
commissioner controls to this firmware.

### Different network, or the device missed the rekey: use migration firmware

Update `thread_tlv` in `secrets.yaml`, retain the **same RSA signing key**, and
build a new matched provisioning set. Use a version equal to the installed
version or a higher version; a lower version is rejected by downgrade
protection.

The migration profile is signed for the already-enabled hardware trust anchor
and has `force_dataset: true`. On boot it replaces the old active dataset in
encrypted NVS. It also embeds the same API key so physical recovery still works
if NVS was accidentally erased.

#### Online migration while the old Thread network works

1. Put `migration.ota.bin` behind a short-lived, access-controlled HTTPS URL
   with a certificate the ESP-IDF trust bundle accepts, and which the device can
   reach. Do not use a public static firmware site: the binary contains the new
   Thread credential. Keep the URL itself out of logs where practical.
2. In Home Assistant Developer Tools, invoke the device's
   `install_signed_firmware` ESPHome action with that URL and the migration OTA
   MD5 recorded in `.provisioning/artifacts/manifest.json`. Depending on the
   Home Assistant version, its generated name is similar to
   `esphome.honeywell_activlink_install_signed_firmware`.
3. Watch it reboot onto the new Thread network and confirm API connectivity.
4. Invoke the same action with the **public production OTA** URL and MD5.
5. After production reconnects, install that same production OTA a second time.
   ESP-IDF alternates app0 and app1; the second pass overwrites the slot that
   held the private migration image.
6. Remove the private migration URL and purge its server/cache copies.

The RSA signature supplies authenticity. MD5 in this action is only a transport
integrity check; it is not the trust anchor.

#### Offline migration over USB

Put the locked device in ROM download mode. Flash the same-key signed migration
factory without erasing NVS:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      flash-migration --confirm-preserve-nvs

Let it boot and attach to the new network. Then return to ROM download mode and
replace app0 with the public production factory:

    uv run python scripts/provision_device.py \
      --port /dev/cu.usbmodemXXXX \
      restore-production --confirm-preserve-nvs

One USB production factory write is enough because both factory writes target
app0; the private migration app is directly overwritten. Neither command uses
an erase-all operation or reaches NVS.

## Failure and recovery matrix

| Situation | Recovery |
| --- | --- |
| Bootstrap does not join or survive a reboot | Stop before production. Correct the TLV/wiring/config and repeat the initial erase/bootstrap. No hardware Secure Boot eFuse has been burned. |
| Power loss during production flash, before Secure Boot enables | Re-enter ROM download mode and repeat the guarded production write using the same artifact set. |
| Dataset changes while device remains attached | Prefer a Thread Pending Operational Dataset; OpenThread stores it without firmware. |
| Device cannot reach its old Thread network | Use same-key signed migration over USB. |
| NVS was erased after lock | Same-key signed migration can recreate the dataset and compiled API key; never change HMAC `key_id: 0`. |
| Private migration remains in the inactive OTA slot | Install public production OTA twice, or perform the USB production factory write. |
| Signing key is lost | Existing locked devices cannot accept new bootstrap/migration/production code. An in-band Thread rekey may still work if they remain attached; otherwise the device is stranded. |
| eFuse key block 0 already has another purpose | Do not run this workflow. It assumes block 0 is empty initially and reserves it permanently for NVS `HMAC_UP`. |

## Implementation files

- `honeywell-gateway.bootstrap.yaml`: private, first-boot profile.
- `honeywell-gateway.release.yaml`: public production profile.
- `honeywell-gateway.migration.yaml`: private, force-dataset profile.
- `packages/security-bootstrap.yaml`: signed OTA plus HMAC-backed NVS, hardware
  Secure Boot deliberately absent.
- `packages/security-production.yaml`: HMAC-backed NVS plus ESP32-C6 Secure Boot
  V2 and Secure Download settings.
- `packages/thread-retained.yaml`: public fail-closed dataset sentinel.
- `packages/managed-updates.yaml`: public update entity and authenticated
  same-key migration action.
- `scripts/prepare_provisioning.py`: private three-profile builder/verifier.
- `scripts/provision_device.py`: eFuse-state-aware flashing guard and receipts.
- `scripts/firmware_security.py`: dataset, sdkconfig, signature, image-boundary,
  and partition-consistency checks.

## Primary references

- [ESPHome ESP32 platform: signed OTA and NVS encryption](https://esphome.io/components/esp32/)
- [ESP-IDF ESP32-C6 Secure Boot V2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/security/secure-boot-v2.html)
- [ESP-IDF security enablement workflows](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/security/security-features-enablement-workflows.html)
- [ESP-IDF NVS encryption](https://docs.espressif.com/projects/esp-idf/en/release-v5.2/esp32c6/api-reference/storage/nvs_encryption.html)
- [esptool erase/write behavior](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html)
- [OpenThread operational dataset concepts and CLI](https://openthread.io/reference/cli/concepts/dataset)
- [OpenThread Operational Dataset API](https://openthread.io/reference/group/api-operational-dataset)
