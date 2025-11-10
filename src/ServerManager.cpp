#include "ServerManager.h"
#include "Config.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "LEDManager.h"
#include "KMeterManager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <Update.h>

extern LEDManager led;
extern MQTTManager mqttManager;

extern const uint8_t _binary_data_index_html_start[] asm("_binary_data_index_html_start");
extern const uint8_t _binary_data_index_html_end[]   asm("_binary_data_index_html_end");
extern const uint8_t _binary_data_style_css_start[]  asm("_binary_data_style_css_start");
extern const uint8_t _binary_data_style_css_end[]    asm("_binary_data_style_css_end");
extern const uint8_t _binary_data_login_html_start[] asm("_binary_data_login_html_start");
extern const uint8_t _binary_data_login_html_end[]   asm("_binary_data_login_html_end");
extern const uint8_t _binary_data_img_logo_png_start[]   asm("_binary_data_img_logo_png_start");
extern const uint8_t _binary_data_img_logo_png_end[]     asm("_binary_data_img_logo_png_end");
extern const uint8_t _binary_data_main_html_start[] asm("_binary_data_main_html_start");
extern const uint8_t _binary_data_main_html_end[]   asm("_binary_data_main_html_end");

String sessionToken;
static bool ledState = false;



ServerManager::ServerManager()
    : server(Config::HTTP_PORT) {}

void ServerManager::begin() {
    setupRoutes();
    initializePWM();
    
    delay(500);
    
    Serial.println("DEBUG: Initializing KMeter-ISO sensor...");
    Serial.println("DEBUG: Using alternative I2C pins to avoid conflicts with PWM");
    if (kmeterManager.begin(0x66, 26, 32)) {
        Serial.println("KMeter-ISO sensor initialized successfully");
    } else {
        Serial.println("Warning: KMeter-ISO sensor initialization failed");
        Serial.println("DEBUG: Will continue without KMeter sensor");
    }
    
    Config::saveAutoPWMEnabled(true);
    Serial.println("DEBUG: Auto-PWM enabled for fan control");
    
    server.begin();
    Serial.print("Web server started on port ");
    Serial.println(Config::HTTP_PORT);
}

void ServerManager::initializePWM() {
    Serial.println("DEBUG: Initializing PWM system...");

    uint32_t freq = Config::MANUAL_PWM_MODE ? Config::MANUAL_PWM_FREQ : 1000;
    
    if (ledcSetup(0, freq, 8)) {
        Serial.printf("DEBUG: PWM Channel 0 setup successful with %u Hz\n", freq);

        ledcAttachPin(22, 0);
        Serial.println("DEBUG: PWM Pin 22 attached to channel 0");

        ledcWrite(0, 0);
        Serial.println("DEBUG: PWM initialized - Pin 22 set to 0%");
    } else {
        Serial.println("ERROR: PWM Channel 0 setup failed");
    }
}

void ServerManager::reconfigurePWM(uint32_t frequency) {
    Serial.printf("DEBUG: Reconfiguring PWM to %u Hz...\n", frequency);
    
    // Detach pin first
    ledcDetachPin(22);
    
    // Reconfigure with new frequency
    if (ledcSetup(0, frequency, 8)) {
        Serial.printf("DEBUG: PWM Channel 0 reconfigured to %u Hz\n", frequency);
        
        // Reattach pin
        ledcAttachPin(22, 0);
        Serial.println("DEBUG: PWM Pin 22 reattached to channel 0");
        
        // Restore duty cycle if in manual mode
        if (Config::MANUAL_PWM_MODE) {
            int duty = map(Config::MANUAL_PWM_DUTY, 0, 100, 0, 255);
            ledcWrite(0, duty);
            Serial.printf("DEBUG: PWM duty restored to %u%%\n", Config::MANUAL_PWM_DUTY);
        }
    } else {
        Serial.println("ERROR: PWM reconfiguration failed");
    }
}

void ServerManager::setPWMDuty(int duty) {
    if (duty < 0 || duty > 255) {
        Serial.printf("ERROR: Invalid PWM duty cycle: %d (must be 0-255)\n", duty);
        return;
    }
    
    ledcWrite(0, duty);
    Serial.printf("PWM Pin 22 set to duty cycle %d (%.1f%%)\n", duty, (duty / 255.0) * 100);
    
    mqttManager.publishFanSpeed(duty);
}

void ServerManager::updateSensors() {
    kmeterManager.update();
    
    if (kmeterManager.isInitialized() && kmeterManager.isReady()) {
        float temp = kmeterManager.getTemperatureCelsius();
        mqttManager.publishTemperature(temp);
    }
    
    updateAutoPWM();
}

void ServerManager::handleClient() {
    server.handleClient();
}

