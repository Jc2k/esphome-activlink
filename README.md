# ESPHome Honeywell ActivLink gateway

This project turns an ESP32-C6 and a CC1101 into a receive-only Home Assistant
gateway for European Honeywell/Friedland ActivLink transmitters.

Each configured bell push is represented as its own ESPHome sub-device in Home
Assistant, with:

- one doorbell event entity with **press** and **secret_press** event types;
- one **battery_low** binary sensor driven by the transmitted LOWBAT flag.

The initial configuration uses Wi-Fi and ESPHome's native API. A Thread variant
is included, but Wi-Fi is deliberately the bring-up path: serial/OTA logs are
much easier to use while the wiring and RF settings are still being verified.
Matter is not needed; ESPHome's native API already provides local delivery,
events, encryption, and sub-devices over either Wi-Fi or Thread.

## Important hardware check

The [linked Amazon listing](https://www.amazon.co.uk/dp/B09YV5M5Z3) is advertised
as a **433 MHz module**. The CC1101 silicon can tune to 868 MHz, but a module's
antenna and RF matching network are frequency-specific. It may work only at very
short range, or not at all, at 868 MHz. For dependable reception, use a CC1101
module sold for **868/915 MHz** with an 868 MHz antenna.

The radio is powered from **3.3 V only**. The common eight-pin CC1101 module
pinout is:

| Module pin | Signal | Required here |
| --- | --- | --- |
| 1 | GND | yes |
| 2 | 3.3 V | yes |
| 3 | GDO0 | yes, raw receive data |
| 4 | CSN / chip select | yes |
| 5 | SCK | yes |
| 6 | MOSI / MO | yes |
| 7 | MISO / MI | yes |
| 8 | GDO2 | no |

Your labels match an Adafruit ESP32-C6 Feather. On that board the connections
resolve to:

| CC1101 signal | Feather label | ESP32-C6 GPIO |
| --- | --- | --- |
| CSN | A3 | GPIO5 |
| SCK | SCK | GPIO21 |
| MOSI | MO | GPIO22 |
| MISO | MI | GPIO23 |
| GDO0 | SCL | GPIO18 |
| GDO2 (unused) | SDA | GPIO19 |

The checked-in Wi-Fi and Thread configurations use this mapping. It has been
verified on the Adafruit ESP32-C6 Feather: SPI reports CC1101 chip ID 0x0014 and
GDO0 receives the doorbell waveform.

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

## Set up with Wi-Fi

Requirements are pinned in uv.lock; do not install ESPHome globally.

    cp secrets.example.yaml secrets.yaml
    uv sync
    uv run esphome config honeywell-gateway.yaml
    uv run esphome run honeywell-gateway.yaml

Before the final command:

1. edit secrets.yaml;
2. confirm the board is an Adafruit ESP32-C6 Feather and CSN is on A3;
3. connect the board by USB.

The first flash should be over USB. Later runs can use ESPHome OTA.

### Managed firmware updates

Released firmware advertises an update entity in Home Assistant. The device
checks the ESPHome update manifest at
[unrouted.uk/esphome-activlink](https://unrouted.uk/esphome-activlink/) every six
hours and can install a newer release directly from there.

Public release binaries contain no Wi-Fi, API, or OTA secrets. The local build
saves its API encryption key and Wi-Fi credentials in flash; release builds load
those saved values after an OTA update. Flash the checked-in local configuration
at least once before using managed updates. After moving to a public release,
subsequent updates use the encrypted Home Assistant API and the HTTP OTA backend;
use USB if you need to return to a locally customized build.

Releases are built, versioned, and published automatically from semantic commit
messages after the host tests and the complete ESP32-C6 firmware build pass.
Use Conventional Commit prefixes such as `fix:`, `feat:`, and `feat!:` (or a
`BREAKING CHANGE:` footer) to request patch, minor, and major releases.
ESPHome is pinned exactly and updated daily by Dependabot; those updates use
`fix(deps):` and therefore produce a patch release. GitHub Actions are checked
weekly and use `ci(deps):`, so Actions-only updates do not produce a release.

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

1. copy the hex-encoded dataset TLV from Home Assistant's Thread integration to
   thread_tlv in secrets.yaml;
2. validate honeywell-gateway.thread.example.yaml;
3. flash it over USB when moving from Wi-Fi to Thread.

    uv run esphome config honeywell-gateway.thread.example.yaml
    uv run esphome run honeywell-gateway.thread.example.yaml

## RF troubleshooting

- “CC1101 found” with a nonzero chip ID proves SPI/CSN wiring, not RF reception.
- FF0F, 0000, or FFFF chip IDs normally indicate bad SPI, CSN, power, or ground
  wiring.
- An 868/915 MHz CC1101 module with an 868 MHz antenna remains the preferred
  hardware for dependable range.
- Keep module wiring short and add local supply decoupling if the breakout does
  not already have it.
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

Every push and pull request runs these tests and compiles the Wi-Fi firmware in
GitHub Actions. On `main`, semantic-release creates the version tag and GitHub
Release, then publishes the matching OTA manifest and binary to GitHub Pages.
After creating the GitHub repository, select **GitHub Actions** as its Pages
source under **Settings > Pages**.
