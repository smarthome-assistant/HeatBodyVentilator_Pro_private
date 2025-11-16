#include "ServerManager.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "LEDManager.h"
#include "ArduinoJson.h"
#include <string.h>
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SERVER";

// Macro for token validation in protected handlers
#define REQUIRE_AUTH() \
    do { \
        if (!check_auth(req)) { \
            ESP_LOGW(TAG, "Unauthorized API access to %s", req->uri); \
            httpd_resp_set_status(req, "401 Unauthorized"); \
            send_json_response(req, "{\"error\":\"Unauthorized - invalid or missing token\"}"); \
            return ESP_OK; \
        } \
    } while(0)

extern LEDManager led;
extern MQTTManager mqttManager;
extern WiFiManager wifi;

static ServerManager* serverInstance = nullptr;

ServerManager::ServerManager() : server(nullptr), ledManager(nullptr), ledState(false), 
                                 ledColorR(255), ledColorG(255), ledColorB(255) {
    serverInstance = this;
}

void ServerManager::begin() {
    // SPIFFS initialisieren
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        ret = esp_spiffs_info(NULL, &total, &used);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS: Total: %d, Used: %d", total, used);
        }
    }
    
    initializePWM();
    
    ESP_LOGI(TAG, "Initializing KMeter-ISO sensor...");
    if (kmeterManager.begin(0x66, 26, 32)) {
        ESP_LOGI(TAG, "KMeter-ISO sensor initialized successfully");
    } else {
        ESP_LOGW(TAG, "KMeter-ISO sensor initialization failed, continuing without sensor");
    }
    
    Config::saveAutoPWMEnabled(true);
    ESP_LOGI(TAG, "Auto-PWM enabled for fan control");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = Config::HTTP_PORT;
    config.max_uri_handlers = 50;  // Increased to accommodate all routes
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;  // Enable wildcard/query string matching
    config.lru_purge_enable = true;  // Enable connection purging
    config.recv_wait_timeout = 10;   // Timeout for receiving data
    config.send_wait_timeout = 10;   // Timeout for sending data
    
    if (httpd_start(&server, &config) == ESP_OK) {
        setupRoutes();
        ESP_LOGI(TAG, "All routes registered");
        ESP_LOGI(TAG, "Web server started on port %d", Config::HTTP_PORT);
        ESP_LOGI(TAG, "Server listening on all network interfaces (0.0.0.0:%d)", Config::HTTP_PORT);
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
    }
}

void ServerManager::restart() {
    ESP_LOGI(TAG, "Restarting HTTP server to bind to new network interface...");
    
    // Stop existing server
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    
    // Wait a moment for cleanup
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Start server again
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = Config::HTTP_PORT;
    config.max_uri_handlers = 50;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        setupRoutes();
        ESP_LOGI(TAG, "HTTP server restarted successfully");
        ESP_LOGI(TAG, "Server now accessible on all network interfaces");
    } else {
        ESP_LOGE(TAG, "Failed to restart HTTP server");
    }
}

void ServerManager::setupRoutes() {
    // Static files
    httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &root_uri);
    
    httpd_uri_t login_uri = {.uri = "/login.html", .method = HTTP_GET, .handler = login_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &login_uri);
    
    httpd_uri_t main_uri = {.uri = "/main.html", .method = HTTP_GET, .handler = main_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &main_uri);
    
    // Alias for main page without .html extension
    httpd_uri_t main_alias_uri = {.uri = "/main", .method = HTTP_GET, .handler = main_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &main_alias_uri);
    
    httpd_uri_t style_uri = {.uri = "/style.css", .method = HTTP_GET, .handler = style_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &style_uri);
    
    httpd_uri_t logo_uri = {.uri = "/img/logo.png", .method = HTTP_GET, .handler = logo_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &logo_uri);
    
    httpd_uri_t setting_uri = {.uri = "/setting.html", .method = HTTP_GET, .handler = setting_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &setting_uri);
    
    // Alias for setting page without .html extension
    httpd_uri_t setting_alias_uri = {.uri = "/setting", .method = HTTP_GET, .handler = setting_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &setting_alias_uri);
    
    // API endpoints
    httpd_uri_t api_status = {.uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_status);
    
    httpd_uri_t api_device_name = {.uri = "/api/device-name", .method = HTTP_POST, .handler = api_device_name_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_device_name);
    
    httpd_uri_t api_reboot = {.uri = "/api/reboot", .method = HTTP_POST, .handler = api_reboot_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_reboot);
    
    httpd_uri_t api_wifi_status = {.uri = "/api/wifi/status", .method = HTTP_GET, .handler = api_wifi_status_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_wifi_status);
    
    // Alias for compatibility with HTML files
    httpd_uri_t api_wifi_status_alt = {.uri = "/api/wifi-status", .method = HTTP_GET, .handler = api_wifi_status_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_wifi_status_alt);
    
    httpd_uri_t api_pwm_status = {.uri = "/api/pwm/status", .method = HTTP_GET, .handler = api_pwm_status_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_pwm_status);
    
    // Alias for compatibility with HTML files
    httpd_uri_t api_pwm_status_alt = {.uri = "/api/pwm-status", .method = HTTP_GET, .handler = api_pwm_status_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_pwm_status_alt);
    
    httpd_uri_t api_pwm_control = {.uri = "/api/pwm/control", .method = HTTP_POST, .handler = api_pwm_control_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_pwm_control);
    
    // OTA Update - TAR file (recommended) or separate files
    httpd_uri_t api_ota_tar = {.uri = "/api/ota/upload", .method = HTTP_POST, .handler = api_ota_tar_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_ota_tar);
    
    httpd_uri_t api_ota_firmware = {.uri = "/api/ota/firmware", .method = HTTP_POST, .handler = api_ota_firmware_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_ota_firmware);
    
    httpd_uri_t api_ota_filesystem = {.uri = "/api/ota/filesystem", .method = HTTP_POST, .handler = api_ota_filesystem_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_ota_filesystem);
    
    httpd_uri_t api_login = {.uri = "/api/login", .method = HTTP_POST, .handler = api_login_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_login);
    
    // POST handler for login form
    httpd_uri_t login_post = {.uri = "/login", .method = HTTP_POST, .handler = login_post_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &login_post);
    
    // System & Device APIs
    httpd_uri_t api_system_info = {.uri = "/api/system-info", .method = HTTP_GET, .handler = api_system_info_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_system_info);
    
    httpd_uri_t api_settings = {.uri = "/api/settings", .method = HTTP_GET, .handler = api_settings_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_settings);
    
    httpd_uri_t api_restart = {.uri = "/api/restart", .method = HTTP_POST, .handler = api_restart_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_restart);
    
    httpd_uri_t api_factory_reset = {.uri = "/api/factory-reset", .method = HTTP_POST, .handler = api_factory_reset_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_factory_reset);
    
    // WiFi APIs
    httpd_uri_t api_wifi_scan = {.uri = "/api/wifi-scan*", .method = HTTP_GET, .handler = api_wifi_scan_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_wifi_scan);
    
    httpd_uri_t wifi_connect = {.uri = "/wifi_connect", .method = HTTP_POST, .handler = api_wifi_connect_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &wifi_connect);
    
    httpd_uri_t api_wifi_disconnect = {.uri = "/api/wifi-disconnect", .method = HTTP_POST, .handler = api_wifi_disconnect_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_wifi_disconnect);
    
    httpd_uri_t api_wifi_clear = {.uri = "/api/wifi-clear", .method = HTTP_POST, .handler = api_wifi_clear_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_wifi_clear);
    
    httpd_uri_t api_toggle_ap = {.uri = "/api/toggle-ap", .method = HTTP_POST, .handler = api_toggle_ap_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_toggle_ap);
    
    // MQTT APIs
    httpd_uri_t api_mqtt_settings = {.uri = "/api/mqtt-settings", .method = HTTP_POST, .handler = api_mqtt_settings_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_mqtt_settings);
    
    httpd_uri_t api_mqtt_test = {.uri = "/api/mqtt-test", .method = HTTP_POST, .handler = api_mqtt_test_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_mqtt_test);
    
    // PWM & Sensor APIs
    httpd_uri_t api_manual_pwm_mode = {.uri = "/api/manual-pwm-mode", .method = HTTP_POST, .handler = api_manual_pwm_mode_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_manual_pwm_mode);
    
    httpd_uri_t api_manual_pwm_settings = {.uri = "/api/manual-pwm-settings", .method = HTTP_POST, .handler = api_manual_pwm_settings_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_manual_pwm_settings);
    
    httpd_uri_t api_temp_mapping = {.uri = "/api/temp-mapping", .method = HTTP_POST, .handler = api_temp_mapping_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_temp_mapping);
    
    httpd_uri_t api_temp_mapping_status = {.uri = "/api/temp-mapping-status*", .method = HTTP_GET, .handler = api_temp_mapping_status_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_temp_mapping_status);
    
    httpd_uri_t api_kmeter_status = {.uri = "/api/kmeter-status*", .method = HTTP_GET, .handler = api_kmeter_status_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_kmeter_status);
    
    httpd_uri_t api_kmeter_config = {.uri = "/api/kmeter-config", .method = HTTP_POST, .handler = api_kmeter_config_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_kmeter_config);
    
    // LED & Auth APIs
    httpd_uri_t api_led_toggle = {.uri = "/api/led-toggle", .method = HTTP_POST, .handler = api_led_toggle_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_led_toggle);
    
    httpd_uri_t api_led_color = {.uri = "/api/led-color", .method = HTTP_POST, .handler = api_led_color_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_led_color);
    
    httpd_uri_t api_change_password = {.uri = "/api/change-password", .method = HTTP_POST, .handler = api_change_password_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_change_password);
    
    httpd_uri_t api_logout = {.uri = "/api/logout", .method = HTTP_POST, .handler = api_logout_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_logout);
    
    // Also support GET for logout
    httpd_uri_t api_logout_get = {.uri = "/api/logout", .method = HTTP_GET, .handler = api_logout_handler, .user_ctx = this};
    httpd_register_uri_handler(server, &api_logout_get);
    
    ESP_LOGI(TAG, "All routes registered");
}