void ServerManager::setupRoutes() {
  server.on("/", HTTP_GET, [this]() {
    server.sendHeader("Location", "/main");
    server.send(302);
  });
  server.on("/main", HTTP_GET, [this]() { handleMain(); });
  server.on("/style.css", HTTP_GET, [this]() {
    size_t len = _binary_data_style_css_end - _binary_data_style_css_start;
    server.setContentLength(len);
    server.send(200, "text/css", "");
    server.sendContent_P(reinterpret_cast<const char*>(_binary_data_style_css_start), len);
  });
  server.on("/favicon.ico", HTTP_GET, [this]() { server.send(204); });
  server.on("/index", HTTP_GET, [this]() {
    server.sendHeader("Location", "/main");
    server.send(302);
  });
  server.on("/login", HTTP_GET, [this]() { handleLogin(); });
  server.on("/login", HTTP_POST, [this]() { handleLogin(); });
  server.onNotFound([this]() { handleNotFound(); });

  server.on("/img/logo.png", HTTP_GET, [this]() {
    size_t len = _binary_data_img_logo_png_end - _binary_data_img_logo_png_start;
    server.setContentLength(len);
    server.send(200, "image/png", "");
    server.sendContent_P(reinterpret_cast<const char*>(_binary_data_img_logo_png_start), len);
  });
  server.on("/setting", HTTP_GET, [this]() { handleSetting(); });
  server.on("/wifi_connect", HTTP_POST, [this]() { handleWifiConnect(); });
  
  server.on("/api/device-name", HTTP_POST, [this]() { handleDeviceName(); });
  server.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server.on("/api/factory-reset", HTTP_POST, [this]() { handleFactoryReset(); });
  server.on("/api/change-password", HTTP_POST, [this]() { handleChangePassword(); });
  server.on("/api/temp-unit", HTTP_POST, [this]() { handleTempUnit(); });
  server.on("/api/toggle-ap", HTTP_POST, [this]() { handleToggleAP(); });
  server.on("/api/mqtt-settings", HTTP_POST, [this]() { handleMQTTSettings(); });
  server.on("/api/mqtt-test", HTTP_POST, [this]() { handleMQTTTest(); });
  server.on("/api/mqtt-status", HTTP_GET, [this]() { handleMQTTStatus(); });
  server.on("/api/wifi-status", HTTP_GET, [this]() { handleWiFiStatus(); });
  server.on("/api/wifi-scan", HTTP_GET, [this]() { handleWiFiScan(); });
  server.on("/api/wifi-disconnect", HTTP_POST, [this]() { handleWiFiDisconnect(); });
  server.on("/api/wifi-clear", HTTP_POST, [this]() { handleWiFiClear(); });
  server.on("/api/led-toggle", HTTP_POST, [this]() { handleLEDToggle(); });
  server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server.on("/api/settings", HTTP_GET, [this]() { handleGetSettings(); });
  server.on("/api/pwm-control", HTTP_POST, [this]() { 
    Serial.println("DEBUG: PWM Control route triggered");
    handlePWMControl(); 
  });
  server.on("/api/pwm-status", HTTP_GET, [this]() { 
    Serial.println("DEBUG: PWM Status route triggered");
    handlePWMStatus(); 
  });
  server.on("/api/system-info", HTTP_GET, [this]() { 
    Serial.println("DEBUG: System Info route triggered");
    handleSystemInfo(); 
  });
  server.on("/api/kmeter-status", HTTP_GET, [this]() { 
    Serial.println("DEBUG: KMeter Status route triggered");
    handleKMeterStatus(); 
  });
  server.on("/api/kmeter-config", HTTP_POST, [this]() { 
    Serial.println("DEBUG: KMeter Config route triggered");
    handleKMeterConfig(); 
  });
  server.on("/api/temp-mapping", HTTP_POST, [this]() { 
    Serial.println("DEBUG: Temperature Mapping route triggered");
    handleTempMapping(); 
  });
  server.on("/api/temp-mapping-status", HTTP_GET, [this]() { 
    Serial.println("DEBUG: Temperature Mapping Status route triggered");
    handleTempMappingStatus(); 
  });
  server.on("/api/auto-pwm", HTTP_POST, [this]() { 
    Serial.println("DEBUG: Auto PWM route triggered");
    handleAutoPWM(); 
  });
  server.on("/api/manual-pwm-mode", HTTP_POST, [this]() { 
    Serial.println("DEBUG: Manual PWM Mode route triggered");
    handleManualPWMMode(); 
  });
  server.on("/api/manual-pwm-settings", HTTP_POST, [this]() { 
    Serial.println("DEBUG: Manual PWM Settings route triggered");
    handleManualPWMSettings(); 
  });
  
  server.on("/api/firmware-update", HTTP_POST, 
    [this]() { 
    },
    [this]() { 
      // This is called during upload process
      handleFirmwareUpdate(); 
    }
  );
}

