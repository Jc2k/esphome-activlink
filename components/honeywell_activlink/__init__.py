import base64

from esphome import codegen as cg
from esphome.components import api, binary_sensor, event, remote_base, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    ICON_TIMER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_SECOND,
)

AUTO_LOAD = ["binary_sensor", "event", "sensor"]
DEPENDENCIES = ["api", "remote_receiver"]

CONF_ACTIVLINK_ID = "activlink_id"
CONF_API_ENCRYPTION_KEY = "api_encryption_key"
CONF_BATTERY_LOW = "battery_low"
CONF_BUTTONS = "buttons"
CONF_DEDUPLICATION = "deduplication"
CONF_DECODE_FAILURES = "decode_failures"
CONF_DIAGNOSTIC_UPDATE_INTERVAL = "diagnostic_update_interval"
CONF_DUPLICATE_FRAMES = "duplicate_frames"
CONF_EVENT = "event"
CONF_LAST_VALID_FRAME_AGE = "last_valid_frame_age"
CONF_UNCONFIGURED_FRAMES = "unconfigured_frames"
CONF_VALID_FRAMES = "valid_frames"

activlink_ns = cg.esphome_ns.namespace("honeywell_activlink")
ActivLinkComponent = activlink_ns.class_(
    "ActivLinkComponent",
    cg.Component,
    remote_base.RemoteReceiverListener,
)


def _activlink_id(value):
    value = cv.hex_uint32_t(value)
    if value > 0xFFFFF:
        raise cv.Invalid("ActivLink IDs are 20-bit values (0x00000 to 0xFFFFF)")
    return value


BUTTON_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ACTIVLINK_ID): _activlink_id,
        cv.Required(CONF_EVENT): event.event_schema(device_class="doorbell"),
        cv.Required(CONF_BATTERY_LOW): binary_sensor.binary_sensor_schema(
            device_class="battery"
        ),
    }
)


def _counter_schema():
    return sensor.sensor_schema(
        icon=ICON_COUNTER,
        accuracy_decimals=0,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        state_class=STATE_CLASS_TOTAL_INCREASING,
    )


def _unique_button_ids(config):
    seen = set()
    for button in config[CONF_BUTTONS]:
        activlink_id = button[CONF_ACTIVLINK_ID]
        if activlink_id in seen:
            raise cv.Invalid(f"duplicate ActivLink ID 0x{activlink_id:05X}")
        seen.add(activlink_id)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ActivLinkComponent),
            cv.GenerateID(remote_base.CONF_RECEIVER_ID): cv.use_id(
                remote_base.RemoteReceiverBase
            ),
            cv.Required(CONF_BUTTONS): cv.All(
                cv.ensure_list(BUTTON_SCHEMA), cv.Length(min=1)
            ),
            cv.Optional(CONF_API_ENCRYPTION_KEY): cv.sensitive(
                api.validate_encryption_key
            ),
            cv.Optional(
                CONF_DEDUPLICATION, default="250ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_DIAGNOSTIC_UPDATE_INTERVAL, default="60s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_VALID_FRAMES): _counter_schema(),
            cv.Optional(CONF_DECODE_FAILURES): _counter_schema(),
            cv.Optional(CONF_DUPLICATE_FRAMES): _counter_schema(),
            cv.Optional(CONF_UNCONFIGURED_FRAMES): _counter_schema(),
            cv.Optional(CONF_LAST_VALID_FRAME_AGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_SECOND,
                icon=ICON_TIMER,
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _unique_button_ids,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await remote_base.register_listener(var, config)
    cg.add(var.set_deduplication(config[CONF_DEDUPLICATION]))
    cg.add(
        var.set_diagnostic_update_interval(
            config[CONF_DIAGNOSTIC_UPDATE_INTERVAL]
        )
    )
    if key := config.get(CONF_API_ENCRYPTION_KEY):
        cg.add(var.set_api_encryption_key(list(base64.b64decode(key))))

    for button in config[CONF_BUTTONS]:
        event_var = await event.new_event(
            button[CONF_EVENT], event_types=["press", "secret_press"]
        )
        battery_var = await binary_sensor.new_binary_sensor(button[CONF_BATTERY_LOW])
        cg.add(
            var.add_button(
                button[CONF_ACTIVLINK_ID],
                event_var,
                battery_var,
            )
        )

    diagnostic_sensors = {
        CONF_VALID_FRAMES: "set_valid_frames_sensor",
        CONF_DECODE_FAILURES: "set_decode_failures_sensor",
        CONF_DUPLICATE_FRAMES: "set_duplicate_frames_sensor",
        CONF_UNCONFIGURED_FRAMES: "set_unconfigured_frames_sensor",
        CONF_LAST_VALID_FRAME_AGE: "set_last_valid_frame_age_sensor",
    }
    for key, setter in diagnostic_sensors.items():
        if conf := config.get(key):
            diagnostic_sensor = await sensor.new_sensor(conf)
            cg.add(getattr(var, setter)(diagnostic_sensor))