void ServerManager::handleClient() {
    // Empty for ESP-IDF - HTTP server is async
}

// Helper function to serve files from SPIFFS
static esp_err_t serve_spiffs_file(httpd_req_t *req, const char* filepath, const char* content_type) {
    FILE* f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, content_type);
    
    // Check if this is an HTML file that needs placeholder replacement
    bool needsReplacement = (strstr(content_type, "text/html") != NULL);
    
    if (needsReplacement) {
        // Process file in streaming mode with on-the-fly replacement
        const size_t CHUNK_SIZE = 512;
        char buffer[CHUNK_SIZE];
        char output[CHUNK_SIZE + 64]; // Extra space for device name
        const char* placeholder = "%DEVICE_NAME%";
        const size_t placeholder_len = strlen(placeholder);
        const size_t device_name_len = strlen(Config::DEVICE_NAME);
        
        char carry_over[64] = {0}; // Buffer for partial placeholder matches across chunks
        size_t carry_len = 0;
        
        while (!feof(f)) {
            size_t read_bytes = fread(buffer, 1, CHUNK_SIZE, f);
            if (read_bytes == 0) break;
            
            // Combine carry-over from previous chunk with current chunk
            char combined[CHUNK_SIZE + 64];
            size_t combined_len = 0;
            
            if (carry_len > 0) {
                memcpy(combined, carry_over, carry_len);
                combined_len = carry_len;
                carry_len = 0;
            }
            
            memcpy(combined + combined_len, buffer, read_bytes);
            combined_len += read_bytes;
            
            // Process combined buffer and look for placeholder
            size_t out_len = 0;
            size_t i = 0;
            
            while (i < combined_len) {
                // Check if we might have a partial match at the end
                if (i + placeholder_len > combined_len && !feof(f)) {
                    // Save remainder for next iteration
                    carry_len = combined_len - i;
                    memcpy(carry_over, combined + i, carry_len);
                    break;
                }
                
                // Check for placeholder
                if (i + placeholder_len <= combined_len && 
                    strncmp(combined + i, placeholder, placeholder_len) == 0) {
                    // Replace with device name
                    memcpy(output + out_len, Config::DEVICE_NAME, device_name_len);
                    out_len += device_name_len;
                    i += placeholder_len;
                } else {
                    output[out_len++] = combined[i++];
                }
            }
            
            // Send this chunk
            if (out_len > 0) {
                if (httpd_resp_send_chunk(req, output, out_len) != ESP_OK) {
                    fclose(f);
                    return ESP_FAIL;
                }
            }
        }
        
        // Send any remaining carry-over
        if (carry_len > 0) {
            httpd_resp_send_chunk(req, carry_over, carry_len);
        }
        
        fclose(f);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    } else {
        // Send non-HTML files as-is
        char buffer[512];
        size_t read_bytes;
        while ((read_bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
            if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
                fclose(f);
                return ESP_FAIL;
            }
        }
        
        fclose(f);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
}

void ServerManager::initializePWM() {
    ESP_LOGI(TAG, "Initializing PWM system...");
    
    uint32_t freq = Config::MANUAL_PWM_MODE ? Config::MANUAL_PWM_FREQ : 1000;
    
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = freq,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };
    
    if (ledc_timer_config(&ledc_timer) == ESP_OK) {
        ESP_LOGI(TAG, "PWM Timer configured with %lu Hz", freq);
        
        ledc_channel_config_t ledc_channel = {
            .gpio_num = 22,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
            .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
            .flags = {
                .output_invert = 0
            }
        };
        
        if (ledc_channel_config(&ledc_channel) == ESP_OK) {
            ESP_LOGI(TAG, "PWM Channel configured - Pin 22 attached");
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            ESP_LOGI(TAG, "PWM initialized - Pin 22 set to 0%%");
        } else {
            ESP_LOGE(TAG, "PWM Channel config failed");
        }
    } else {
        ESP_LOGE(TAG, "PWM Timer config failed");
    }
}

void ServerManager::reconfigurePWM(uint32_t frequency) {
    ESP_LOGI(TAG, "Reconfiguring PWM to %lu Hz...", frequency);
    
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = frequency,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };
    
    if (ledc_timer_config(&ledc_timer) == ESP_OK) {
        ESP_LOGI(TAG, "PWM reconfigured to %lu Hz", frequency);
        
        if (Config::MANUAL_PWM_MODE) {
            int duty = (Config::MANUAL_PWM_DUTY * 255) / 100;
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            ESP_LOGI(TAG, "PWM duty restored to %d%%", Config::MANUAL_PWM_DUTY);
        }
    } else {
        ESP_LOGE(TAG, "PWM reconfiguration failed");
    }
}

void ServerManager::setPWMDuty(int duty) {
    if (duty < 0 || duty > 255) {
        ESP_LOGE(TAG, "Invalid PWM duty cycle: %d (must be 0-255)", duty);
        return;
    }
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void ServerManager::updateSensors() {
    kmeterManager.update();
}

int ServerManager::mapTemperatureToPWM(float temperature) {
    if (temperature <= Config::TEMP_START) {
        return 0;
    } else if (temperature >= Config::TEMP_MAX) {
        return 255;
    } else {
        float range = Config::TEMP_MAX - Config::TEMP_START;
        float temp_above_start = temperature - Config::TEMP_START;
        return (int)((temp_above_start / range) * 255.0);
    }
}

void ServerManager::updateAutoPWM() {
    if (!Config::AUTO_PWM_ENABLED || Config::MANUAL_PWM_MODE) {
        return;
    }
    
    float temp = kmeterManager.getTemperatureCelsius();
    if (temp > 0) {
        int pwm = mapTemperatureToPWM(temp);
        setPWMDuty(pwm);
    }
}

// Static HTTP Handlers
void ServerManager::send_json_response(httpd_req_t *req, const char* json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
}

void ServerManager::url_decode(char* dst, const char* src, size_t dst_size) {
    size_t src_len = strlen(src);
    size_t dst_index = 0;
    
    for (size_t i = 0; i < src_len && dst_index < dst_size - 1; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            // Hex decode
            char hex[3] = {src[i+1], src[i+2], '\0'};
            char* endptr;
            long value = strtol(hex, &endptr, 16);
            if (*endptr == '\0') {
                dst[dst_index++] = (char)value;
                i += 2; // Skip the two hex digits
            } else {
                dst[dst_index++] = src[i]; // Invalid hex, copy as-is
            }
        } else if (src[i] == '+') {
            dst[dst_index++] = ' '; // Convert + to space
        } else {
            dst[dst_index++] = src[i];
        }
    }
    dst[dst_index] = '\0';
}

bool ServerManager::check_auth(httpd_req_t *req) {
    // Check for token in URL parameter
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char token[32] = {0};
        if (httpd_query_key_value(query, "token", token, sizeof(token)) == ESP_OK) {
            return is_valid_token(token);
        }
    }
    return false; // Not authenticated
}

// Token storage (simple in-memory storage, will be reset on reboot)
static char current_token[9] = {0};
static uint64_t token_created = 0;
static const uint64_t TOKEN_EXPIRY = 24ULL * 60ULL * 60ULL * 1000000ULL; // 24 hours in microseconds