void ServerManager::handleRoot() {
    if (!isAuthenticated()) {
        server.sendHeader("Location", "/login");
        server.send(302);
        return;
    }
    String html(reinterpret_cast<const char*>(_binary_data_index_html_start),
                _binary_data_index_html_end - _binary_data_index_html_start);

    String pageContent(reinterpret_cast<const char*>(_binary_data_main_html_start),
                       _binary_data_main_html_end - _binary_data_main_html_start);

    html.replace("%CONTENT%", pageContent);
    html.replace("%DEVICE_NAME%", Config::DEVICE_NAME);
    html.replace("%TOKEN%", sessionToken);
    html.replace("%HOME_ACTIVE%", "active");
    html.replace("%SETTING_ACTIVE%", "");

    server.setContentLength(html.length());
    server.send(200, "text/html", html);
}

void ServerManager::handleLogin() {
  Serial.println("Login handler called");
  
  if (server.method() == HTTP_POST) {
    Serial.println("POST request received");
    String password = server.arg("password");
    
    if (password == Config::WEB_PASSWORD) {
      Serial.println("Password correct - redirecting");
      sessionToken = String(random(0xffff), HEX);
      
      server.sendHeader("Location", "/main?token=" + sessionToken);
      server.send(302);
      return;
    } else {
      Serial.println("Password incorrect");
    }
  } else {
    Serial.println("GET request - showing login page");
  }
  
  Serial.println("Sending login page");
  size_t len = _binary_data_login_html_end - _binary_data_login_html_start;
  server.setContentLength(len);
  server.send(200, "text/html", "");
  server.sendContent_P(reinterpret_cast<const char*>(_binary_data_login_html_start), len);
}

void ServerManager::handleNotFound() {
    Serial.println("404 - Page not found: " + server.uri());
    server.send(404, "text/plain", "Nicht gefunden");
}

void ServerManager::handleMain() {
    if (!isAuthenticated()) {
        server.sendHeader("Location", "/login");
        server.send(302);
        return;
    }
    
    String html(reinterpret_cast<const char*>(_binary_data_main_html_start),
                _binary_data_main_html_end - _binary_data_main_html_start);

    html.replace("%DEVICE_NAME%", Config::DEVICE_NAME);
    html.replace("%TOKEN%", sessionToken);

    server.setContentLength(html.length());
    server.send(200, "text/html", html);
}

bool ServerManager::isAuthenticated() {
    if (server.hasArg("token")) {
        return server.arg("token") == sessionToken;
    }
    return false;
}

void ServerManager::handleSetting() {
    Serial.println("DEBUG: handleSetting() called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: handleSetting() - Authentication failed, redirecting to login");
        server.sendHeader("Location", "/login");
        server.send(302);
        return;
    }
    
    Serial.println("DEBUG: handleSetting() - Authentication successful");
    
    extern const uint8_t _binary_data_setting_html_start[] asm("_binary_data_setting_html_start");
    extern const uint8_t _binary_data_setting_html_end[]   asm("_binary_data_setting_html_end");
    
    size_t htmlSize = _binary_data_setting_html_end - _binary_data_setting_html_start;
    Serial.printf("DEBUG: setting.html binary size: %d bytes\n", htmlSize);
    
    const char* htmlData = reinterpret_cast<const char*>(_binary_data_setting_html_start);
    
    server.setContentLength(htmlSize);
    server.send_P(200, "text/html", htmlData, htmlSize);
    
    Serial.println("DEBUG: HTML response sent directly");
}

void ServerManager::handleWifiConnect() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    Preferences prefs;
    if (prefs.begin("wifi", false)) {
        prefs.putString("ssid", ssid);
        prefs.putString("password", password);
        prefs.end();
        Serial.println("WiFi credentials saved");
    } else {
        Serial.println("Error: Failed to save WiFi credentials");
        server.send(500, "text/plain", "Failed to save credentials");
        return;
    }

    wifi.beginSTA(ssid.c_str(), password.c_str());
    server.send(200, "text/plain", "OK");
}

void ServerManager::handleDeviceName() {
    if (!isAuthenticated()) {
        server.send(401, "application/json", "{\"success\":false,\"error\":\"Unauthorized\"}");
        return;
    }
    String deviceName = server.arg("name");
    if (deviceName.length() > 0 && deviceName.length() < 32) {
        Config::saveDeviceName(deviceName.c_str());
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Device name saved\"}");
    } else {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid device name\"}");
    }
}

void ServerManager::handleReboot() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
}

void ServerManager::handleFactoryReset() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    Config::factoryReset();
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
}

