import base64

from esphome import codegen as cg
from esphome.components import (
    api,
    binary_sensor,
    button,
    cc1101,
    event,
    remote_base,
    sensor,
    text_sensor,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    ICON_TIMER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_SECOND,
)

from .api_resource_patch import install_dynamic_device_info_patch

AUTO_LOAD = ["binary_sensor", "button", "event", "sensor", "text_sensor"]
DEPENDENCIES = ["api", "cc1101", "remote_receiver"]

CONF_API_ENCRYPTION_KEY = "api_encryption_key"
CONF_BATTERY_LOW = "battery_low"
CONF_CANCEL_BUTTON = "cancel_button"
CONF_CANDIDATE_ROWS = "candidate_rows"
CONF_DEDUPLICATION = "deduplication"
CONF_DECODE_FAILURES = "decode_failures"
CONF_DIAGNOSTIC_UPDATE_INTERVAL = "diagnostic_update_interval"
CONF_DUPLICATE_FRAMES = "duplicate_frames"
CONF_EVENT = "event"
CONF_FORGET_BUTTON = "forget_button"
CONF_LAST_VALID_FRAME_AGE = "last_valid_frame_age"
CONF_LAST_CAPTURE_SUMMARY = "last_capture_summary"
CONF_PAIR_BUTTON = "pair_button"
CONF_PAIRED = "paired"
CONF_PAIRED_COUNT = "paired_count"
CONF_PAIRING_STATE = "pairing_state"
CONF_PAIRING_TIMEOUT = "pairing_timeout"
CONF_REMOVE_BUTTON = "remove_button"
CONF_RADIO_ID = "radio_id"
CONF_RADIO_RECOVERIES = "radio_recoveries"
CONF_RELAY_DEDUPLICATION = "relay_deduplication"
CONF_SLOTS = "slots"
CONF_TRANSMITTER_ID = "transmitter_id"
CONF_UNCONFIGURED_FRAMES = "unconfigured_frames"
CONF_VALID_FRAMES = "valid_frames"
MAX_PAIRED_BUTTONS = 8

activlink_ns = cg.esphome_ns.namespace("honeywell_activlink")
ActivLinkComponent = activlink_ns.class_(
    "ActivLinkComponent",
    cg.Component,
    remote_base.RemoteReceiverListener,
)
ActivLinkPairButton = activlink_ns.class_("ActivLinkPairButton", button.Button)
ActivLinkRemoveButton = activlink_ns.class_("ActivLinkRemoveButton", button.Button)
ActivLinkCancelButton = activlink_ns.class_("ActivLinkCancelButton", button.Button)
ActivLinkForgetButton = activlink_ns.class_("ActivLinkForgetButton", button.Button)