char* ServerManager::generate_token() {
    // Generate a simple 4-character hex token (like "83f8" in your screenshot)
    uint32_t random_num = esp_random() & 0xFFFF; // 16-bit random number
    snprintf(current_token, sizeof(current_token), "%04lx", (unsigned long)random_num);
    token_created = esp_timer_get_time();
    ESP_LOGI(TAG, "Generated new token: %s", current_token);
    return current_token;
}

bool ServerManager::is_valid_token(const char* token) {
    if (!token || strlen(token) == 0) {
        return false;
    }
    
    // Check if token matches
    if (strcmp(token, current_token) != 0) {
        ESP_LOGI(TAG, "Invalid token: %s (expected: %s)", token, current_token);
        return false;
    }
    
    // Check if token is expired (24 hours)
    uint64_t current_time = esp_timer_get_time();
    if (current_time - token_created > TOKEN_EXPIRY) {
        ESP_LOGI(TAG, "Token expired");
        return false;
    }
    
    return true;
}

bool ServerManager::check_token(httpd_req_t *req) {
    return check_auth(req);
}

esp_err_t ServerManager::root_handler(httpd_req_t *req) {
    // Check for token in URL
    if (!check_auth(req)) {
        // No valid token - show login page
        return serve_spiffs_file(req, "/spiffs/login.html", "text/html");
    }
    // Valid token - show main page
    return serve_spiffs_file(req, "/spiffs/main.html", "text/html");
}

esp_err_t ServerManager::login_handler(httpd_req_t *req) {
    return serve_spiffs_file(req, "/spiffs/login.html", "text/html");
}

esp_err_t ServerManager::main_handler(httpd_req_t *req) {
    // Require valid token to access main page
    if (!check_auth(req)) {
        ESP_LOGW(TAG, "Unauthorized access to main.html - redirecting to login");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    return serve_spiffs_file(req, "/spiffs/main.html", "text/html");
}

esp_err_t ServerManager::setting_handler(httpd_req_t *req) {
    // Require valid token to access settings page
    if (!check_auth(req)) {
        ESP_LOGW(TAG, "Unauthorized access to setting.html - redirecting to login");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    return serve_spiffs_file(req, "/spiffs/setting.html", "text/html");
}

esp_err_t ServerManager::style_handler(httpd_req_t *req) {
    return serve_spiffs_file(req, "/spiffs/style.css", "text/css");
}

esp_err_t ServerManager::logo_handler(httpd_req_t *req) {
    return serve_spiffs_file(req, "/spiffs/img/logo.png", "image/png");
}

esp_err_t ServerManager::api_status_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    DynamicJsonDocument doc(768);
    doc["status"] = "online";
    doc["device_name"] = Config::DEVICE_NAME;
    doc["firmware"] = Config::FIRMWARE_VERSION;
    doc["wifi_connected"] = wifi.isConnected();
    doc["mqtt_connected"] = mqttManager.isConnected();
    
    // Get WiFi RSSI
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        doc["wifi_rssi"] = ap_info.rssi;
    } else {
        doc["wifi_rssi"] = 0;
    }
    
    // Uptime in human readable format
    uint64_t uptime_sec = esp_timer_get_time() / 1000000;
    uint32_t days = uptime_sec / 86400;
    uint32_t hours = (uptime_sec % 86400) / 3600;
    uint32_t minutes = (uptime_sec % 3600) / 60;
    char uptime_str[64];
    if (days > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%lud %luh %lum", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%luh %lum", hours, minutes);
    } else {
        snprintf(uptime_str, sizeof(uptime_str), "%lum", minutes);
    }
    doc["uptime"] = uptime_str;
    
    // Memory info
    doc["free_memory"] = esp_get_free_heap_size() / 1024; // KB
    
    // LED state from ServerManager
    ServerManager* manager = (ServerManager*)req->user_ctx;
    doc["led_state"] = manager->ledState;
    doc["led_r"] = manager->ledColorR;
    doc["led_g"] = manager->ledColorG;
    doc["led_b"] = manager->ledColorB;
    
    char json[768];
    serializeJson(doc, json, sizeof(json));
    send_json_response(req, json);
    return ESP_OK;
}

esp_err_t ServerManager::api_device_name_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    char name[64] = {0};
    
    // Parse form data (name=xxx)
    if (strstr(buf, "name=")) {
        char *name_start = strstr(buf, "name=") + 5;
        char *name_end = strchr(name_start, '&');
        int len = name_end ? (name_end - name_start) : strlen(name_start);
        if (len > 0 && len < sizeof(name)) {
            char temp[64];
            strncpy(temp, name_start, len);
            temp[len] = '\0';
            // URL decode name (e.g., %23 -> #)
            url_decode(name, temp, sizeof(name));
        }
    } else {
        // Parse JSON
        DynamicJsonDocument doc(128);
        deserializeJson(doc, buf);
        
        const char* newName = doc["name"];
        if (newName) {
            strncpy(name, newName, sizeof(name) - 1);
        }
    }
    
    if (strlen(name) > 0) {
        Config::saveDeviceName(name);
        send_json_response(req, "{\"success\":true}");
    } else {
        send_json_response(req, "{\"success\":false}");
    }
    
    return ESP_OK;
}

esp_err_t ServerManager::api_reboot_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    send_json_response(req, "{\"success\":true,\"message\":\"Rebooting...\"}");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t ServerManager::api_wifi_status_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    DynamicJsonDocument doc(512);
    bool isConnected = wifi.isConnected();
    doc["connected"] = isConnected;
    
    char ip[16];
    wifi.getLocalIP(ip, sizeof(ip));
    doc["ip"] = ip;
    
    if (isConnected) {
        // Get SSID
        wifi_config_t wifi_config;
        if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
            doc["ssid"] = (char*)wifi_config.sta.ssid;
        } else {
            doc["ssid"] = "Unknown";
        }
        
        // Get MAC address
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        doc["mac"] = macStr;
        
        // Get RSSI (signal strength)
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            doc["rssi"] = ap_info.rssi;
        } else {
            doc["rssi"] = 0;
        }
    } else {
        doc["ssid"] = "Nicht verbunden";
        
        // Still provide MAC even when disconnected
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        doc["mac"] = macStr;
        doc["rssi"] = 0;
    }
    
    char json[512];
    serializeJson(doc, json, sizeof(json));
    send_json_response(req, json);
    return ESP_OK;
}

esp_err_t ServerManager::api_pwm_status_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    DynamicJsonDocument doc(512);
    doc["manual_mode"] = Config::MANUAL_PWM_MODE;
    doc["manual_freq"] = Config::MANUAL_PWM_FREQ;
    doc["manual_duty"] = Config::MANUAL_PWM_DUTY;
    doc["frequency"] = Config::MANUAL_PWM_FREQ; // Alias
    doc["duty"] = Config::MANUAL_PWM_DUTY; // Alias
    doc["auto_pwm"] = Config::AUTO_PWM_ENABLED;
    
    // Get current duty cycle from hardware
    uint32_t duty_raw = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    doc["duty_raw"] = duty_raw;
    doc["duty_percent"] = (duty_raw * 100) / 255;
    
    char json[512];
    serializeJson(doc, json, sizeof(json));
    send_json_response(req, json);
    return ESP_OK;
}

esp_err_t ServerManager::api_pwm_control_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, buf);
    
    if (doc.containsKey("duty")) {
        int duty = doc["duty"];
        duty = (duty * 255) / 100; // Convert 0-100 to 0-255
        serverInstance->setPWMDuty(duty);
    }
    
    if (doc.containsKey("frequency")) {
        uint32_t freq = doc["frequency"];
        serverInstance->reconfigurePWM(freq);
        Config::saveManualPWMSettings(freq, Config::MANUAL_PWM_DUTY);
    }
    
    send_json_response(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t ServerManager::api_login_handler(httpd_req_t *req) {
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, buf);
    
    const char* password = doc["password"];
    
    // Validate password against Config::WEB_PASSWORD
    bool isValid = (password != nullptr && strcmp(password, Config::WEB_PASSWORD) == 0);
    
    if (isValid) {
        // Generate new token
        char* token = generate_token();
        
        // Return token in JSON response
        char response[128];
        snprintf(response, sizeof(response), "{\"success\":true,\"token\":\"%s\"}", token);
        send_json_response(req, response);
    } else {
        send_json_response(req, "{\"success\":false,\"error\":\"Invalid password\"}");
    }
    
    return ESP_OK;
}

