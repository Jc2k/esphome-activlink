from esphome import codegen as cg
from esphome.components import binary_sensor, event, remote_base
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["binary_sensor", "event"]
DEPENDENCIES = ["remote_receiver"]

CONF_ACTIVLINK_ID = "activlink_id"
CONF_BATTERY_LOW = "battery_low"
CONF_BUTTONS = "buttons"
CONF_DEDUPLICATION = "deduplication"
CONF_EVENT = "event"

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
            cv.Optional(
                CONF_DEDUPLICATION, default="250ms"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _unique_button_ids,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await remote_base.register_listener(var, config)
    cg.add(var.set_deduplication(config[CONF_DEDUPLICATION]))

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

