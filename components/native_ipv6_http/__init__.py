import esphome.codegen as cg
import esphome.config_validation as cv


CODEOWNERS = ["@jc2k"]
DEPENDENCIES = ["esp32", "http_request", "openthread"]

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    # Interpose on ESP-IDF's HTTP client constructor without copying or
    # forking ESPHome's full http_request component.
    cg.add_build_flag("-Wl,--wrap=esp_http_client_init")