SLOT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_EVENT): event.event_schema(device_class="doorbell"),
        cv.Required(CONF_BATTERY_LOW): binary_sensor.binary_sensor_schema(
            device_class="battery"
        ),
        cv.Required(CONF_PAIRED): binary_sensor.binary_sensor_schema(
            icon="mdi:link-variant",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Required(CONF_TRANSMITTER_ID): text_sensor.text_sensor_schema(
            icon="mdi:identifier",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Required(CONF_FORGET_BUTTON): button.button_schema(
            ActivLinkForgetButton,
            icon="mdi:delete-outline",
            entity_category=ENTITY_CATEGORY_CONFIG,
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


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ActivLinkComponent),
        cv.GenerateID(remote_base.CONF_RECEIVER_ID): cv.use_id(
            remote_base.RemoteReceiverBase
        ),
        cv.Required(CONF_RADIO_ID): cv.use_id(cc1101.CC1101Component),
        cv.Required(CONF_SLOTS): cv.All(
            cv.ensure_list(SLOT_SCHEMA),
            cv.Length(min=MAX_PAIRED_BUTTONS, max=MAX_PAIRED_BUTTONS),
        ),
        cv.Required(CONF_PAIR_BUTTON): button.button_schema(
            ActivLinkPairButton,
            icon="mdi:link-plus",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Required(CONF_REMOVE_BUTTON): button.button_schema(
            ActivLinkRemoveButton,
            icon="mdi:link-off",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Required(CONF_CANCEL_BUTTON): button.button_schema(
            ActivLinkCancelButton,
            icon="mdi:cancel",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Required(CONF_PAIRING_STATE): text_sensor.text_sensor_schema(
            icon="mdi:link-variant",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Required(CONF_PAIRED_COUNT): sensor.sensor_schema(
            icon=ICON_COUNTER,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_API_ENCRYPTION_KEY): cv.sensitive(
            api.validate_encryption_key
        ),
        cv.Optional(
            CONF_DEDUPLICATION, default="250ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_RELAY_DEDUPLICATION, default="4s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_PAIRING_TIMEOUT, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_DIAGNOSTIC_UPDATE_INTERVAL, default="60s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_VALID_FRAMES): _counter_schema(),
        cv.Optional(CONF_DECODE_FAILURES): _counter_schema(),
        cv.Optional(CONF_CANDIDATE_ROWS): _counter_schema(),
        cv.Optional(CONF_DUPLICATE_FRAMES): _counter_schema(),
        cv.Optional(CONF_UNCONFIGURED_FRAMES): _counter_schema(),
        cv.Optional(CONF_RADIO_RECOVERIES): _counter_schema(),
        cv.Optional(CONF_LAST_VALID_FRAME_AGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            icon=ICON_TIMER,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_LAST_CAPTURE_SUMMARY): text_sensor.text_sensor_schema(
            icon="mdi:pulse",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    install_dynamic_device_info_patch()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await remote_base.register_listener(var, config)
    radio = await cg.get_variable(config[CONF_RADIO_ID])
    cg.add(var.set_radio(radio))
    cg.add(var.set_deduplication(config[CONF_DEDUPLICATION]))
    cg.add(var.set_relay_deduplication(config[CONF_RELAY_DEDUPLICATION]))
    cg.add(var.set_pairing_timeout(config[CONF_PAIRING_TIMEOUT]))
    cg.add(
        var.set_diagnostic_update_interval(
            config[CONF_DIAGNOSTIC_UPDATE_INTERVAL]
        )
    )
    if key := config.get(CONF_API_ENCRYPTION_KEY):
        cg.add(var.set_api_encryption_key(list(base64.b64decode(key))))

    for slot_index, slot in enumerate(config[CONF_SLOTS]):
        event_var = await event.new_event(
            slot[CONF_EVENT], event_types=["ring", "secret_press"]
        )
        battery_var = await binary_sensor.new_binary_sensor(slot[CONF_BATTERY_LOW])
        paired_var = await binary_sensor.new_binary_sensor(slot[CONF_PAIRED])
        transmitter_var = await text_sensor.new_text_sensor(slot[CONF_TRANSMITTER_ID])
        forget_var = await button.new_button(slot[CONF_FORGET_BUTTON])
        cg.add(forget_var.set_parent(var))
        cg.add(forget_var.set_slot(slot_index))
        cg.add(
            var.add_slot(
                event_var,
                battery_var,
                paired_var,
                transmitter_var,
                forget_var,
            )
        )

    management_buttons = (
        CONF_PAIR_BUTTON,
        CONF_REMOVE_BUTTON,
        CONF_CANCEL_BUTTON,
    )
    for key in management_buttons:
        management_var = await button.new_button(config[key])
        cg.add(management_var.set_parent(var))

    pairing_state = await text_sensor.new_text_sensor(config[CONF_PAIRING_STATE])
    cg.add(var.set_pairing_state_text_sensor(pairing_state))
    paired_count = await sensor.new_sensor(config[CONF_PAIRED_COUNT])
    cg.add(var.set_paired_count_sensor(paired_count))

    diagnostic_sensors = {
        CONF_VALID_FRAMES: "set_valid_frames_sensor",
        CONF_DECODE_FAILURES: "set_decode_failures_sensor",
        CONF_CANDIDATE_ROWS: "set_candidate_rows_sensor",
        CONF_DUPLICATE_FRAMES: "set_duplicate_frames_sensor",
        CONF_UNCONFIGURED_FRAMES: "set_unconfigured_frames_sensor",
        CONF_RADIO_RECOVERIES: "set_radio_recoveries_sensor",
        CONF_LAST_VALID_FRAME_AGE: "set_last_valid_frame_age_sensor",
    }
    for key, setter in diagnostic_sensors.items():
        if conf := config.get(key):
            diagnostic_sensor = await sensor.new_sensor(conf)
            cg.add(getattr(var, setter)(diagnostic_sensor))

    if conf := config.get(CONF_LAST_CAPTURE_SUMMARY):
        diagnostic_text_sensor = await text_sensor.new_text_sensor(conf)
        cg.add(var.set_last_capture_summary_text_sensor(diagnostic_text_sensor))
