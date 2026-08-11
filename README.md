# ESPHome Honeywell ActivLink gateway

This project turns an ESP32-C6 and a CC1101 into a receive-only Home Assistant
gateway for European Honeywell/Friedland ActivLink transmitters.

Each paired bell push is represented as its own ESPHome sub-device in Home
Assistant, with:

- one doorbell event entity with **ring** and **secret_press** event types;
- one **battery_low** binary sensor driven by the transmitted LOWBAT flag;
- diagnostic entities showing whether the slot is paired and its transmitter
  ID.

Every gateway derives its hostname from the Feather's stable hardware MAC. For
example, a board whose MAC ends in `82:92:48` advertises as
`honeywell-activlink-829248.local` and appears as **Honeywell ActivLink Gateway
829248**. This keeps mDNS, the native API, and Home Assistant names stable and
collision-free when multiple gateways run the same firmware.

The primary configurations use 2.4 GHz Wi-Fi and ESPHome's native API. Wi-Fi
credentials are supplied at runtime with physically authorized BLE Improv and
are never compiled into firmware. Managed production devices use a guarded
local bootstrap before the irreversible security transition. Matter is not
needed.

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
deviation and 6250 symbols/s. A 325 kHz receive filter gives the CC1101 enough
frequency-compensation range for the roughly 65--73 kHz offset seen in
historical European captures. The M146 demodulates that signal natively and
outputs its asynchronous symbol stream on GDO0. A transmission repeats a 48-bit
PWM frame many times. The decoder:

1. accepts either electrical polarity from GDO0;
2. requires four matching frames, following rtl_433's integrity rule, while
   accumulating that consensus across adjacent ESP32 RMT capture chunks;
3. verifies even parity;
4. extracts the 20-bit transmitter ID, device type, secret-press, relay, and
   LOWBAT flags;
5. ignores non-doorbell ActivLink device types.

Because asynchronous reception remains in RX continuously, the component also
recognizes the CC1101's documented frequency-offset-loop collapse signature.
It only acts when the capture also contains correctly aligned ActivLink PWM
pairs, filtering RF noise that cannot possibly contain a complete row. It then
restarts RX, clearing the stale frequency estimate quickly enough to receive
later rows from the same repeated broadcast. The **ActivLink Radio Recoveries**
diagnostic counts these restarts.