// POST handler for HTML login form (receives form-urlencoded data)
esp_err_t ServerManager::login_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ESP_LOGI(TAG, "Login POST data: %s", buf);
    
    // Parse form data: password=xxx&rememberMe=on
    char password[64] = {0};
    char *pwd_start = strstr(buf, "password=");
    if (pwd_start) {
        pwd_start += 9; // Skip "password="
        char *pwd_end = strchr(pwd_start, '&');
        int len = pwd_end ? (pwd_end - pwd_start) : strlen(pwd_start);
        if (len > 0 && len < sizeof(password)) {
            strncpy(password, pwd_start, len);
            password[len] = '\0';
            
            // URL decode (replace %20 with space, etc.)
            // Simple implementation - just check for basic chars
            ESP_LOGI(TAG, "Password received (length: %d)", strlen(password));
        }
    }
    
    // Validate password against Config::WEB_PASSWORD
    bool isValid = (strlen(password) > 0 && strcmp(password, Config::WEB_PASSWORD) == 0);
    
    if (isValid) {
        ESP_LOGI(TAG, "Login successful");
        
        // Generate new token
        char* token = generate_token();
        
        // Redirect to index page with token
        char location[128];
        snprintf(location, sizeof(location), "/?token=%s", token);
        
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", location);
        httpd_resp_send(req, NULL, 0);
    } else {
        ESP_LOGI(TAG, "Login failed - invalid password");
        // Redirect back to login with error
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html?error=1");
        httpd_resp_send(req, NULL, 0);
    }
    
    return ESP_OK;
}

// System Info Handler
esp_err_t ServerManager::api_system_info_handler(httpd_req_t *req) {
    DynamicJsonDocument doc(1024);
    
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    // Basic chip info
    doc["chipModel"] = "ESP32-PICO-D4";
    doc["chipCores"] = chip_info.cores;
    doc["chipRevision"] = chip_info.revision;
    
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    doc["flashSize"] = flash_size / (1024 * 1024);
    
    doc["freeHeap"] = esp_get_free_heap_size();
    doc["minFreeHeap"] = esp_get_minimum_free_heap_size();
    doc["uptime"] = esp_timer_get_time() / 1000000;
    doc["sdkVersion"] = esp_get_idf_version();
    
    // Device info needed by HTML
    doc["device_name"] = Config::DEVICE_NAME;
    doc["firmware_version"] = Config::FIRMWARE_VERSION;
    
    // WiFi info
    char ip[16];
    wifi.getLocalIP(ip, sizeof(ip));
    doc["wifi_ip"] = ip;
    doc["wifi_connected"] = wifi.isConnected();
    
    // Get WiFi SSID and other info
    wifi_config_t wifi_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
        doc["wifi_ssid"] = (char*)wifi_config.sta.ssid;
    } else {
        doc["wifi_ssid"] = "Unknown";
    }
    
    // Get MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    doc["wifi_mac"] = macStr;
    
    // Get RSSI (signal strength)
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        doc["wifi_rssi"] = ap_info.rssi;
    } else {
        doc["wifi_rssi"] = 0;
    }
    
    // MQTT status
    doc["mqtt_connected"] = mqttManager.isConnected();
    
    char response[1024];
    serializeJson(doc, response, sizeof(response));
    send_json_response(req, response);
    return ESP_OK;
}

// Settings Handler - returns all current settings
esp_err_t ServerManager::api_settings_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    DynamicJsonDocument doc(1536);
    doc["device_name"] = Config::DEVICE_NAME;
    doc["deviceName"] = Config::DEVICE_NAME; // Alias for compatibility
    doc["mqtt_server"] = Config::MQTT_SERVER;
    doc["mqttServer"] = Config::MQTT_SERVER; // Alias
    doc["mqtt_port"] = Config::MQTT_PORT;
    doc["mqttPort"] = Config::MQTT_PORT; // Alias
    doc["mqtt_user"] = Config::MQTT_USER;
    doc["mqttUser"] = Config::MQTT_USER; // Alias
    doc["mqtt_pass"] = ""; // Don't send password back
    doc["mqttPass"] = ""; // Alias
    doc["mqtt_topic"] = Config::MQTT_TOPIC;
    doc["mqttTopic"] = Config::MQTT_TOPIC; // Alias
    doc["mqtt_connected"] = mqttManager.isConnected();
    doc["manual_freq"] = Config::MANUAL_PWM_FREQ;
    doc["manualPWMFreq"] = Config::MANUAL_PWM_FREQ; // Alias
    doc["manual_duty"] = Config::MANUAL_PWM_DUTY;
    doc["manualPWMDuty"] = Config::MANUAL_PWM_DUTY; // Alias
    doc["manual_mode"] = Config::MANUAL_PWM_MODE;
    doc["manualPWMMode"] = Config::MANUAL_PWM_MODE; // Alias
    doc["auto_pwm"] = Config::AUTO_PWM_ENABLED;
    doc["autoPWMEnabled"] = Config::AUTO_PWM_ENABLED; // Alias
    doc["startTemp"] = Config::TEMP_START;
    doc["tempStart"] = Config::TEMP_START; // Alias
    doc["maxTemp"] = Config::TEMP_MAX;
    doc["tempMax"] = Config::TEMP_MAX; // Alias
    doc["temp_unit"] = Config::TEMP_UNIT;
    doc["tempUnit"] = Config::TEMP_UNIT; // Alias
    
    // Get actual AP status from WiFi mode
    wifi_mode_t mode;
    bool ap_running = false;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        ap_running = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    }
    doc["ap_enabled"] = ap_running;
    doc["apEnabled"] = ap_running; // Alias
    
    // LED state and color
    ServerManager* manager = (ServerManager*)req->user_ctx;
    doc["led_state"] = manager->ledState;
    doc["led_r"] = manager->ledColorR;
    doc["led_g"] = manager->ledColorG;
    doc["led_b"] = manager->ledColorB;
    
    doc["firmware_version"] = Config::FIRMWARE_VERSION;
    doc["last_password_change"] = "Nie"; // TODO: Track this in NVS
    
    char response[1536];
    serializeJson(doc, response, sizeof(response));
    send_json_response(req, response);
    return ESP_OK;
}

// Restart Handler
esp_err_t ServerManager::api_restart_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ESP_LOGI(TAG, "Restart requested");
    send_json_response(req, "{\"success\":true,\"message\":\"Restarting...\"}");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// Factory Reset Handler
esp_err_t ServerManager::api_factory_reset_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ESP_LOGI(TAG, "Factory reset requested");
    
    // Clear NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    send_json_response(req, "{\"success\":true,\"message\":\"Factory reset complete. Restarting...\"}");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// WiFi Scan Handler
esp_err_t ServerManager::api_wifi_scan_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ESP_LOGI(TAG, "WiFi scan requested");
    
    // Check if WiFi is initialized
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        send_json_response(req, "{\"networks\":[],\"error\":\"WiFi not initialized\"}");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "WiFi mode before scan: %d (AP=2, STA=1, APSTA=3)", mode);
    
    // Ensure WiFi is in STA or APSTA mode for scanning
    if (mode == WIFI_MODE_AP) {
        // Create STA interface if it doesn't exist
        esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif == NULL) {
            ESP_LOGI(TAG, "Creating STA network interface for scanning");
            sta_netif = esp_netif_create_default_wifi_sta();
        }
        
        // Switch to APSTA mode temporarily to allow scanning
        ESP_LOGI(TAG, "Switching from AP to APSTA mode for scanning");
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(err));
            send_json_response(req, "{\"networks\":[],\"error\":\"Failed to enable STA mode\"}");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Give it time to switch and initialize
    }
    
    // Start WiFi scan optimized for APSTA mode
    // Use ACTIVE scan with minimal time to reduce disruption
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 0,      // Minimal time per channel
                .max = 120     // Max 120ms per channel (reduced from default 300ms)
            },
            .passive = 0
        },
        .home_chan_dwell_time = 30,  // Return to AP channel every 30ms to service clients
        .channel_bitmap = {
            .ghz_2_channels = 0,
            .ghz_5_channels = 0
        },
        .coex_background_scan = false  // Disable to avoid longer scan times
    };
    
    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        char error_response[256];
        snprintf(error_response, sizeof(error_response), 
                 "{\"networks\":[],\"error\":\"Scan failed: %s\"}", 
                 esp_err_to_name(err));
        send_json_response(req, error_response);
        return ESP_OK;
    }
    
    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    ESP_LOGI(TAG, "WiFi scan found %d networks", ap_count);
    
    if (ap_count == 0) {
        send_json_response(req, "{\"networks\":[]}");
        return ESP_OK;
    }
    
    // Limit to 20 networks to avoid memory issues
    if (ap_count > 20) {
        ap_count = 20;
    }
    
    wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_records == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        send_json_response(req, "{\"networks\":[]}");
        return ESP_OK;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    
    // Build JSON response
    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.createNestedArray("networks");
    
    for (int i = 0; i < ap_count; i++) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = (char*)ap_records[i].ssid;
        network["rssi"] = ap_records[i].rssi;
        network["channel"] = ap_records[i].primary;
        network["encryption"] = ap_records[i].authmode;
        network["security"] = (ap_records[i].authmode != WIFI_AUTH_OPEN);
    }
    
    free(ap_records);
    
    char response[4096];
    size_t len = serializeJson(doc, response, sizeof(response));
    ESP_LOGI(TAG, "WiFi scan response length: %d bytes", len);
    send_json_response(req, response);
    
    return ESP_OK;
}