void ServerManager::handleChangePassword() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    String currentPassword = server.arg("current_password");
    String newPassword = server.arg("new_password");
    
    if (currentPassword != Config::WEB_PASSWORD) {
        server.send(200, "text/plain", "INVALID");
        return;
    }
    
    if (newPassword.length() < 6 || newPassword.length() >= 32) {
        server.send(400, "text/plain", "Password must be 6-31 characters");
        return;
    }
    
    if (newPassword == currentPassword) {
        server.send(400, "text/plain", "New password must be different");
        return;
    }
    
    Config::saveWebPassword(newPassword.c_str());
    sessionToken = "";
    server.send(200, "text/plain", "OK");
}

void ServerManager::handleTempUnit() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    String tempUnit = server.arg("unit");
    if (tempUnit == "celsius" || tempUnit == "fahrenheit") {
        Config::saveTempUnit(tempUnit.c_str());
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Invalid temperature unit");
    }
}

void ServerManager::handleToggleAP() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    String enable = server.arg("enable");
    bool apEnabled = (enable == "1");
    Config::saveAPEnabled(apEnabled);
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
}

void ServerManager::handleMQTTSettings() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    String server_arg = server.arg("server");
    String port_arg = server.arg("port");
    String user_arg = server.arg("user");
    String pass_arg = server.arg("pass");
    String topic_arg = server.arg("topic");
    
    if (server_arg.length() > 0 && port_arg.toInt() > 0) {
        uint16_t port = port_arg.toInt();
        
        mqttManager.disconnect();
        
        Config::saveMQTTSettings(server_arg.c_str(), port, user_arg.c_str(), pass_arg.c_str(), topic_arg.c_str());
        
        mqttManager.begin();
        
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Invalid MQTT settings");
    }
}

void ServerManager::handleMQTTTest() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    // Use current MQTT settings for test
    String testServer = String(Config::MQTT_SERVER);
    int testPort = Config::MQTT_PORT;
    String testUser = String(Config::MQTT_USER);
    String testPass = String(Config::MQTT_PASS);
    
    if (testServer.length() == 0 || testPort <= 0) {
        server.send(200, "application/json", "{\"success\":false,\"error\":\"No MQTT server configured\"}");
        return;
    }
    
    // Test MQTT connection
    WiFiClient testClient;
    PubSubClient testMqtt(testClient);
    
    testMqtt.setServer(testServer.c_str(), testPort);
    
    String clientId = "ESP32-Test-" + String(random(0xffff), HEX);
    bool connected;
    
    if (testUser.length() > 0) {
        connected = testMqtt.connect(clientId.c_str(), testUser.c_str(), testPass.c_str());
    } else {
        connected = testMqtt.connect(clientId.c_str());
    }
    
    if (connected) {
        testMqtt.disconnect();
        server.send(200, "application/json", "{\"success\":true}");
    } else {
        String error = "Connection failed (Code: " + String(testMqtt.state()) + ")";
        server.send(200, "application/json", "{\"success\":false,\"error\":\"" + error + "\"}");
    }
}

void ServerManager::handleWiFiStatus() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    String json = "{";
    if (WiFi.status() == WL_CONNECTED) {
        json += "\"connected\":true,";
        json += "\"ssid\":\"" + WiFi.SSID() + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"mac\":\"" + WiFi.macAddress() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"status\":\"Connected (RSSI: " + String(WiFi.RSSI()) + "dBm)\"";
    } else {
        json += "\"connected\":false,";
        json += "\"ssid\":\"-\",";
        json += "\"ip\":\"-\",";
        json += "\"mac\":\"" + WiFi.macAddress() + "\",";
        json += "\"rssi\":-100,";
        json += "\"status\":\"Not connected\"";
    }
    json += "}";
    
    server.send(200, "application/json", json);
}

void ServerManager::handleWiFiScan() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    int n = WiFi.scanNetworks();
    String json = "{\"networks\":[";
    
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"channel\":" + String(WiFi.channel(i)) + ",";
        json += "\"encryption\":" + String(WiFi.encryptionType(i)) + ",";
        json += "\"selected\":" + String(WiFi.SSID(i) == WiFi.SSID() ? "true" : "false");
        json += "}";
    }
    
    json += "]}";
    server.send(200, "application/json", json);
}

void ServerManager::handleWiFiDisconnect() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    WiFi.disconnect();
    server.send(200, "text/plain", "OK");
}

void ServerManager::handleWiFiClear() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    // Disconnect from WiFi
    WiFi.disconnect();
    
    // Clear saved WiFi credentials from preferences
    Preferences prefs;
    if (prefs.begin("wifi", false)) {
        prefs.clear();
        prefs.end();
        Serial.println("WiFi credentials cleared from preferences");
        server.send(200, "text/plain", "OK");
    } else {
        Serial.println("Error: Failed to clear WiFi preferences");
        server.send(500, "text/plain", "Failed to clear credentials");
    }
}

