#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============== 设备配置 ==============
// 取消注释选择设备类型（三选一）
#define INDOOR_SENSOR  // 室内温湿度传感器
// #define OUTDOOR_SENSOR  // 室外温湿度传感器
// #define LED_CONTROLLER  // LED控制器

// 根据选择的设备类型设置参数
#ifdef INDOOR_SENSOR
  #define LOCATION "indoor"
  #define DEVICE_ID "esp01s_indoor"
#elif defined(OUTDOOR_SENSOR)
  #define LOCATION "outdoor"
  #define DEVICE_ID "esp01s_outdoor"
#elif defined(LED_CONTROLLER)
  #define LOCATION "living_room"
  #define DEVICE_ID "esp01s_led"
#endif

// ============== 网络配置 ==============
const char* WIFI_SSID = "OpenWrt";
const char* WIFI_PASS = "zf22zf79?";
const char* MQTT_SERVER = "10.0.0.224";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASS = "Zf15730790542@";

// ============== 硬件配置 ==============
#define LED_PIN 2        // GPIO2控制LED
#define DHTPIN 2         // GPIO2连接DHT22（传感器模式）
#define DHTTYPE DHT22
#define SENSOR_RETRIES 3
#define DEFAULT_TEMP 22
#define DEFAULT_HUM 70

// ============== 系统参数 ==============
#define SLEEP_MINUTES 10
// #define DEEP_SLEEP

DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// 函数声明
void setup_wifi();
void reconnect();
void sendAutoDiscovery();
bool readDHT(float &temp, float &hum);
void sendSensorData();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void updateLEDState(bool state);
#ifdef DEEP_SLEEP
void goToSleep();
#endif

// MQTT回调函数 - 处理收到的命令
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  // 将payload转为字符串
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  Serial.println(message);

  // 检查是否是LED控制命令
  String topicStr = String(topic);
  String commandTopic = "homeassistant/switch/" + String(DEVICE_ID) + "/set";
  
  if (topicStr.equals(commandTopic)) {
    if (strcmp(message, "ON") == 0) {
      updateLEDState(true);
      mqtt.publish(("homeassistant/switch/" + String(DEVICE_ID) + "/state").c_str(), "ON");
    } else if (strcmp(message, "OFF") == 0) {
      updateLEDState(false);
      mqtt.publish(("homeassistant/switch/" + String(DEVICE_ID) + "/state").c_str(), "OFF");
    }
  }
}

// 更新LED状态
void updateLEDState(bool state) {
  digitalWrite(LED_PIN, state ? LOW : HIGH); // 低电平点亮LED
  Serial.print("LED set to: ");
  Serial.println(state ? "ON" : "OFF");
}

#ifdef DEEP_SLEEP
void goToSleep() {
  Serial.println("\nEntering deep sleep for " + String(SLEEP_MINUTES) + " minutes");
  ESP.deepSleep(SLEEP_MINUTES * 60 * 1000000);
  delay(100);
}
#endif

void sendAutoDiscovery() {
  char deviceInfo[256];
  snprintf(deviceInfo, sizeof(deviceInfo),
    R"("device": {
      "identifiers": ["%s"],
      "name": "ESP8266 %s",
      "manufacturer": "ESP8266"
    })",
    DEVICE_ID, LOCATION
  );

  // 温度传感器自动发现
  #if defined(INDOOR_SENSOR) || defined(OUTDOOR_SENSOR)
  char tempConfig[512];
  snprintf(tempConfig, sizeof(tempConfig),
    R"({
      "name":"%s Temperature",
      "dev_cla":"temperature",
      "stat_t":"homeassistant/sensor/%s/state",
      "unit_of_meas":"°C",
      "val_tpl":"{{ value_json.temperature | default(22) }}",
      "uniq_id":"%s_temp",
      %s
    })",
    LOCATION, DEVICE_ID, DEVICE_ID, deviceInfo
  );
  
  char humConfig[512];
  snprintf(humConfig, sizeof(humConfig),
    R"({
      "name":"%s Humidity",
      "dev_cla":"humidity",
      "stat_t":"homeassistant/sensor/%s/state",
      "unit_of_meas":"%",
      "val_tpl":"{{ value_json.humidity | default(70) }}",
      "uniq_id":"%s_hum",
      %s
    })",
    LOCATION, DEVICE_ID, DEVICE_ID, deviceInfo
  );
  #endif

  // LED开关自动发现
  #ifdef LED_CONTROLLER
  char switchConfig[512];
  snprintf(switchConfig, sizeof(switchConfig),
    R"({
      "name":"%s LED",
      "cmd_t":"homeassistant/switch/%s/set",
      "stat_t":"homeassistant/switch/%s/state",
      "pl_off":"OFF",
      "pl_on":"ON",
      "uniq_id":"%s_switch",
      %s
    })",
    LOCATION, DEVICE_ID, DEVICE_ID, DEVICE_ID, deviceInfo
  );
  #endif

  // 发布自动发现配置
  #if defined(INDOOR_SENSOR) || defined(OUTDOOR_SENSOR)
    const char* discovTemp = ("homeassistant/sensor/" + String(DEVICE_ID) + "_temp/config").c_str();
    const char* discovHum = ("homeassistant/sensor/" + String(DEVICE_ID) + "_hum/config").c_str();
    mqtt.publish(discovTemp, tempConfig, true);
    mqtt.publish(discovHum, humConfig, true);
  #endif
  
  #ifdef LED_CONTROLLER
    const char* discovSwitch = ("homeassistant/switch/" + String(DEVICE_ID) + "/config").c_str();
    mqtt.publish(discovSwitch, switchConfig, true);
    // 初始化状态
    mqtt.publish(("homeassistant/switch/" + String(DEVICE_ID) + "/state").c_str(), "OFF");
  #endif
  
  Serial.println("Discovery sent");
}