// WiFi Connect Handler (POST /wifi_connect)
esp_err_t ServerManager::api_wifi_connect_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    char ssid[64] = {0};
    char password[64] = {0};
    
    // Parse form data
    if (strstr(buf, "ssid=")) {
        char *ssid_start = strstr(buf, "ssid=") + 5;
        char *ssid_end = strchr(ssid_start, '&');
        int len = ssid_end ? (ssid_end - ssid_start) : strlen(ssid_start);
        if (len > 0 && len < sizeof(ssid)) {
            char temp_ssid[64];
            strncpy(temp_ssid, ssid_start, len);
            temp_ssid[len] = '\0';
            // URL decode SSID
            url_decode(ssid, temp_ssid, sizeof(ssid));
            ESP_LOGI(TAG, "Decoded SSID: %s", ssid);
        }
    }
    
    if (strstr(buf, "password=")) {
        char *pwd_start = strstr(buf, "password=") + 9;
        char *pwd_end = strchr(pwd_start, '&');
        int len = pwd_end ? (pwd_end - pwd_start) : strlen(pwd_start);
        if (len > 0 && len < sizeof(password)) {
            char temp_pwd[64];
            strncpy(temp_pwd, pwd_start, len);
            temp_pwd[len] = '\0';
            // URL decode password
            url_decode(password, temp_pwd, sizeof(password));
        }
    }
    
    // Fallback to JSON
    if (strlen(ssid) == 0) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, buf);
        
        const char* s = doc["ssid"];
        const char* p = doc["password"];
        
        if (s) strncpy(ssid, s, sizeof(ssid) - 1);
        if (p) strncpy(password, p, sizeof(password) - 1);
    }
    
    if (strlen(ssid) > 0 && strlen(password) > 0) {
        // Save WiFi credentials to NVS
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_set_str(nvs_handle, "ssid", ssid);
            nvs_set_str(nvs_handle, "password", password);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
            ESP_LOGI(TAG, "WiFi credentials saved to NVS");
        } else {
            ESP_LOGE(TAG, "Failed to open NVS for WiFi credentials");
        }
        
        // Start non-blocking WiFi connection
        // The checkWiFiConnection() loop will pick up the new credentials
        wifi_config_t sta_config = {};
        strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        
        ESP_LOGI(TAG, "Starting WiFi connection to: %s", ssid);
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        esp_wifi_connect();
        
        // Return simple success message that HTML expects
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    
    httpd_resp_send(req, "Error: Missing SSID or password", 32);
    return ESP_FAIL;
}

// WiFi Disconnect Handler
esp_err_t ServerManager::api_wifi_disconnect_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ESP_LOGI(TAG, "WiFi disconnect requested");
    wifi.disconnect();
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// WiFi Clear Handler
esp_err_t ServerManager::api_wifi_clear_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ESP_LOGI(TAG, "Clear WiFi credentials requested");
    wifi.clearCredentials();
    wifi.disconnect();
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Toggle AP Handler
esp_err_t ServerManager::api_toggle_ap_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse form data (enable=1 or enable=0)
    char enable_str[8] = {0};
    bool enable = false;
    
    if (strstr(buf, "enable=")) {
        char *enable_start = strstr(buf, "enable=") + 7;
        char *enable_end = strchr(enable_start, '&');
        int len = enable_end ? (enable_end - enable_start) : strlen(enable_start);
        if (len > 0 && len < sizeof(enable_str)) {
            strncpy(enable_str, enable_start, len);
            enable_str[len] = '\0';
            enable = (strcmp(enable_str, "1") == 0);
        }
    } else {
        // Try JSON format
        DynamicJsonDocument doc(256);
        deserializeJson(doc, buf);
        enable = doc["enable"];
    }
    
    ESP_LOGI(TAG, "Toggle AP: %s", enable ? "ON" : "OFF");
    
    // Save setting to NVS
    Config::saveAPEnabled(enable);
    
    // Clear emergency mode flag when user manually changes AP setting
    Config::AP_EMERGENCY_MODE = false;
    
    // Get current WiFi mode
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    
    if (enable) {
        // Enable AP
        if (current_mode == WIFI_MODE_STA) {
            // Switch from STA to APSTA
            ESP_LOGI(TAG, "Switching from STA to APSTA mode");
            esp_wifi_set_mode(WIFI_MODE_APSTA);
            wifi.beginAP();
        } else if (current_mode == WIFI_MODE_NULL) {
            // Start fresh AP
            ESP_LOGI(TAG, "Starting AP mode");
            wifi.beginAP();
        }
        // If already APSTA or AP, do nothing
    } else {
        // Disable AP
        if (current_mode == WIFI_MODE_APSTA) {
            // Switch from APSTA to STA only
            ESP_LOGI(TAG, "Switching from APSTA to STA mode (AP disabled)");
            wifi.stopAP();
            esp_wifi_set_mode(WIFI_MODE_STA);
        } else if (current_mode == WIFI_MODE_AP) {
            // Can't disable AP if it's the only mode running
            ESP_LOGW(TAG, "Cannot disable AP - it's the only network interface. Connect to WiFi first!");
            httpd_resp_send(req, "ERROR: Connect to WiFi before disabling AP", 43);
            return ESP_OK;
        }
    }
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// MQTT Settings Handler
esp_err_t ServerManager::api_mqtt_settings_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Try to parse as form data first
    char server[128] = {0};
    char port_str[8] = {0};
    char user[64] = {0};
    char pass[64] = {0};
    char topic[64] = {0};
    
    bool is_form_data = (strstr(buf, "server=") != NULL);
    
    if (is_form_data) {
        // Parse form data
        if (strstr(buf, "server=")) {
            char *start = strstr(buf, "server=") + 7;
            char *end = strchr(start, '&');
            int len = end ? (end - start) : strlen(start);
            if (len > 0 && len < sizeof(server)) {
                strncpy(server, start, len);
                server[len] = '\0';
            }
        }
        
        if (strstr(buf, "port=")) {
            char *start = strstr(buf, "port=") + 5;
            char *end = strchr(start, '&');
            int len = end ? (end - start) : strlen(start);
            if (len > 0 && len < sizeof(port_str)) {
                strncpy(port_str, start, len);
                port_str[len] = '\0';
            }
        }
        
        if (strstr(buf, "user=")) {
            char *start = strstr(buf, "user=") + 5;
            char *end = strchr(start, '&');
            int len = end ? (end - start) : strlen(start);
            if (len >= 0 && len < sizeof(user)) {
                char temp[64];
                strncpy(temp, start, len);
                temp[len] = '\0';
                url_decode(user, temp, sizeof(user));
            }
        }
        
        if (strstr(buf, "pass=")) {
            char *start = strstr(buf, "pass=") + 5;
            char *end = strchr(start, '&');
            int len = end ? (end - start) : strlen(start);
            if (len >= 0 && len < sizeof(pass)) {
                char temp[64];
                strncpy(temp, start, len);
                temp[len] = '\0';
                url_decode(pass, temp, sizeof(pass));
            }
        }
    } else {
        // Parse JSON
        DynamicJsonDocument doc(512);
        deserializeJson(doc, buf);
        
        const char* srv = doc["server"];
        uint16_t prt = doc["port"];
        const char* usr = doc["user"];
        const char* pwd = doc["pass"];
        const char* tpc = doc["topic"];
        
        if (srv) strncpy(server, srv, sizeof(server) - 1);
        if (prt > 0) snprintf(port_str, sizeof(port_str), "%d", prt);
        if (usr) strncpy(user, usr, sizeof(user) - 1);
        if (pwd) strncpy(pass, pwd, sizeof(pass) - 1);
        if (tpc) strncpy(topic, tpc, sizeof(topic) - 1);
    }
    
    // Validate and save
    if (strlen(server) > 0) {
        uint16_t port = strlen(port_str) > 0 ? atoi(port_str) : 1883;
        Config::saveMQTTSettings(server, port, user, pass, strlen(topic) > 0 ? topic : Config::MQTT_TOPIC);
        
        ESP_LOGI(TAG, "MQTT settings saved, reconnecting...");
        mqttManager.reconnect();
        
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// MQTT Test Handler
esp_err_t ServerManager::api_mqtt_test_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ESP_LOGI(TAG, "MQTT test connection requested");
    
    // Check current connection status
    bool connected = mqttManager.isConnected();
    
    char response[256];
    if (connected) {
        snprintf(response, sizeof(response), 
                "{\"success\":true,\"message\":\"MQTT connected to %s:%d\"}", 
                Config::MQTT_SERVER, Config::MQTT_PORT);
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\":false,\"error\":\"Not connected. Server=%s:%d, User=%s\"}", 
                Config::MQTT_SERVER, Config::MQTT_PORT, Config::MQTT_USER);
    }
    
    send_json_response(req, response);
    return ESP_OK;
}