void ServerManager::handleMQTTStatus() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    String json = "{";
    if (mqttManager.isConnected()) {
        json += "\"connected\":true,";
        json += "\"status\":\"Connected to " + String(Config::MQTT_SERVER) + "\"";
    } else {
        json += "\"connected\":false,";
        if (strlen(Config::MQTT_SERVER) == 0) {
            json += "\"status\":\"No MQTT server configured\"";
        } else {
            json += "\"status\":\"Not connected to " + String(Config::MQTT_SERVER) + "\"";
        }
    }
    json += "}";
    
    server.send(200, "application/json", json);
}

void ServerManager::handleLEDToggle() {
    if (!isAuthenticated()) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    // Check if state parameter is provided
    if (server.hasArg("state")) {
        String state = server.arg("state");
        ledState = (state == "1");
    } else {
        // If no state parameter, toggle current state
        ledState = !ledState;
    }
    
    if (ledState) {
        led.setColor(CRGB::White);  // LED an (weiß)
    } else {
        led.setColor(CRGB::Black); // LED aus
    }
    
    server.send(200, "text/plain", "OK");
}

void ServerManager::handleStatus() {
    unsigned long uptimeSeconds = millis() / 1000;
    unsigned long days = uptimeSeconds / 86400;
    unsigned long hours = (uptimeSeconds % 86400) / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;
    unsigned long seconds = uptimeSeconds % 60;
    
    String uptimeStr = "";
    if (days > 0) {
        uptimeStr += String(days) + "d ";
    }
    if (hours > 0 || days > 0) {
        uptimeStr += String(hours) + "h ";
    }
    if (minutes > 0 || hours > 0 || days > 0) {
        uptimeStr += String(minutes) + "m ";
    }
    uptimeStr += String(seconds) + "s";
    
    String status = "{";
    status += "\"uptime\":\"" + uptimeStr + "\",";
    status += "\"free_memory\":" + String(ESP.getFreeHeap() / 1024) + ","; // in KB
    status += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    status += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
    status += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
    status += "\"mqtt_connected\":" + String(mqttManager.isConnected() ? "true" : "false") + ",";
    status += "\"led_state\":" + String(ledState ? "true" : "false");
    status += "}";
    server.send(200, "application/json", status);
}

void ServerManager::handleGetSettings() {
    String settings = "{";
    settings += "\"device_name\":\"" + String(Config::DEVICE_NAME) + "\",";
    settings += "\"mqtt_server\":\"" + String(Config::MQTT_SERVER) + "\",";
    settings += "\"mqtt_port\":" + String(Config::MQTT_PORT) + ",";
    settings += "\"mqtt_user\":\"" + String(Config::MQTT_USER) + "\",";
    // Mask password for security - show asterisks if password exists
    String maskedPassword = "";
    if (strlen(Config::MQTT_PASS) > 0) {
        for (int i = 0; i < strlen(Config::MQTT_PASS); i++) {
            maskedPassword += "*";
        }
    }
    settings += "\"mqtt_pass\":\"" + maskedPassword + "\",";
    settings += "\"ap_enabled\":" + String(Config::AP_ENABLED ? "true" : "false") + ",";
    settings += "\"temp_unit\":\"" + String(Config::TEMP_UNIT) + "\",";
    settings += "\"last_password_change\":\"" + Config::getLastPasswordChange() + "\",";
    settings += "\"firmware_version\":\"" + String(Config::FIRMWARE_VERSION) + "\",";
    settings += "\"mqtt_connected\":" + String(mqttManager.isConnected() ? "true" : "false") + ",";
    settings += "\"led_state\":" + String(ledState ? "true" : "false");
    settings += "}";
    server.send(200, "application/json", settings);
}

void ServerManager::handleFirmwareUpdate() {
    // Check authentication - for uploads, we need to check if token exists in args
    bool authenticated = false;
    if (server.hasArg("token")) {
        authenticated = (server.arg("token") == sessionToken);
    }
    
    if (!authenticated) {
        Serial.println("Firmware update: Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("Firmware update request received (authenticated)");
    
    HTTPUpload& upload = server.upload();
    
    static bool updateStarted = false;
    static size_t totalBytes = 0;
    
    if (upload.status == UPLOAD_FILE_START) {
        updateStarted = false;
        totalBytes = 0;
        
        Serial.printf("Firmware update started: %s\n", upload.filename.c_str());
        
        // Validate file extension
        if (!upload.filename.endsWith(".bin")) {
            Serial.println("Invalid file extension");
            return;
        }
        
        Serial.println("Starting OTA update...");
        
        // Start OTA update with maximum possible size
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
            Serial.println("OTA update begin failed");
            return;
        }
        
        updateStarted = true;
        Serial.println("OTA update started successfully");
        
    } else if (upload.status == UPLOAD_FILE_WRITE && updateStarted) {
        // Write firmware data
        size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
            Update.printError(Serial);
            Serial.printf("OTA write failed: expected %u, written %u\n", upload.currentSize, written);
            updateStarted = false;
            return;
        }
        
        totalBytes += upload.currentSize;
        Serial.printf("Written: %u bytes (total: %u)\n", upload.currentSize, totalBytes);
        
    } else if (upload.status == UPLOAD_FILE_END) {
        if (updateStarted && Update.end(true)) {
            Serial.printf("Firmware update successful: %u bytes\n", totalBytes);
            server.send(200, "text/plain", "OK");
            
            Serial.println("Restarting device in 2 seconds...");
            delay(2000);
            ESP.restart();
        } else {
            if (updateStarted) {
                Update.printError(Serial);
                Serial.println("OTA update end failed");
            }
            server.send(500, "text/plain", "Update failed");
        }
        updateStarted = false;
        
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (updateStarted) {
            Update.end();
            Serial.println("Firmware update aborted");
            updateStarted = false;
        }
    }
}

