import esphome.codegen as cg
from esphome.components import binary_sensor, sensor, text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_COUNTER,
    ICON_TIMER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_SECOND,
)

AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]
DEPENDENCIES = ["openthread"]

CONF_ATTACHED = "attached"
CONF_ATTACH_ATTEMPTS = "attach_attempts"
CONF_ATTACH_DURATION = "attach_duration"
CONF_CHANNEL = "channel"
CONF_IPV6_RX_FAILURES = "ipv6_rx_failures"
CONF_IPV6_TX_FAILURES = "ipv6_tx_failures"
CONF_MAC_CCA_FAILURES = "mac_cca_failures"
CONF_MAC_RX_FCS_ERRORS = "mac_rx_fcs_errors"
CONF_MAC_TX_RETRIES = "mac_tx_retries"
CONF_PARENT_AVERAGE_RSSI = "parent_average_rssi"
CONF_PARENT_CHANGES = "parent_changes"
CONF_PARENT_LINK_QUALITY = "parent_link_quality"
CONF_PARENT_RLOC16 = "parent_rloc16"
CONF_PARTITION_ID = "partition_id"
CONF_RLOC16 = "rloc16"
CONF_ROLE = "role"
CONF_ROUTER_NEIGHBORS = "router_neighbors"
CONF_ROUTER_NEIGHBOR_BEST_RSSI = "router_neighbor_best_rssi"
CONF_ROUTER_NEIGHBOR_COUNT = "router_neighbor_count"
CONF_ROUTER_NEIGHBOR_MIN_LINK_QUALITY = "router_neighbor_min_link_quality"
CONF_ROUTER_NEIGHBOR_WORST_RSSI = "router_neighbor_worst_rssi"
CONF_ROUTER_TABLE = "router_table"
CONF_KNOWN_ROUTER_COUNT = "known_router_count"
CONF_REACHABLE_ROUTER_COUNT = "reachable_router_count"
CONF_SRP_CLIENT_RUNNING = "srp_client_running"
CONF_SRP_HOST_STATE = "srp_host_state"

thread_diagnostics_ns = cg.esphome_ns.namespace("thread_diagnostics")
ThreadDiagnostics = thread_diagnostics_ns.class_(
    "ThreadDiagnostics", cg.PollingComponent
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
        cv.GenerateID(): cv.declare_id(ThreadDiagnostics),
        cv.Optional(CONF_ATTACHED): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ROLE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RLOC16): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PARENT_RLOC16): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_PARTITION_ID): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_SRP_CLIENT_RUNNING): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_SRP_HOST_STATE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ROUTER_NEIGHBORS): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ROUTER_TABLE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ATTACH_DURATION): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            icon=ICON_TIMER,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DURATION,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PARENT_AVERAGE_RSSI): sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            icon="mdi:signal",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PARENT_LINK_QUALITY): sensor.sensor_schema(
            icon="mdi:signal",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CHANNEL): sensor.sensor_schema(
            icon="mdi:access-point-network",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PARENT_CHANGES): _counter_schema(),
        cv.Optional(CONF_ATTACH_ATTEMPTS): _counter_schema(),
        cv.Optional(CONF_IPV6_TX_FAILURES): _counter_schema(),
        cv.Optional(CONF_IPV6_RX_FAILURES): _counter_schema(),
        cv.Optional(CONF_MAC_TX_RETRIES): _counter_schema(),
        cv.Optional(CONF_MAC_CCA_FAILURES): _counter_schema(),
        cv.Optional(CONF_MAC_RX_FCS_ERRORS): _counter_schema(),
        cv.Optional(CONF_ROUTER_NEIGHBOR_COUNT): sensor.sensor_schema(
            icon="mdi:access-point-network",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ROUTER_NEIGHBOR_BEST_RSSI): sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            icon="mdi:signal",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ROUTER_NEIGHBOR_WORST_RSSI): sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            icon="mdi:signal",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_ROUTER_NEIGHBOR_MIN_LINK_QUALITY): sensor.sensor_schema(
            icon="mdi:signal",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_KNOWN_ROUTER_COUNT): sensor.sensor_schema(
            icon="mdi:router-network",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_REACHABLE_ROUTER_COUNT): sensor.sensor_schema(
            icon="mdi:router-network",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    binary_sensors = {
        CONF_ATTACHED: "set_attached_binary_sensor",
        CONF_SRP_CLIENT_RUNNING: "set_srp_client_running_binary_sensor",
    }
    for key, setter in binary_sensors.items():
        if conf := config.get(key):
            entity = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(var, setter)(entity))

    text_sensors = {
        CONF_ROLE: "set_role_text_sensor",
        CONF_RLOC16: "set_rloc16_text_sensor",
        CONF_PARENT_RLOC16: "set_parent_rloc16_text_sensor",
        CONF_PARTITION_ID: "set_partition_id_text_sensor",
        CONF_SRP_HOST_STATE: "set_srp_host_state_text_sensor",
        CONF_ROUTER_NEIGHBORS: "set_router_neighbors_text_sensor",
        CONF_ROUTER_TABLE: "set_router_table_text_sensor",
    }
    for key, setter in text_sensors.items():
        if conf := config.get(key):
            entity = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(var, setter)(entity))

    sensors = {
        CONF_ATTACH_DURATION: "set_attach_duration_sensor",
        CONF_PARENT_AVERAGE_RSSI: "set_parent_average_rssi_sensor",
        CONF_PARENT_LINK_QUALITY: "set_parent_link_quality_sensor",
        CONF_CHANNEL: "set_channel_sensor",
        CONF_PARENT_CHANGES: "set_parent_changes_sensor",
        CONF_ATTACH_ATTEMPTS: "set_attach_attempts_sensor",
        CONF_IPV6_TX_FAILURES: "set_ipv6_tx_failures_sensor",
        CONF_IPV6_RX_FAILURES: "set_ipv6_rx_failures_sensor",
        CONF_MAC_TX_RETRIES: "set_mac_tx_retries_sensor",
        CONF_MAC_CCA_FAILURES: "set_mac_cca_failures_sensor",
        CONF_MAC_RX_FCS_ERRORS: "set_mac_rx_fcs_errors_sensor",
        CONF_ROUTER_NEIGHBOR_COUNT: "set_router_neighbor_count_sensor",
        CONF_ROUTER_NEIGHBOR_BEST_RSSI: "set_router_neighbor_best_rssi_sensor",
        CONF_ROUTER_NEIGHBOR_WORST_RSSI: "set_router_neighbor_worst_rssi_sensor",
        CONF_ROUTER_NEIGHBOR_MIN_LINK_QUALITY: "set_router_neighbor_min_link_quality_sensor",
        CONF_KNOWN_ROUTER_COUNT: "set_known_router_count_sensor",
        CONF_REACHABLE_ROUTER_COUNT: "set_reachable_router_count_sensor",
    }
    for key, setter in sensors.items():
        if conf := config.get(key):
            entity = await sensor.new_sensor(conf)
            cg.add(getattr(var, setter)(entity))