// Manual PWM Mode Handler
esp_err_t ServerManager::api_manual_pwm_mode_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    bool enabled = false;
    
    // Parse form data (mode=manual or mode=auto)
    if (strstr(buf, "mode=")) {
        char *mode_start = strstr(buf, "mode=") + 5;
        char *mode_end = strchr(mode_start, '&');
        char mode[16] = {0};
        int len = mode_end ? (mode_end - mode_start) : strlen(mode_start);
        if (len > 0 && len < sizeof(mode)) {
            strncpy(mode, mode_start, len);
            mode[len] = '\0';
            enabled = (strcmp(mode, "manual") == 0);
        }
    } else {
        // Try JSON format
        DynamicJsonDocument doc(256);
        deserializeJson(doc, buf);
        enabled = doc["enabled"];
    }
    
    Config::saveManualPWMMode(enabled);
    
    ESP_LOGI(TAG, "Manual PWM mode: %s", enabled ? "ON" : "OFF");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Manual PWM Settings Handler
esp_err_t ServerManager::api_manual_pwm_settings_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ServerManager* serverInstance = (ServerManager*)req->user_ctx;
    
    uint32_t freq = 0;
    uint8_t duty = 0;
    
    // Parse form data
    if (strstr(buf, "frequency=")) {
        char *freq_start = strstr(buf, "frequency=") + 10;
        char *freq_end = strchr(freq_start, '&');
        char freq_str[16] = {0};
        int len = freq_end ? (freq_end - freq_start) : strlen(freq_start);
        if (len > 0 && len < sizeof(freq_str)) {
            strncpy(freq_str, freq_start, len);
            freq_str[len] = '\0';
            freq = atoi(freq_str);
        }
    }
    
    if (strstr(buf, "duty=")) {
        char *duty_start = strstr(buf, "duty=") + 5;
        char *duty_end = strchr(duty_start, '&');
        char duty_str[16] = {0};
        int len = duty_end ? (duty_end - duty_start) : strlen(duty_start);
        if (len > 0 && len < sizeof(duty_str)) {
            strncpy(duty_str, duty_start, len);
            duty_str[len] = '\0';
            duty = atoi(duty_str);
        }
    }
    
    // Fallback to JSON if form parsing failed
    if (freq == 0 && duty == 0) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, buf);
        
        if (doc.containsKey("frequency")) {
            freq = doc["frequency"];
        }
        
        if (doc.containsKey("dutyCycle")) {
            duty = doc["dutyCycle"];
        } else if (doc.containsKey("duty")) {
            duty = doc["duty"];
        }
    }
    
    // Apply settings
    if (freq > 0) {
        serverInstance->reconfigurePWM(freq);
    }
    
    if (duty <= 100) {
        int duty_8bit = (duty * 255) / 100;
        serverInstance->setPWMDuty(duty_8bit);
        Config::saveManualPWMSettings(freq > 0 ? freq : Config::MANUAL_PWM_FREQ, duty);
    }
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Temperature Mapping Handler
esp_err_t ServerManager::api_temp_mapping_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    float tempStart = 0;
    float tempMax = 0;
    
    // Parse form data
    if (strstr(buf, "startTemp=")) {
        char *start_pos = strstr(buf, "startTemp=") + 10;
        char *end_pos = strchr(start_pos, '&');
        char temp_str[16] = {0};
        int len = end_pos ? (end_pos - start_pos) : strlen(start_pos);
        if (len > 0 && len < sizeof(temp_str)) {
            strncpy(temp_str, start_pos, len);
            temp_str[len] = '\0';
            tempStart = atof(temp_str);
        }
    }
    
    if (strstr(buf, "maxTemp=")) {
        char *start_pos = strstr(buf, "maxTemp=") + 8;
        char *end_pos = strchr(start_pos, '&');
        char temp_str[16] = {0};
        int len = end_pos ? (end_pos - start_pos) : strlen(start_pos);
        if (len > 0 && len < sizeof(temp_str)) {
            strncpy(temp_str, start_pos, len);
            temp_str[len] = '\0';
            tempMax = atof(temp_str);
        }
    }
    
    // Fallback to JSON
    if (tempStart == 0 && tempMax == 0) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, buf);
        
        tempStart = doc["tempStart"];
        tempMax = doc["tempMax"];
    }
    
    if (tempStart > 0 && tempMax > tempStart) {
        Config::saveTempMapping(tempStart, tempMax);
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// Temperature Mapping Status Handler
esp_err_t ServerManager::api_temp_mapping_status_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    DynamicJsonDocument doc(384);
    doc["tempStart"] = Config::TEMP_START;
    doc["startTemp"] = Config::TEMP_START; // Alias
    doc["tempMax"] = Config::TEMP_MAX;
    doc["maxTemp"] = Config::TEMP_MAX; // Alias
    doc["autoPWMEnabled"] = Config::AUTO_PWM_ENABLED;
    doc["auto_pwm"] = Config::AUTO_PWM_ENABLED; // Alias
    
    char response[384];
    serializeJson(doc, response, sizeof(response));
    send_json_response(req, response);
    return ESP_OK;
}

// KMeter Status Handler
esp_err_t ServerManager::api_kmeter_status_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ServerManager* serverInstance = (ServerManager*)req->user_ctx;
    
    DynamicJsonDocument doc(768);
    
    // Get KMeter data
    bool isReady = serverInstance->getKMeterManager()->isReady();
    float tempC = serverInstance->getKMeterManager()->getTemperatureCelsius();
    float tempF = serverInstance->getKMeterManager()->getTemperatureFahrenheit();
    float internalTemp = serverInstance->getKMeterManager()->getInternalTemperature();
    
    doc["connected"] = true; // TODO: Check actual connection
    doc["initialized"] = true; // Assume initialized if we got here
    doc["ready"] = isReady;
    doc["temperature"] = tempC; // Legacy field
    doc["temperature_celsius"] = tempC;
    doc["temperatureF"] = tempF;
    doc["temperature_fahrenheit"] = tempF;
    doc["internal_temperature"] = internalTemp;
    doc["unit"] = Config::TEMP_UNIT;
    
    // Status string
    if (isReady) {
        doc["status_string"] = "Bereit";
    } else {
        doc["status_string"] = "Nicht bereit";
    }
    
    // I2C configuration
    doc["i2c_address"] = "0x66";
    doc["read_interval"] = 1000; // Default 1 second
    
    char response[768];
    serializeJson(doc, response, sizeof(response));
    send_json_response(req, response);
    return ESP_OK;
}

