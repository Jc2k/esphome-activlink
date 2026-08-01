# ESPHome Honeywell ActivLink gateway

This project turns an ESP32-C6 and a CC1101 into a receive-only Home Assistant
gateway for European Honeywell/Friedland ActivLink transmitters.

Each configured bell push is represented as its own ESPHome sub-device in Home
Assistant, with:

- one doorbell event entity with **press** and **secret_press** event types;
- one **battery_low** binary sensor driven by the transmitted LOWBAT flag.

All checked-in device configurations use Thread and ESPHome's native API.
Managed production devices use a guarded local bootstrap before the
irreversible security transition. Matter is not needed; ESPHome's native API
provides local delivery, events, encryption, and sub-devices over Thread.

## M146 hardware and Veroboard wiring

The final radio is the [M5Stack M146 CC1101 module](https://docs.m5stack.com/en/module/Module_CC1101).
Its E07-900M10S radio, 855--925 MHz matching, SMA connector, and supplied antenna
are suitable for the 868 MHz ActivLink signal. This module differs from the
eight-pin prototype in one important respect: power M146 from **5 V**, not the
Feather's 3.3 V pin. It has its own 5 V-to-3.3 V regulator.

Wire the M146's 30-pin M5-Bus connector directly to the Veroboard as follows:

| M146 M5-Bus pin | Signal | Feather connection | ESP32-C6 GPIO |
| --- | --- | --- | --- |
| 28 | 5 V input | USB | -- |
| 1, 3, 5 | Ground | GND rail | -- |
| 11 | SCK | SCK | GPIO21 |
| 7 | MOSI | MO | GPIO22 |
| 9 | MISO | MI | GPIO23 |
| 8 | CSN | A3 | GPIO5 |
| 2 | GDO0 receive data | header pad 14 (see note below) | GPIO14 |

Set the M146 routing switches before powering it:

- set exactly the CSN switch labelled **25** on; leave CSN 12, 15, and 0 off;
- set exactly the GDO0 switch labelled **35** on; leave GDO0 5 and 13 off; and
- leave every GDO2 switch off because this receiver does not use GDO2.

The switch labels are legacy M5Stack Core GPIO numbers, **not Feather GPIO
numbers**. With the selections above they route CSN to physical bus pin 8 and
GDO0 to physical bus pin 2; the Veroboard then connects those pins to Feather
GPIO5 and GPIO14 respectively.

The spare Feather header pad is GPIO14. Adafruit boards made before 23 January
2025 may have that pad mistakenly silkscreened **12**; the board schematic still
connects it to ESP32-C6 GPIO14. Do not use actual GPIO12: it is the USB D- signal
and connecting GDO0 there would interfere with USB flashing, logs, and JTAG.

Do not connect M146 bus pin 12 to the Feather 3V pin, and do not connect HPWR,
BAT, GDO2, or other pass-through pins. Powering the Feather through its USB-C
connector makes 5 V available at the Feather USB pin for M146. If the Feather is
instead powered only from LiPo, provide a separate regulated 5 V supply for
M146; do not feed 5 V into the Feather USB rail while a computer is also
connected.

For a robust Veroboard build, use a 2x15 2.54 mm socket so M146 remains
replaceable, and mechanically support the module rather than hanging it from
wires. Confirm pin 1 from the module marking and the official connector drawing
before soldering because the apparent left/right numbering reverses when the
connector is viewed from the other side. Use a short common ground rail, keep
SPI and GDO0 tracks short, and add a 10--47 uF bulk capacitor between M146 pin
28 and ground near the socket. Mount the SMA end where the connector and antenna
are supported, preferably with the antenna upright and clear of the Feather,
USB cable, and metalwork.

The checked-in configurations now use GPIO14 for GDO0. This leaves the Feather's
GPIO18/GPIO19 I2C bus available for its onboard MAX17048 battery monitor. The
earlier 433 MHz prototype was verified with GPIO18; its successful chip ID and
waveform captures remain useful evidence for the decoder and radio profile, but
its physical wiring and power instructions do not apply to M146.

## Protocol

European ActivLink uses 2-FSK centred on 868.3 MHz, approximately 50 kHz
deviation and 6250 symbols/s. The linked 433 MHz-matched module did not produce
data with native 2-FSK demodulation, but it successfully receives the lower FSK
sideband as ASK/OOK at 868.21 MHz. That proven fallback is the checked-in radio
profile. A transmission repeats a 48-bit PWM frame many times. The decoder:

1. accepts either electrical polarity from GDO0;
2. requires two matching frames;
3. verifies even parity;
4. extracts the 20-bit transmitter ID, device type, secret-press, relay, and
   LOWBAT flags;
5. ignores non-doorbell ActivLink device types.

This follows the published
[ActivLink frame analysis](https://github.com/klohner/honeywell-wireless-doorbell)
and the maintained
[rtl_433 decoder](https://github.com/merbanan/rtl_433/blob/master/src/devices/honeywell_wdb.c).
Physical captures from transmitter 0xFB100 confirm normal frame FB1000200001
and secret-press frame FB1000200010; both reported a healthy battery.

## Set up a disposable Thread development device

Requirements are pinned in uv.lock; do not install ESPHome globally.

    cp secrets.example.yaml secrets.yaml
    uv sync
    uv run esphome config honeywell-gateway.yaml
    uv run esphome run honeywell-gateway.yaml

Before the final command:

1. edit `secrets.yaml` and replace `thread_tlv` with the preferred Thread
   network's active operational dataset;
2. generate a native API encryption key with `openssl rand -base64 32` and
   paste it as the quoted `api_encryption_key` value;
3. generate an independent bootstrap OTA password with
   `openssl rand -hex 32` and paste it as the quoted `ota_password` value;
4. confirm the board is an Adafruit ESP32-C6 Feather and CSN is on A3; and
5. connect the board by USB.

The first flash should be over USB. This development profile embeds the Thread
dataset and API key, so do not distribute its binary. Use the managed workflow
below for a device that will be permanently secured.

### Managed firmware updates

Managed devices use a private local bootstrap to put the Thread Active
Operational Dataset and Home Assistant API key into HMAC-backed encrypted NVS.
A public production image then preserves NVS and enables ESP32-C6 hardware
Secure Boot V2. Public binaries contain neither credential; a fail-closed
sentinel prevents them from joining a known fallback network on blank flash.

This is an irreversible, ordered procedure—not a generic ESPHome or Web Tools
flash. Follow the complete [secure Thread provisioning and dataset rotation
runbook](docs/secure-thread-provisioning.md). Its guarded scripts verify the
dataset, signing key, sdkconfig, bootloader/app signatures, partition boundaries,
eFuse state, and the pre-lock reboot checkpoint.

Released firmware advertises an update entity in Home Assistant and checks the
manifest at
[unrouted.uk/esphome-activlink](https://unrouted.uk/esphome-activlink/). Every
public release is signed with the deployment's RSA-3072 key. Hardware Secure
Boot and ESPHome's OTA verifier reject unsigned or differently signed code, and
downgrade protection rejects older versions. After lock, USB recovery also
requires same-key signed firmware and must preserve NVS.

Releases are built, versioned, and published automatically from semantic commit
messages after the host tests and the complete ESP32-C6 firmware build pass.
Use Conventional Commit prefixes such as `fix:`, `feat:`, and `feat!:` (or a
`BREAKING CHANGE:` footer) to request patch, minor, and major releases.
ESPHome is pinned exactly and updated daily by Dependabot; those updates use
`fix(deps):` and therefore produce a patch release. GitHub Actions are checked
weekly and use `ci(deps):`, so Actions-only updates do not produce a release.

The release workflow expects the private PEM key in the GitHub Actions secret
`FIRMWARE_SIGNING_KEY`. Generate the key once, keep an offline backup, and copy
the complete PEM file into that secret:

    uv run python -m espsecure generate-signing-key \
      --version 2 --scheme rsa3072 .firmware-signing-key.pem

The key file is ignored by Git. Losing or replacing it prevents Secure-Boot
devices from accepting any new firmware or dataset-migration image; USB does not
bypass the hardware trust anchor. Already installed code keeps running. Keep an
offline backup.
Pull requests build and cryptographically verify a hardware-Secure-Boot release
with a throwaway key, while releases use only the configured Actions secret.

### Discover another transmitter ID

The first captured transmitter, ID 0xFB100, is checked in as Front Door Button.
With logs open, press another bell push. A successfully decoded unconfigured
button prints a message like:

    Unconfigured ActivLink doorbell id=0x8DF50; add activlink_id: 0x8DF50 ...

Add that value to [packages/core.yaml](packages/core.yaml), compile again, and
Home Assistant will receive events from its new sub-device. The example 0x8DF50
is from a public rtl_433 capture; it is not your ID.

If no ActivLink line appears, temporarily add **dump: raw** under
**remote_receiver:** in packages/core.yaml. Raw timings around 160/320/480 µs
confirm that the CC1101 data path works. Remove the raw dump after diagnosis
because a long ActivLink broadcast produces a large log.

### Add more buttons

For every additional transmitter, add another item to both the existing
**esphome.devices** list and the existing **honeywell_activlink.buttons** list:

    esphome:
      devices:
        - id: back_door_button
          name: Back Door Button

    honeywell_activlink:
      buttons:
        - activlink_id: 0x12345
          event:
            name: Press
            device_id: back_door_button
          battery_low:
            name: Battery low
            device_id: back_door_button
            entity_category: diagnostic

Merge these entries into the existing lists rather than adding duplicate
top-level keys.

## Thread after RF bring-up

ESPHome can carry its native API over Thread on the C6; this is not Matter. You
need a working Home Assistant Thread border router and the preferred network's
active operational dataset.

For a disposable development device, the example Thread configuration remains
available. For a managed device, do not flash it directly. Copy the complete
hex-encoded dataset TLV into `secrets.yaml`, then follow the [secure Thread
runbook](docs/secure-thread-provisioning.md) to build and verify the private
bootstrap, prove encrypted-NVS retention across a power cycle, install public
production, and only then enable hardware Secure Boot.

If the dataset changes later, a normal Thread Pending Operational Dataset is
persisted automatically. Moving to an unrelated network uses a private,
same-signing-key migration image; the runbook covers both online and USB paths
and removal of the private image from both OTA slots.

## Home Assistant diagnostics

All checked-in profiles publish the no-extra-wiring diagnostics from
`packages/diagnostics.yaml` once per minute. They include:

- ESP32-C6 uptime, die temperature, reset reason, heap health, and maximum
  component loop time;
- Thread attachment and role, RLOC16, channel, partition, SRP state, attachment
  duration, parent changes, attach attempts, IPv6 failures, and MAC retry/error
  counters;
- parent RLOC16, average RSSI, and link quality while the C6 is a Thread child;
  these are unavailable when it is acting as a router or leader because those
  roles do not have a parent;
- ActivLink valid frames, rejected captures, duplicates, unconfigured IDs, and
  the age of the last valid decoded frame.

The counters start again after a reboot. All of these entities use Home
Assistant's diagnostic category so they do not crowd the main device controls.
Battery-voltage and CC1101 RSSI/frequency-error telemetry are intentionally left
for a later revision. Moving GDO0 to GPIO14 makes the Feather's GPIO18/GPIO19
I2C bus and onboard MAX17048 available, but its ESPHome integration still needs
to be added and validated. Reliable CC1101 per-frame metrics likewise need M146
and receive-state handling to be tested together.

## RF troubleshooting

- “CC1101 found” with a nonzero chip ID proves SPI/CSN wiring, not RF reception.
- FF0F, 0000, or FFFF chip IDs normally indicate bad SPI, CSN, power, or ground
  wiring.
- M146 is matched for 855--925 MHz and has an external SMA antenna, making it
  the preferred hardware for dependable range in this build.
- Keep the Veroboard tracks short. M146 has onboard decoupling; the additional
  nearby bulk capacitor helps with wiring and supply transients.
- This project's 433 MHz-matched module works with ASK/OOK reception on the
  lower FSK sideband at 868.21 MHz. For a proper 868/915 MHz module, native
  2-FSK at 868.3 MHz with 50 kHz deviation should give better sensitivity.

## Tests

The protocol tests compile the decoder as host C++ and cover the captured normal
and secret frames plus inverted polarity, synthetic LOWBAT, repetition, and
parity cases:

    uv run pytest

Running the following additionally validates the external component's Python
schema and the complete ESPHome configuration:

    uv run esphome config honeywell-gateway.yaml

Every push and pull request runs these tests and compiles and cryptographically
verifies the hardware-Secure-Boot release firmware with an ephemeral CI key in
GitHub Actions. On `main`, semantic-release creates the version tag and GitHub
Release, then publishes only the public production OTA and factory firmware plus
the matching OTA manifest to GitHub Pages.
After creating the GitHub repository, select **GitHub Actions** as its Pages
source under **Settings > Pages**.
