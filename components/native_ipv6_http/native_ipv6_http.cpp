#include <esp_http_client.h>

extern "C" {

esp_http_client_handle_t __real_esp_http_client_init(const esp_http_client_config_t *config);

esp_http_client_handle_t __wrap_esp_http_client_init(const esp_http_client_config_t *config) {
  if (config == nullptr) {
    return __real_esp_http_client_init(config);
  }

  esp_http_client_config_t ipv6_config = *config;
  ipv6_config.addr_type = HTTP_ADDR_TYPE_INET6;
  return __real_esp_http_client_init(&ipv6_config);
}

}  // extern "C"