// KMeter Config Handler
esp_err_t ServerManager::api_kmeter_config_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse form data
    if (strstr(buf, "unit=")) {
        char *unit_start = strstr(buf, "unit=") + 5;
        char *unit_end = strchr(unit_start, '&');
        char unit[8] = {0};
        int len = unit_end ? (unit_end - unit_start) : strlen(unit_start);
        if (len > 0 && len < sizeof(unit)) {
            strncpy(unit, unit_start, len);
            unit[len] = '\0';
            Config::saveTempUnit(unit);
            httpd_resp_send(req, "OK", 2);
            return ESP_OK;
        }
    }
    
    if (strstr(buf, "read_interval=")) {
        char *interval_start = strstr(buf, "read_interval=") + 14;
        char *interval_end = strchr(interval_start, '&');
        char interval_str[16] = {0};
        int len = interval_end ? (interval_end - interval_start) : strlen(interval_start);
        if (len > 0 && len < sizeof(interval_str)) {
            strncpy(interval_str, interval_start, len);
            interval_str[len] = '\0';
            // TODO: Save interval setting
            httpd_resp_send(req, "OK", 2);
            return ESP_OK;
        }
    }
    
    if (strstr(buf, "i2c_address=")) {
        char *addr_start = strstr(buf, "i2c_address=") + 12;
        char *addr_end = strchr(addr_start, '&');
        char addr_str[16] = {0};
        int len = addr_end ? (addr_end - addr_start) : strlen(addr_start);
        if (len > 0 && len < sizeof(addr_str)) {
            strncpy(addr_str, addr_start, len);
            addr_str[len] = '\0';
            // TODO: Save I2C address and reinitialize sensor
            httpd_resp_send(req, "OK", 2);
            return ESP_OK;
        }
    }
    
    // Fallback to JSON
    DynamicJsonDocument doc(512);
    deserializeJson(doc, buf);
    
    const char* unit = doc["unit"];
    if (unit) {
        Config::saveTempUnit(unit);
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// LED Toggle Handler
esp_err_t ServerManager::api_led_toggle_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ServerManager* manager = (ServerManager*)req->user_ctx;
    
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        // If no body, just toggle
        ESP_LOGI(TAG, "LED toggle (no body)");
        if (manager->ledManager) {
            manager->ledState = !manager->ledState;
            if (manager->ledState) {
                manager->ledManager->setColor(manager->ledColorR, manager->ledColorG, manager->ledColorB);
            } else {
                manager->ledManager->off();
            }
        }
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    // Parse form data (state=1 or state=0)
    char state[8] = {0};
    if (strstr(buf, "state=")) {
        char *state_start = strstr(buf, "state=") + 6;
        char *state_end = strchr(state_start, '&');
        int len = state_end ? (state_end - state_start) : strlen(state_start);
        if (len > 0 && len < sizeof(state)) {
            strncpy(state, state_start, len);
            state[len] = '\0';
            
            bool enabled = (strcmp(state, "1") == 0);
            ESP_LOGI(TAG, "LED: %s with color R=%d G=%d B=%d", 
                     enabled ? "ON" : "OFF", 
                     manager->ledColorR, manager->ledColorG, manager->ledColorB);
            
            if (manager->ledManager) {
                manager->ledState = enabled;
                if (enabled) {
                    manager->ledManager->setColor(manager->ledColorR, manager->ledColorG, manager->ledColorB);
                } else {
                    manager->ledManager->off();
                }
            }
            
            httpd_resp_send(req, "OK", 2);
            return ESP_OK;
        }
    }
    
    // Try JSON format as fallback
    DynamicJsonDocument doc(256);
    deserializeJson(doc, buf);
    
    if (doc.containsKey("enabled")) {
        bool enabled = doc["enabled"];
        ESP_LOGI(TAG, "LED: %s", enabled ? "ON" : "OFF");
        
        if (manager->ledManager) {
            manager->ledState = enabled;
            if (enabled) {
                manager->ledManager->setColor(manager->ledColorR, manager->ledColorG, manager->ledColorB);
            } else {
                manager->ledManager->off();
            }
        }
    }
    
    if (doc.containsKey("color")) {
        const char* color = doc["color"];
        ESP_LOGI(TAG, "LED color: %s", color);
        // Note: Color is typically set via RGB values in api_led_color_handler
    }
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

esp_err_t ServerManager::api_led_color_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ServerManager* manager = (ServerManager*)req->user_ctx;
    
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing RGB values");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    // Parse form data (r=###&g=###&b=###)
    int r = 0, g = 0, b = 0;
    char *r_start = strstr(buf, "r=");
    char *g_start = strstr(buf, "g=");
    char *b_start = strstr(buf, "b=");
    
    if (r_start && g_start && b_start) {
        r = atoi(r_start + 2);
        g = atoi(g_start + 2);
        b = atoi(b_start + 2);
        
        // Validate ranges
        r = (r < 0) ? 0 : (r > 255) ? 255 : r;
        g = (g < 0) ? 0 : (g > 255) ? 255 : g;
        b = (b < 0) ? 0 : (b > 255) ? 255 : b;
        
        ESP_LOGI(TAG, "LED color stored: R=%d G=%d B=%d (LED state: %s)", r, g, b, manager->ledState ? "ON" : "OFF");
        
        // Store color in manager
        manager->ledColorR = r;
        manager->ledColorG = g;
        manager->ledColorB = b;
        
        // Only apply color if LED is currently ON
        if (manager->ledManager && manager->ledState) {
            manager->ledManager->setColor(r, g, b);
            ESP_LOGI(TAG, "LED color applied immediately (LED is ON)");
        }
        
        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid RGB format");
    return ESP_FAIL;
}

// Change Password Handler
esp_err_t ServerManager::api_change_password_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    char currentPassword[64] = {0};
    char newPassword[64] = {0};
    
    // Parse form data (current_password and new_password)
    if (strstr(buf, "current_password=")) {
        char *pwd_start = strstr(buf, "current_password=") + 17;
        char *pwd_end = strchr(pwd_start, '&');
        int len = pwd_end ? (pwd_end - pwd_start) : strlen(pwd_start);
        if (len > 0 && len < sizeof(currentPassword)) {
            strncpy(currentPassword, pwd_start, len);
            currentPassword[len] = '\0';
        }
    }
    
    if (strstr(buf, "new_password=")) {
        char *pwd_start = strstr(buf, "new_password=") + 13;
        char *pwd_end = strchr(pwd_start, '&');
        int len = pwd_end ? (pwd_end - pwd_start) : strlen(pwd_start);
        if (len > 0 && len < sizeof(newPassword)) {
            strncpy(newPassword, pwd_start, len);
            newPassword[len] = '\0';
        }
    }
    
    // Fallback to JSON with camelCase
    if (strlen(currentPassword) == 0 || strlen(newPassword) == 0) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, buf);
        
        const char* currPwd = doc["currentPassword"];
        const char* newPwd = doc["newPassword"];
        
        if (currPwd) strncpy(currentPassword, currPwd, sizeof(currentPassword) - 1);
        if (newPwd) strncpy(newPassword, newPwd, sizeof(newPassword) - 1);
    }
    
    if (strlen(currentPassword) == 0 || strlen(newPassword) == 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Verify current password
    if (strcmp(currentPassword, Config::WEB_PASSWORD) != 0) {
        httpd_resp_send(req, "INVALID", 7);
        return ESP_OK;
    }
    
    // Save new password
    Config::saveWebPassword(newPassword);
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// Logout Handler
esp_err_t ServerManager::api_logout_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ESP_LOGI(TAG, "Logout - invalidating token");
    
    // Invalidate the current token
    current_token[0] = '\0';
    token_created = 0;
    
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login.html");
    httpd_resp_send(req, NULL, 0);
    
    return ESP_OK;
}

