#pragma once
#include "esp_http_server.h"
#include "esp_log.h"
#include "Config.h"
#include "KMeterManager.h"
#include "driver/ledc.h"

class ServerManager {
public:
    ServerManager();
    void begin();
    void handleClient();  // Empty for ESP-IDF (async server)
    void initializePWM();
    void reconfigurePWM(uint32_t frequency);
    void setPWMDuty(int duty);
    void updateSensors();
    void updateAutoPWM();
    KMeterManager* getKMeterManager() { return &kmeterManager; }
    
private:
    httpd_handle_t server;
    KMeterManager kmeterManager;
    
    void setupRoutes();
    int mapTemperatureToPWM(float temperature);
    
    // HTTP Handler functions
    static esp_err_t root_handler(httpd_req_t *req);
    static esp_err_t login_handler(httpd_req_t *req);
    static esp_err_t login_post_handler(httpd_req_t *req);
    static esp_err_t main_handler(httpd_req_t *req);
    static esp_err_t style_handler(httpd_req_t *req);
    static esp_err_t logo_handler(httpd_req_t *req);
    static esp_err_t setting_handler(httpd_req_t *req);
    
    // API endpoints
    static esp_err_t api_login_handler(httpd_req_t *req);
    static esp_err_t api_status_handler(httpd_req_t *req);
    static esp_err_t api_system_info_handler(httpd_req_t *req);
    static esp_err_t api_settings_handler(httpd_req_t *req);
    static esp_err_t api_restart_handler(httpd_req_t *req);
    static esp_err_t api_device_name_handler(httpd_req_t *req);
    static esp_err_t api_reboot_handler(httpd_req_t *req);
    static esp_err_t api_factory_reset_handler(httpd_req_t *req);
    static esp_err_t api_wifi_status_handler(httpd_req_t *req);
    static esp_err_t api_wifi_scan_handler(httpd_req_t *req);
    static esp_err_t api_wifi_connect_handler(httpd_req_t *req);
    static esp_err_t api_wifi_disconnect_handler(httpd_req_t *req);
    static esp_err_t api_wifi_clear_handler(httpd_req_t *req);
    static esp_err_t api_toggle_ap_handler(httpd_req_t *req);
    static esp_err_t api_mqtt_settings_handler(httpd_req_t *req);
    static esp_err_t api_mqtt_test_handler(httpd_req_t *req);
    static esp_err_t api_mqtt_status_handler(httpd_req_t *req);
    static esp_err_t api_manual_pwm_mode_handler(httpd_req_t *req);
    static esp_err_t api_manual_pwm_settings_handler(httpd_req_t *req);
    static esp_err_t api_temp_mapping_handler(httpd_req_t *req);
    static esp_err_t api_temp_mapping_status_handler(httpd_req_t *req);
    static esp_err_t api_kmeter_status_handler(httpd_req_t *req);
    static esp_err_t api_kmeter_config_handler(httpd_req_t *req);
    static esp_err_t api_led_toggle_handler(httpd_req_t *req);
    static esp_err_t api_change_password_handler(httpd_req_t *req);
    static esp_err_t api_logout_handler(httpd_req_t *req);
    static esp_err_t api_pwm_control_handler(httpd_req_t *req);
    static esp_err_t api_pwm_status_handler(httpd_req_t *req);
    
    // Helper functions
    static bool check_auth(httpd_req_t *req);
    static bool check_token(httpd_req_t *req);
    static char* generate_token();
    static bool is_valid_token(const char* token);
    static void send_json_response(httpd_req_t *req, const char* json);
    static void send_html_response(httpd_req_t *req, const uint8_t* html_start, const uint8_t* html_end);
};