// PWM Control Handler
void ServerManager::handleSystemInfo() {
    if (!isAuthenticated()) {
        server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    // Erstelle JSON mit System-Informationen für Platzhalter
    String json = "{";
    json += "\"device_name\":\"" + String(Config::DEVICE_NAME) + "\",";
    json += "\"wifi_ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"wifi_ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"wifi_mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"wifi_rssi\":\"" + String(WiFi.RSSI()) + "\"";
    json += "}";
    
    Serial.println("DEBUG: System info JSON: " + json);
    server.send(200, "application/json", json);
}

void ServerManager::handlePWMControl() {
    Serial.println("DEBUG: PWM Control Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: PWM Control - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: PWM Control - Authentication successful");
    
    if (server.hasArg("duty")) {
        int duty = server.arg("duty").toInt();
        
        Serial.printf("DEBUG: PWM Control - Received duty=%d\n", duty);
        
        // Validate duty cycle (0-255)
        if (duty < 0 || duty > 255) {
            Serial.printf("DEBUG: PWM Control - Invalid duty cycle: %d\n", duty);
            server.send(400, "text/plain", "Invalid duty cycle");
            return;
        }
        
        Serial.printf("DEBUG: PWM Control - Setting PWM Pin 22 to duty %d\n", duty);
        
        // PWM-Wert über zentrale Funktion setzen
        setPWMDuty(duty);
        
        Serial.println("DEBUG: PWM Control - Sending OK response");
        server.send(200, "text/plain", "OK");
    } else {
        Serial.println("DEBUG: PWM Control - Missing parameters");
        Serial.printf("DEBUG: PWM Control - Args count: %d\n", server.args());
        for (int i = 0; i < server.args(); i++) {
            Serial.printf("DEBUG: PWM Control - Arg %d: %s = %s\n", i, server.argName(i).c_str(), server.arg(i).c_str());
        }
        server.send(400, "text/plain", "Missing parameters");
    }
}

// PWM Status Handler
void ServerManager::handlePWMStatus() {
    Serial.println("DEBUG: PWM Status Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: PWM Status - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: PWM Status - Authentication successful");
    Serial.println("DEBUG: PWM Status - Building JSON response");
    
    int currentDuty = ledcRead(0);
    float dutyCyclePercent = (currentDuty / 255.0) * 100.0;
    
    String json = "{";
    json += "\"pwm_pin\": 22,";
    json += "\"duty_raw\": " + String(currentDuty) + ",";
    json += "\"duty_percent\": " + String(dutyCyclePercent, 1) + ",";
    json += "\"manual_mode\": " + String(Config::MANUAL_PWM_MODE ? "true" : "false") + ",";
    json += "\"manual_freq\": " + String(Config::MANUAL_PWM_FREQ) + ",";
    json += "\"manual_duty\": " + String(Config::MANUAL_PWM_DUTY);
    json += "}";
    
    Serial.printf("DEBUG: PWM Status - Pin 22, Duty %d (%.1f%%), Mode: %s\n", 
                  currentDuty, dutyCyclePercent, Config::MANUAL_PWM_MODE ? "Manual" : "Auto");
    Serial.printf("DEBUG: PWM Status - JSON: %s\n", json.c_str());
    server.send(200, "application/json", json);
}

// KMeter Status Handler
void ServerManager::handleKMeterStatus() {
    Serial.println("DEBUG: KMeter Status Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: KMeter Status - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: KMeter Status - Authentication successful");
    
    // Update sensor vor dem Lesen
    kmeterManager.update();
    
    String json = "{";
    json += "\"initialized\": " + String(kmeterManager.isInitialized() ? "true" : "false") + ",";
    json += "\"ready\": " + String(kmeterManager.isReady() ? "true" : "false") + ",";
    json += "\"temperature_celsius\": " + String(kmeterManager.getTemperatureCelsius(), 2) + ",";
    json += "\"temperature_fahrenheit\": " + String(kmeterManager.getTemperatureFahrenheit(), 2) + ",";
    json += "\"internal_temperature\": " + String(kmeterManager.getInternalTemperature(), 2) + ",";
    json += "\"error_status\": " + String(kmeterManager.getErrorStatus()) + ",";
    json += "\"status_string\": \"" + kmeterManager.getStatusString() + "\",";
    json += "\"i2c_address\": \"0x" + String(kmeterManager.getI2CAddress(), HEX) + "\",";
    json += "\"read_interval\": " + String(kmeterManager.getReadInterval());
    json += "}";
    
    Serial.printf("DEBUG: KMeter Status - JSON: %s\n", json.c_str());
    server.send(200, "application/json", json);
}

// KMeter Configuration Handler
void ServerManager::handleKMeterConfig() {
    Serial.println("DEBUG: KMeter Config Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: KMeter Config - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: KMeter Config - Authentication successful");
    
    if (server.hasArg("read_interval")) {
        unsigned long interval = server.arg("read_interval").toInt();
        if (interval >= 5000 && interval <= 300000) { // 5s bis 5min
            kmeterManager.setReadInterval(interval);
            Serial.printf("KMeter read interval set to %lu ms\n", interval);
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "Invalid interval (5s-5min)");
        }
    } else if (server.hasArg("i2c_address")) {
        String addrStr = server.arg("i2c_address");
        uint8_t addr = (uint8_t)strtol(addrStr.c_str(), NULL, 16);
        
        if (addr >= 0x08 && addr <= 0x77) { // Gültige I2C-Adressen
            if (kmeterManager.setI2CAddress(addr)) {
                server.send(200, "text/plain", "OK");
            } else {
                server.send(500, "text/plain", "Failed to change I2C address");
            }
        } else {
            server.send(400, "text/plain", "Invalid I2C address");
        }
    } else {
        server.send(400, "text/plain", "Missing parameters");
    }
}