void setup_wifi() {
  Serial.print("\nConnecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected\nIP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed");
    #ifdef DEEP_SLEEP
      goToSleep();
    #endif
  }
}

void reconnect() {
  unsigned long startTime = millis();
  while (!mqtt.connected() && millis() - startTime < 10000) {
    Serial.print("MQTT connecting...");
    
    if (mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      sendAutoDiscovery();
      
      // 订阅LED控制主题
      #ifdef LED_CONTROLLER
        String commandTopic = "homeassistant/switch/" + String(DEVICE_ID) + "/set";
        mqtt.subscribe(commandTopic.c_str());
        Serial.println("Subscribed to: " + commandTopic);
      #endif
    } else {
      Serial.print("failed (rc=");
      Serial.print(mqtt.state());
      Serial.println(") retrying...");
      delay(2000);
    }
  }
}

bool readDHT(float &temp, float &hum) {
  for (int i = 0; i < SENSOR_RETRIES; i++) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    if (!isnan(t) && !isnan(h)) {
      temp = t;
      hum = h;
      return true;
    }
    delay(200);
  }
  
  temp = DEFAULT_TEMP;
  hum = DEFAULT_HUM;
  return false;
}

void sendSensorData() {
  float temperature, humidity;
  
  bool success = readDHT(temperature, humidity);
  Serial.print(success ? "DHT read OK: " : "DHT read FAILED: ");
  Serial.print(temperature, 1);
  Serial.print("°C, ");
  Serial.print(humidity, 1);
  Serial.println("%");

  char payload[128];
  snprintf(payload, sizeof(payload), 
    "{\"temperature\":%.1f,\"humidity\":%.1f}", 
    temperature, humidity
  );

  const char* stateTopic = ("homeassistant/sensor/" + String(DEVICE_ID) + "/state").c_str();
  
  if (mqtt.publish(stateTopic, payload)) {
    Serial.println("Data sent to MQTT");
  } else {
    Serial.println("MQTT publish failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nStarting Device - " + String(LOCATION));
  
  // 根据设备类型初始化硬件
  #if defined(INDOOR_SENSOR) || defined(OUTDOOR_SENSOR)
    dht.begin();
  #endif
  
  #ifdef LED_CONTROLLER
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // 初始状态关闭
    Serial.println("LED Controller initialized");
  #endif
  
  setup_wifi();
  
  if (WiFi.status() == WL_CONNECTED) {
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(mqttCallback); // 设置回调函数
    mqtt.setBufferSize(512);
    reconnect();
    
    #if defined(INDOOR_SENSOR) || defined(OUTDOOR_SENSOR)
      if (mqtt.connected()) {
        sendSensorData();
        delay(500);
      }
    #endif
  }
  
  #ifdef DEEP_SLEEP
  goToSleep();
  #endif
}

void loop() {
  #ifndef DEEP_SLEEP
  static unsigned long lastSend = 0;
  
  if (!mqtt.connected()) {
    reconnect();
  }
  
  #if defined(INDOOR_SENSOR) || defined(OUTDOOR_SENSOR)
    if (millis() - lastSend >= SLEEP_MINUTES * 60 * 1000) {
      if (mqtt.connected()) {
        sendSensorData();
      }
      lastSend = millis();
    }
  #endif
  
  mqtt.loop();
  delay(100);
  #endif
}