This follows the published
[ActivLink frame analysis](https://github.com/klohner/honeywell-wireless-doorbell)
and the maintained
[rtl_433 decoder](https://github.com/merbanan/rtl_433/blob/master/src/devices/honeywell_wdb.c).
Physical captures from transmitter 0xFB100 confirm normal frame FB1000200001
and secret-press frame FB1000200010; both reported a healthy battery. The same
push also produces a relay-marked copy approximately 1.0--1.35 seconds later.
The component suppresses relay-marked copies with a dedicated four-second window while
retaining the shorter 250 ms repeat-frame filter, so genuine rapid presses are
not hidden. The old
433 MHz prototype used an ASK/OOK sideband workaround at 868.21 MHz; that is no
longer part of the checked-in M146 profile.

## Set up a Wi-Fi development device

Requirements are pinned in uv.lock; do not install ESPHome globally.

    cp secrets.example.yaml secrets.yaml
    uv sync
    uv run esphome config honeywell-gateway.yaml
    uv run esphome run honeywell-gateway.yaml

Before the final command:

1. generate a native API encryption key with `openssl rand -base64 32` and
   paste it as the quoted `api_encryption_key` value;
2. generate an independent bootstrap OTA password with
   `openssl rand -hex 32` and paste it as the quoted `ota_password` value;
3. confirm the board is an Adafruit ESP32-C6 Feather and CSN is on A3; and
4. connect the board by USB.

The first flash should be over USB. When the gateway advertises through BLE
Improv, briefly press **BOOT** to authorize provisioning, then send a 2.4 GHz
SSID and password from an Improv client. The credential is saved in encrypted
NVS, not in YAML or the firmware binary. This development profile still embeds
the API key, so do not distribute its binary. Use the managed workflow below
for a device that will be permanently secured.

### Managed firmware updates

Managed devices use a private local bootstrap to establish HMAC-backed encrypted
NVS and save the Home Assistant API key. BLE Improv supplies Wi-Fi credentials
directly to that encrypted NVS. A public production image then preserves NVS
and enables ESP32-C6 hardware Secure Boot V2. Public binaries contain neither
the Wi-Fi credential nor API key.

This is an irreversible, ordered procedure—not a generic ESPHome or Web Tools
flash. Follow the complete [secure Wi-Fi and BLE Improv provisioning
runbook](docs/secure-wifi-provisioning.md). Its guarded scripts verify the
credential-free public build, signing key, sdkconfig, bootloader/app signatures,
partition boundaries, eFuse state, and the pre-lock reboot checkpoint.

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
The release builder enforces version 1.1.0 as its minimum because the first
native-FSK production deployment already runs that version and ESPHome's
downgrade protection would reject an older release.
ESPHome is pinned exactly and updated daily by Dependabot; those updates use
`fix(deps):` and therefore produce a patch release. GitHub Actions are checked
weekly and use `ci(deps):`, so Actions-only updates do not produce a release.

The release workflow expects the private PEM key in the GitHub Actions secret
`FIRMWARE_SIGNING_KEY`. Generate the key once, keep an offline backup, and copy
the complete PEM file into that secret:

    uv run python -m espsecure generate-signing-key \
      --version 2 --scheme rsa3072 .firmware-signing-key.pem

The key file is ignored by Git. Losing or replacing it prevents Secure-Boot
devices from accepting any new production or recovery image; USB does not bypass
the hardware trust anchor. Already installed code keeps running. Keep an offline
backup.
Pull requests build and cryptographically verify a hardware-Secure-Boot release
with a throwaway key, while releases use only the configured Actions secret.

### Pair and remove buttons at runtime

No real transmitter IDs are compiled into the public firmware. The component
has capacity for eight stable Home Assistant sub-device slots, but advertises
only paired slots through the native API. Empty slots therefore do not appear
in Home Assistant. The runtime mapping is stored in ESPHome's NVS preferences.
Managed production firmware protects NVS with hardware-backed encryption.
Normal OTA updates and this project's guarded serial restore preserve that
partition.

Each slot also retains whether LOWBAT has ever been observed and its last value.
The battery entity therefore restores its last known state after either the
gateway or Home Assistant restarts. It remains unknown only until the first
valid transmission from a newly paired or legacy-migrated button. NVS is written
only when the battery state is first observed or changes.

To pair using the Feather, briefly press and release **BOOT**, then press the
ActivLink bell push within 30 seconds. The next valid doorbell ID is assigned to
the first empty slot. Rename that slot in Home Assistant, for example from
**ActivLink Button 1** to **Front Door**.

To remove one, hold **BOOT** for at least three seconds, release it, then press
the paired ActivLink bell push within 30 seconds. Removal is targeted by the RF
ID, so it cannot accidentally remove a different slot. Do not hold BOOT while
pressing RESET: GPIO9 is also the ESP32-C6 download-mode strap.

Home Assistant exposes equivalent **Pair next ActivLink button**, **Remove an
ActivLink button**, and **Cancel ActivLink pairing mode** controls. The
**Pairing state** entity reports the active mode, result, or timeout. Pairing
and removal consume the identifying RF press rather than emitting a doorbell
event.

In Home Assistant, trigger automations from the slot's **Press** event entity.
A normal push has event type **ring**, matching Home Assistant's standard
doorbell event, while the alternate ActivLink push has type **secret_press**.
The entity state is the time of the latest event rather than an on/off value.

Every ActivLink child device also exposes **Forget this button**. It immediately
removes that slot without requiring a transmission, which covers lost, broken,
or flat-battery transmitters. Forgetting clears the transmitter ID and retained
LOWBAT value, cancels any active pairing/removal mode, and makes the slot
available again. A successful pairing, removal, or forget operation briefly
restarts the gateway so Home Assistant repeats native API discovery. Current
Home Assistant releases then remove the unlinked slot's entities and child
device from their registries. The forget action has no confirmation step in
ESPHome.

The first upgrade from a version with YAML-configured transmitters starts with
an empty pairing table. Pair each existing transmitter once; all subsequent
firmware updates retain that table. Pairing is passive learning of ActivLink's
20-bit transmitter ID, not a two-way radio handshake.

With logs open, an unpaired button prints a message like:

    Unpaired ActivLink doorbell id=0x8DF50; start pairing and press it again

If no ActivLink line appears, temporarily add **dump: raw** under
**remote_receiver:** in packages/core.yaml. Raw timings around 160/320/480 µs
confirm that the CC1101 data path works. Frequent 60--100 µs fragments indicate
poor FSK discrimination; increasing the GPIO filter can merge real transitions
and is not a substitute for adequate CC1101 channel bandwidth. Remove the raw
dump after diagnosis because a long ActivLink broadcast produces a large log.

The ESP32-C6 receiver deliberately uses a 1,024-symbol RMT capture window with
a 40 KiB software queue. Increasing `receive_symbols` to 4,096 is
counterproductive here: ESPHome reserves the maximum capture size for every
queued item, leaving room for only two items and causing `Buffer overflow`
during the doorbell's repeated burst.

## Wi-Fi onboarding after RF bring-up

The production configuration intentionally contains an empty `wifi:` block and
BLE Improv. On a device without saved credentials, Improv advertises immediately.
If a saved network cannot be reached, it advertises after 90 seconds. Briefly
press **BOOT** to authorize a one-minute provisioning session, then send the new
2.4 GHz credentials. An unprovisioned window closes after ten minutes; reset or
power-cycle the gateway to reopen it.

No SSID or password belongs in `secrets.yaml`, repository settings, or GitHub
Actions. The release verifier rejects generated firmware if ESPHome adds a
configured Wi-Fi station. The old `honeywell-gateway.thread.example.yaml` remains
only as a legacy development example and is not used by production builds.

## Home Assistant diagnostics

Production profiles publish the no-extra-wiring diagnostics once per minute.
They include:

- ESP32-C6 uptime, die temperature, reset reason, heap health, and maximum
  component loop time;
- Wi-Fi signal strength, IP address, SSID, BSSID, MAC address, and DNS server;
- ActivLink valid frames, parity-valid candidate rows, rejected captures,
  duplicates, unconfigured IDs, and the age of the last valid decoded frame;
- a bounded timing summary of the most recent substantial capture that did not
  contain a parity-valid ActivLink row, for diagnosing radio-profile or timing
  mismatches without enabling the USB serial console.

The counters start again after a reboot. All of these entities use Home
Assistant's diagnostic category so they do not crowd the main device controls.
The public production profile keeps the physical USB Serial/JTAG log sink at
115200 baud so BLE Improv and Wi-Fi recovery remain observable when the
encrypted ESPHome API is offline. Opening the USB console can reset the Feather;
expect to see the complete boot and network onboarding sequence.
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
- The earlier 433 MHz-matched prototype required ASK/OOK reception on one FSK
  sideband at 868.21 MHz. The M146 profile instead uses native 2-FSK at
  868.3 MHz with 50 kHz deviation for better selectivity and sensitivity.

## Tests

The host C++ tests cover both the runtime pairing table and the protocol
decoder. Protocol vectors generate the documented three-symbol FSK stream (HLL
for 0 and HHL for 1) and cover the captured normal and secret frames plus
inverted polarity, synthetic LOWBAT, repeated-row consensus within and across
RMT captures, per-burst deduplication, and parity cases:

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