// OTA TAR Update Handler - uploads single .tar file with streaming
esp_err_t ServerManager::api_ota_tar_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ServerManager* self = (ServerManager*)req->user_ctx;
    ESP_LOGI(TAG, "OTA TAR Upload started, content length: %d", req->content_len);
    ESP_LOGI(TAG, "Free heap before upload: %d bytes", esp_get_free_heap_size());
    
    if (req->content_len == 0 || req->content_len > 10 * 1024 * 1024) { // Max 10MB
        ESP_LOGE(TAG, "Invalid content length: %d", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file size (max 10MB)");
        return ESP_FAIL;
    }
    
    // Process TAR directly from HTTP stream without saving to file
    // This saves memory and SPIFFS space
    
    const size_t BUFFER_SIZE = 32 * 1024; // 32KB buffer
    uint8_t* buffer = (uint8_t*)malloc(BUFFER_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    size_t totalReceived = 0;
    size_t remaining = req->content_len;
    bool firmwareFound = false;
    bool spiffsFound = false;
    bool success = true;
    
    typedef struct {
        char name[100];
        char mode[8];
        char uid[8];
        char gid[8];
        char size[12];
        char mtime[12];
        char checksum[8];
        char typeflag;
        char linkname[100];
        char magic[6];
        char version[2];
        char uname[32];
        char gname[32];
        char devmajor[8];
        char devminor[8];
        char prefix[155];
        char padding[12];
    } TarHeader;
    
    auto parseOctal = [](const char* str, size_t len) -> size_t {
        size_t result = 0;
        for (size_t i = 0; i < len && str[i] != '\0' && str[i] != ' '; i++) {
            if (str[i] >= '0' && str[i] <= '7') {
                result = result * 8 + (str[i] - '0');
            }
        }
        return result;
    };
    
    while (remaining > 0 && success) {
        // Read TAR header (512 bytes)
        if (remaining < 512) break;
        
        TarHeader header;
        int ret = httpd_req_recv(req, (char*)&header, 512);
        if (ret != 512) {
            ESP_LOGE(TAG, "Failed to read TAR header");
            success = false;
            break;
        }
        totalReceived += 512;
        remaining -= 512;
        
        // Check for end of archive (zero block)
        bool allZero = true;
        uint8_t* headerBytes = (uint8_t*)&header;
        for (size_t i = 0; i < 512; i++) {
            if (headerBytes[i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            ESP_LOGI(TAG, "Reached end of TAR archive");
            break;
        }
        
        // Verify TAR magic
        if (strncmp(header.magic, "ustar", 5) != 0) {
            ESP_LOGW(TAG, "Invalid TAR magic, skipping");
            // Skip this block and continue
            continue;
        }
        
        // Parse file size
        size_t fileSize = parseOctal(header.size, 12);
        ESP_LOGI(TAG, "Found file: %s (%u bytes)", header.name, fileSize);
        
        // Calculate padded size (512-byte aligned)
        size_t paddedSize = (fileSize + 511) & ~511;
        
        if (header.typeflag == '0' || header.typeflag == '\0') {
            // Regular file
            if (strcmp(header.name, "firmware.bin") == 0) {
                ESP_LOGI(TAG, "Processing firmware.bin (%u bytes)...", fileSize);
                
                // Stream firmware directly to flash without loading into RAM
                struct StreamContext {
                    httpd_req_t* req;
                    size_t remaining;
                    size_t paddedSize;
                };
                
                StreamContext ctx;
                ctx.req = req;
                ctx.remaining = fileSize;
                ctx.paddedSize = paddedSize;
                
                auto readFirmware = [](uint8_t* buffer, size_t size, void* userData) -> size_t {
                    StreamContext* ctx = (StreamContext*)userData;
                    if (ctx->remaining == 0) return 0;
                    
                    size_t toRead = (size > ctx->remaining) ? ctx->remaining : size;
                    int ret = httpd_req_recv(ctx->req, (char*)buffer, toRead);
                    if (ret <= 0) return 0;
                    
                    ctx->remaining -= ret;
                    return ret;
                };
                
                if (self->otaManager.flashFirmwareStreaming(fileSize, readFirmware, &ctx)) {
                    firmwareFound = true;
                    ESP_LOGI(TAG, "Firmware flashed successfully");
                    
                    // Skip padding if any
                    size_t padding = paddedSize - fileSize;
                    if (padding > 0 && remaining >= padding) {
                        int ret = httpd_req_recv(req, (char*)buffer, padding);
                        if (ret > 0) {
                            totalReceived += ret;
                            remaining -= ret;
                        }
                    }
                    // Update counters for data already read
                    totalReceived += fileSize - ctx.remaining;
                    remaining -= (fileSize - ctx.remaining);
                } else {
                    ESP_LOGE(TAG, "Firmware flash failed");
                    success = false;
                }
                
            } else if (strcmp(header.name, "spiffs.bin") == 0) {
                ESP_LOGI(TAG, "Processing spiffs.bin (%u bytes)...", fileSize);
                
                // Stream SPIFFS directly to flash without loading into RAM
                struct StreamContext {
                    httpd_req_t* req;
                    size_t remaining;
                    size_t paddedSize;
                };
                
                StreamContext ctx;
                ctx.req = req;
                ctx.remaining = fileSize;
                ctx.paddedSize = paddedSize;
                
                auto readSPIFFS = [](uint8_t* buffer, size_t size, void* userData) -> size_t {
                    StreamContext* ctx = (StreamContext*)userData;
                    if (ctx->remaining == 0) return 0;
                    
                    size_t toRead = (size > ctx->remaining) ? ctx->remaining : size;
                    int ret = httpd_req_recv(ctx->req, (char*)buffer, toRead);
                    if (ret <= 0) return 0;
                    
                    ctx->remaining -= ret;
                    return ret;
                };
                
                if (self->otaManager.flashSPIFFSStreaming(fileSize, readSPIFFS, &ctx)) {
                    spiffsFound = true;
                    ESP_LOGI(TAG, "SPIFFS flashed successfully");
                    
                    // Skip padding if any
                    size_t padding = paddedSize - fileSize;
                    if (padding > 0 && remaining >= padding) {
                        int ret = httpd_req_recv(req, (char*)buffer, padding);
                        if (ret > 0) {
                            totalReceived += ret;
                            remaining -= ret;
                        }
                    }
                    // Update counters for data already read
                    totalReceived += fileSize - ctx.remaining;
                    remaining -= (fileSize - ctx.remaining);
                } else {
                    ESP_LOGE(TAG, "SPIFFS flash failed");
                    success = false;
                }
                
            } else {
                // Skip unknown file
                ESP_LOGI(TAG, "Skipping file: %s", header.name);
                size_t toSkip = paddedSize;
                while (toSkip > 0 && remaining > 0) {
                    size_t chunkSize = (toSkip > BUFFER_SIZE) ? BUFFER_SIZE : toSkip;
                    ret = httpd_req_recv(req, (char*)buffer, chunkSize);
                    if (ret <= 0) break;
                    toSkip -= ret;
                    totalReceived += ret;
                    remaining -= ret;
                }
            }
        } else {
            // Skip non-regular files
            size_t toSkip = paddedSize;
            while (toSkip > 0 && remaining > 0) {
                size_t chunkSize = (toSkip > BUFFER_SIZE) ? BUFFER_SIZE : toSkip;
                ret = httpd_req_recv(req, (char*)buffer, chunkSize);
                if (ret <= 0) break;
                toSkip -= ret;
                totalReceived += ret;
                remaining -= ret;
            }
        }
        
        if (!success) break;
    }
    
    free(buffer);
    
    if (!firmwareFound && !spiffsFound) {
        ESP_LOGE(TAG, "No firmware.bin or spiffs.bin found in TAR");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid TAR content");
        return ESP_FAIL;
    }
    
    if (success) {
        ESP_LOGI(TAG, "OTA Update successful, rebooting in 3 seconds...");
        const char* response = "{\"status\":\"success\",\"message\":\"Update successful, rebooting...\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "OTA Update failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Update failed");
        return ESP_FAIL;
    }
}

// OTA Firmware Update Handler
esp_err_t ServerManager::api_ota_firmware_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ServerManager* self = (ServerManager*)req->user_ctx;
    ESP_LOGI(TAG, "OTA Firmware Upload started, content length: %d", req->content_len);
    
    if (req->content_len == 0 || req->content_len > 2 * 1024 * 1024) { // Max 2MB
        ESP_LOGE(TAG, "Invalid content length: %d", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file size");
        return ESP_FAIL;
    }
    
    // Allocate buffer for firmware
    uint8_t* fwBuffer = (uint8_t*)malloc(req->content_len);
    if (!fwBuffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for firmware upload");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    // Read entire file
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, (char*)(fwBuffer + received), req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(fwBuffer);
            ESP_LOGE(TAG, "Failed to receive firmware data");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    
    ESP_LOGI(TAG, "Received complete firmware: %d bytes", received);
    
    // Flash firmware
    bool success = self->otaManager.flashFirmwareDirect(fwBuffer, received);
    
    free(fwBuffer);
    
    if (success) {
        ESP_LOGI(TAG, "Firmware Update successful, rebooting in 3 seconds...");
        const char* response = "{\"status\":\"success\",\"message\":\"Firmware update successful, rebooting...\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Firmware Update failed: %s", self->otaManager.getLastError());
        char errorMsg[512];
        snprintf(errorMsg, sizeof(errorMsg), 
                 "{\"status\":\"error\",\"message\":\"%s\"}", 
                 self->otaManager.getLastError());
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, errorMsg, strlen(errorMsg));
        return ESP_FAIL;
    }
}

// OTA Filesystem Update Handler
esp_err_t ServerManager::api_ota_filesystem_handler(httpd_req_t *req) {
    REQUIRE_AUTH();
    
    ServerManager* self = (ServerManager*)req->user_ctx;
    ESP_LOGI(TAG, "OTA Filesystem Upload started, content length: %d", req->content_len);
    
    if (req->content_len == 0 || req->content_len > 2 * 1024 * 1024) { // Max 2MB
        ESP_LOGE(TAG, "Invalid content length: %d", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid file size");
        return ESP_FAIL;
    }
    
    // Allocate buffer for SPIFFS
    uint8_t* fsBuffer = (uint8_t*)malloc(req->content_len);
    if (!fsBuffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for filesystem upload");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    
    // Read entire file
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, (char*)(fsBuffer + received), req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(fsBuffer);
            ESP_LOGE(TAG, "Failed to receive filesystem data");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    
    ESP_LOGI(TAG, "Received complete filesystem: %d bytes", received);
    
    // Flash SPIFFS
    bool success = self->otaManager.flashSPIFFSDirect(fsBuffer, received);
    
    free(fsBuffer);
    
    if (success) {
        ESP_LOGI(TAG, "Filesystem Update successful, rebooting in 3 seconds...");
        const char* response = "{\"status\":\"success\",\"message\":\"Filesystem update successful, rebooting...\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Filesystem Update failed: %s", self->otaManager.getLastError());
        char errorMsg[512];
        snprintf(errorMsg, sizeof(errorMsg), 
                 "{\"status\":\"error\",\"message\":\"%s\"}", 
                 self->otaManager.getLastError());
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, errorMsg, strlen(errorMsg));
        return ESP_FAIL;
    }
}