// Temperature Mapping Handler
void ServerManager::handleTempMapping() {
    Serial.println("DEBUG: Temperature Mapping Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: Temperature Mapping - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: Temperature Mapping - Authentication successful");
    
    if (server.hasArg("startTemp") && server.hasArg("maxTemp")) {
        float startTemp = server.arg("startTemp").toFloat();
        float maxTemp = server.arg("maxTemp").toFloat();
        
        Serial.printf("DEBUG: Temperature Mapping - Received startTemp=%.1f, maxTemp=%.1f\n", startTemp, maxTemp);
        
        // Validate temperature values
        if (startTemp < 0 || startTemp > 70 || maxTemp < 0 || maxTemp > 70) {
            Serial.println("DEBUG: Temperature Mapping - Invalid temperature range");
            server.send(400, "text/plain", "Invalid temperature range");
            return;
        }
        
        if (startTemp >= maxTemp) {
            Serial.println("DEBUG: Temperature Mapping - Start temperature must be lower than max temperature");
            server.send(400, "text/plain", "Start temperature must be lower than max temperature");
            return;
        }
        
        // Save settings
        Config::saveTempMapping(startTemp, maxTemp);
        
        Serial.printf("DEBUG: Temperature Mapping - Saved startTemp=%.1f°C, maxTemp=%.1f°C\n", startTemp, maxTemp);
        server.send(200, "text/plain", "OK");
    } else {
        Serial.println("DEBUG: Temperature Mapping - Missing parameters");
        server.send(400, "text/plain", "Missing parameters");
    }
}

// Temperature Mapping Status Handler
void ServerManager::handleTempMappingStatus() {
    Serial.println("DEBUG: Temperature Mapping Status Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: Temperature Mapping Status - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: Temperature Mapping Status - Authentication successful");
    
    String json = "{";
    json += "\"startTemp\": " + String(Config::TEMP_START, 1) + ",";
    json += "\"maxTemp\": " + String(Config::TEMP_MAX, 1) + ",";
    json += "\"autoEnabled\": " + String(Config::AUTO_PWM_ENABLED ? "true" : "false");
    json += "}";
    
    Serial.printf("DEBUG: Temperature Mapping Status - JSON: %s\n", json.c_str());
    server.send(200, "application/json", json);
}

// Auto PWM Handler
void ServerManager::handleAutoPWM() {
    Serial.println("DEBUG: Auto PWM Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: Auto PWM - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: Auto PWM - Authentication successful");
    
    if (server.hasArg("enabled")) {
        bool enabled = server.arg("enabled").toInt() == 1;
        
        Serial.printf("DEBUG: Auto PWM - Setting enabled=%s\n", enabled ? "true" : "false");
        
        // Save setting
        Config::saveAutoPWMEnabled(enabled);
        
        if (enabled) {
            // If auto PWM is enabled, immediately update PWM based on current temperature
            updateAutoPWM();
        }
        
        Serial.printf("DEBUG: Auto PWM - Saved enabled=%s\n", enabled ? "true" : "false");
        server.send(200, "text/plain", "OK");
    } else {
        Serial.println("DEBUG: Auto PWM - Missing parameters");
        server.send(400, "text/plain", "Missing parameters");
    }
}

