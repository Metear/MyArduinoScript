#include <WiFi.h>
#include <PubSubClient.h>

// ===== 网络配置 =====
const char* WIFI_SSID = "OpenWrt";
const char* WIFI_PASS = "zf22zf79?";
const char* MQTT_SERVER = "10.0.0.224";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "admin";
const char* MQTT_PASS = "Zf15730790542@";

// ===== LED 配置 =====
const int LED_PIN = 2;   // GPIO2 连接 LED
#define LED_ON HIGH     // 根据硬件调整
#define LED_OFF LOW

// ===== MQTT 主题配置 =====
const char* discovTopic = "homeassistant/switch/esp32_led/config";
const char* stateTopic = "home/esp32/led/state";
const char* commandTopic = "home/esp32/led/set";

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool ledState = false;

// 函数声明
void setup_wifi();
void reconnect();
void sendAutoDiscovery();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void updateLedState(bool state);
void sendStateUpdate();

void setup() {
  Serial.begin(115200);
  delay(2000); // 确保串口稳定
  
  Serial.println("\n\n=== ESP32 LED Controller (Stable) ===");
  
  // 初始化 LED 引脚
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
  ledState = false;
  
  // 连接 WiFi - 使用已验证的连接代码
  setup_wifi();
  
  // 配置 MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512); // 设置足够大的缓冲区
  
  // 连接 MQTT
  reconnect();
}

void loop() {
  if (!mqtt.connected()) {
    reconnect();
  }
  mqtt.loop();
  
  // 定期发送心跳
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 30000) {
    lastHeartbeat = millis();
    sendStateUpdate();
    Serial.println("Heartbeat sent");
  }
  
  delay(10);
}

// 使用已验证的 WiFi 连接代码
void setup_wifi() {
  Serial.println("\nSetting up WiFi...");
  
  Serial.print("Connecting to: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  // 等待连接
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retryCount++;
    
    if (retryCount > 40) { // 20秒后超时
      Serial.println("\nWiFi connection failed! Restarting...");
      delay(2000);
      ESP.restart();
    }
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  Serial.println("\nConnecting to MQTT...");
  
  if (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection... ");
    
    // 尝试连接
    if (mqtt.connect("esp32_led_controller", MQTT_USER, MQTT_PASS)) {
      Serial.println("connected!");
      
      // 订阅命令主题
      if (mqtt.subscribe(commandTopic)) {
        Serial.println("Subscribed to command topic");
      }
      
      // 发送自动发现配置
      sendAutoDiscovery();
      
      // 发送初始状态
      sendStateUpdate();
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

// 发送自动发现配置
void sendAutoDiscovery() {
  Serial.println("\nSending auto-discovery config...");
  
  // 构建自动发现消息
  String discoveryMsg = "{";
  discoveryMsg += "\"name\":\"ESP32 LED Controller\",";
  discoveryMsg += "\"unique_id\":\"esp32_led_switch\",";
  discoveryMsg += "\"command_topic\":\"" + String(commandTopic) + "\",";
  discoveryMsg += "\"state_topic\":\"" + String(stateTopic) + "\",";
  discoveryMsg += "\"payload_on\":\"ON\",";
  discoveryMsg += "\"payload_off\":\"OFF\",";
  discoveryMsg += "\"state_on\":\"ON\",";
  discoveryMsg += "\"state_off\":\"OFF\",";
  discoveryMsg += "\"availability_topic\":\"" + String(stateTopic) + "\",";
  discoveryMsg += "\"payload_available\":\"online\",";
  discoveryMsg += "\"payload_not_available\":\"offline\",";
  discoveryMsg += "\"device\":{";
  discoveryMsg += "\"identifiers\":[\"esp32_led_controller\"],";
  discoveryMsg += "\"name\":\"ESP32 LED Controller\",";
  discoveryMsg += "\"manufacturer\":\"ESP32\",";
  discoveryMsg += "\"model\":\"LED Controller\"";
  discoveryMsg += "}";
  discoveryMsg += "}";
  
  // 发布自动发现消息
  if (mqtt.publish(discovTopic, discoveryMsg.c_str(), true)) {
    Serial.println("Auto-discovery sent successfully!");
    Serial.println("Discovery message:");
    Serial.println(discoveryMsg);
  } else {
    Serial.println("Failed to send auto-discovery");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // 创建消息缓冲区
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.print("\nMessage received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // 处理命令
  if (strcmp(topic, commandTopic) == 0) {
    if (strcmp(message, "ON") == 0) {
      updateLedState(true);
    } else if (strcmp(message, "OFF") == 0) {
      updateLedState(false);
    }
  }
}

void updateLedState(bool state) {
  ledState = state;
  digitalWrite(LED_PIN, state ? LED_ON : LED_OFF);
  Serial.println("LED turned " + String(state ? "ON" : "OFF"));
  
  // 更新状态
  sendStateUpdate();
}

void sendStateUpdate() {
  const char* payload = ledState ? "ON" : "OFF";
  
  if (mqtt.publish(stateTopic, payload, true)) {
    Serial.println("State update sent: " + String(payload));
  } else {
    Serial.println("Failed to send state update");
  }
}