// Manual PWM Mode Handler
void ServerManager::handleManualPWMMode() {
    Serial.println("DEBUG: Manual PWM Mode Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: Manual PWM Mode - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: Manual PWM Mode - Authentication successful");
    
    if (server.hasArg("mode")) {
        bool manualMode = server.arg("mode") == "manual";
        
        Serial.printf("DEBUG: Manual PWM Mode - Setting mode=%s\n", manualMode ? "manual" : "auto");
        
        // Save setting
        Config::saveManualPWMMode(manualMode);
        
        // If switching to auto mode, trigger auto PWM update
        if (!manualMode) {
            Config::saveAutoPWMEnabled(true);
            updateAutoPWM();
        }
        
        Serial.printf("DEBUG: Manual PWM Mode - Saved mode=%s\n", manualMode ? "manual" : "auto");
        server.send(200, "text/plain", "OK");
    } else {
        Serial.println("DEBUG: Manual PWM Mode - Missing parameters");
        server.send(400, "text/plain", "Missing parameters");
    }
}

// Manual PWM Settings Handler
void ServerManager::handleManualPWMSettings() {
    Serial.println("DEBUG: Manual PWM Settings Handler called");
    
    if (!isAuthenticated()) {
        Serial.println("DEBUG: Manual PWM Settings - Authentication failed");
        server.send(401, "text/plain", "Unauthorized");
        return;
    }
    
    Serial.println("DEBUG: Manual PWM Settings - Authentication successful");
    
    if (server.hasArg("frequency") && server.hasArg("duty")) {
        uint32_t frequency = server.arg("frequency").toInt();
        uint8_t dutyCycle = server.arg("duty").toInt();
        
        // Validate inputs
        if (frequency < 100 || frequency > 40000) {
            Serial.println("ERROR: Invalid frequency (100-40000 Hz)");
            server.send(400, "text/plain", "Invalid frequency (100-40000 Hz)");
            return;
        }
        
        if (dutyCycle > 100) {
            Serial.println("ERROR: Invalid duty cycle (0-100%)");
            server.send(400, "text/plain", "Invalid duty cycle (0-100%)");
            return;
        }
        
        Serial.printf("DEBUG: Manual PWM Settings - Freq=%u Hz, Duty=%u%%\n", frequency, dutyCycle);
        
        // Save settings
        Config::saveManualPWMSettings(frequency, dutyCycle);
        
        // Apply frequency change
        reconfigurePWM(frequency);
        
        // Apply duty cycle (convert % to 0-255)
        int duty255 = map(dutyCycle, 0, 100, 0, 255);
        setPWMDuty(duty255);
        
        Serial.printf("DEBUG: Manual PWM Settings - Applied Freq=%u Hz, Duty=%u%% (raw=%d)\n", 
                      frequency, dutyCycle, duty255);
        server.send(200, "text/plain", "OK");
    } else {
        Serial.println("DEBUG: Manual PWM Settings - Missing parameters");
        server.send(400, "text/plain", "Missing parameters");
    }
}

// Map temperature to PWM duty cycle (1-255)
int ServerManager::mapTemperatureToPWM(float temperature) {
    // If temperature is below start temperature, return 0 (off)
    if (temperature <= Config::TEMP_START) {
        return 0;
    }
    
    // If temperature is above max temperature, return 255 (full power)
    if (temperature >= Config::TEMP_MAX) {
        return 255;
    }
    
    // Map temperature between start and max to PWM range 1-255
    // Using Arduino map function: map(value, fromLow, fromHigh, toLow, toHigh)
    int pwmValue = map((int)(temperature * 10), 
                      (int)(Config::TEMP_START * 10), 
                      (int)(Config::TEMP_MAX * 10), 
                      25, 255);
    
    return pwmValue;
}

// Update PWM based on current temperature (called from auto PWM mode)
void ServerManager::updateAutoPWM() {
    // Skip if in manual mode
    if (Config::MANUAL_PWM_MODE) {
        return; // Manual mode active
    }
    
    if (!Config::AUTO_PWM_ENABLED) {
        return; // Auto PWM is disabled
    }
    
    if (!kmeterManager.isInitialized() || !kmeterManager.isReady()) {
        Serial.println("WARNING: Auto PWM - KMeter sensor not ready");
        return;
    }
    
    float currentTemp = kmeterManager.getTemperatureCelsius();
    int pwmValue = mapTemperatureToPWM(currentTemp);
    
    Serial.printf("Auto PWM: Temperature=%.1f°C -> PWM=%d\n", currentTemp, pwmValue);
    
    // Set PWM value
    setPWMDuty(pwmValue);
}
