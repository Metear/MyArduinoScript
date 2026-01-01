#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>  // 使用DHT.h库替代DHTesp.h

// ============== 设备配置 ==============
// 取消注释选择设备类型（二选一）
// #define INDOOR_SENSOR  // 室内温湿度传感器
#define OUTDOOR_SENSOR  // 室外温湿度传感器

// 根据选择的设备类型设置参数
#ifdef INDOOR_SENSOR
  #define LOCATION "indoor"
  #define DEVICE_ID "esp01s_indoor"
#elif defined(OUTDOOR_SENSOR)
  #define LOCATION "outdoor"
  #define DEVICE_ID "esp01s_outdoor"
#endif

// ============== 网络配置 ==============
const char* WIFI_SSID = "OpenWrt";
const char* WIFI_PASS = "zf22zf79?";
const char* MQTT_SERVER = "10.0.0.118";
const int MQTT_PORT = 1883;

// ============== 传感器配置 ==============
#define DHTPIN 2         // GPIO2 连接DHT22
#define DHTTYPE DHT22    // DHT传感器类型
#define SENSOR_RETRIES 3 // 读取失败重试次数
#define DEFAULT_TEMP 22  // 读取失败默认温度
#define DEFAULT_HUM 70   // 读取失败默认湿度

// ============== 系统参数 ==============
#define SLEEP_MINUTES 10  // 数据发送间隔(分钟)
//#define DEEP_SLEEP     // 启用深度睡眠节省功耗(需连接GPIO16-RST)

DHT dht(DHTPIN, DHTTYPE);  // 使用DHT类初始化传感器
WiFiClient espClient;
PubSubClient mqtt(espClient);

// MQTT主题配置
const String discovTemp = "homeassistant/sensor/" + String(DEVICE_ID) + "_temp/config";
const String discovHum = "homeassistant/sensor/" + String(DEVICE_ID) + "_hum/config";
const String stateTopic = "homeassistant/sensor/" + String(DEVICE_ID) + "/state";

// 设备信息JSON
const String deviceInfo = R"rawliteral(
"device": {
  "identifiers": [")rawliteral" + String(DEVICE_ID) + R"rawliteral("],
  "name": "DHT22 )rawliteral" + String(LOCATION) + R"rawliteral(",
  "manufacturer": "ESP8266"
})rawliteral";

// 函数声明
void setup_wifi();
void reconnect();
void sendAutoDiscovery();
bool readDHT(float &temp, float &hum);
void sendSensorData();
#ifdef DEEP_SLEEP
void goToSleep();
#endif

#ifdef DEEP_SLEEP
void goToSleep() {
  Serial.println("\nEntering deep sleep for " + String(SLEEP_MINUTES) + " minutes");
  ESP.deepSleep(SLEEP_MINUTES * 60 * 1000000); // 微秒
  delay(100);  // 确保进入睡眠
}
#endif

void sendAutoDiscovery() {
  // 温度传感器自动发现配置
  String tempConfig = R"rawliteral({
    "name":")rawliteral" + String(LOCATION) + R"rawliteral( Temperature",
    "dev_cla":"temperature",
    "stat_t":")rawliteral" + stateTopic + R"rawliteral(",
    "unit_of_meas":"°C",
    "val_tpl":"{{ value_json.temperature | default(22) }}",
    "uniq_id":")rawliteral" + String(DEVICE_ID) + R"rawliteral(_temp",
    )rawliteral" + deviceInfo + R"rawliteral(
  })rawliteral";
  
  // 湿度传感器自动发现配置
  String humConfig = R"rawliteral({
    "name":")rawliteral" + String(LOCATION) + R"rawliteral( Humidity",
    "dev_cla":"humidity",
    "stat_t":")rawliteral" + stateTopic + R"rawliteral(",
    "unit_of_meas":"%",
    "val_tpl":"{{ value_json.humidity | default(70) }}",
    "uniq_id":")rawliteral" + String(DEVICE_ID) + R"rawliteral(_hum",
    )rawliteral" + deviceInfo + R"rawliteral(
  })rawliteral";

  bool pubTemp = mqtt.publish(discovTemp.c_str(), tempConfig.c_str(), true);
  bool pubHum = mqtt.publish(discovHum.c_str(), humConfig.c_str(), true);
  
  Serial.println((pubTemp && pubHum) ? 
                 "Discovery sent" : "Discovery failed");
}

void setup_wifi() {
  Serial.print("\nConnecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);  // 禁用AP模式节省内存
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  // 带超时的WiFi连接
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
      goToSleep(); // 连接失败直接休眠
    #endif
  }
}

void reconnect() {
  unsigned long startTime = millis();
  while (!mqtt.connected() && millis() - startTime < 10000) {
    Serial.print("MQTT connecting...");
    if (mqtt.connect(DEVICE_ID)) {
      Serial.println("connected");
      sendAutoDiscovery();
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
    // 使用DHT.h库的读取方法
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    // 检查读取是否有效
    if (!isnan(t) && !isnan(h)) {
      temp = t;
      hum = h;
      return true;
    }
    delay(200); // 重试前短暂延迟
  }
  
  // 所有尝试失败后使用默认值
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

  String payload = "{";
  payload += "\"temperature\":" + String(temperature, 1);
  payload += ",\"humidity\":" + String(humidity, 1);
  payload += "}";

  if (mqtt.publish(stateTopic.c_str(), payload.c_str())) {
    Serial.println("Data sent to MQTT");
  } else {
    Serial.println("MQTT publish failed");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nStarting DHT22 - " + String(LOCATION));
  
  dht.begin();  // 使用DHT.h库的初始化方法
  
  setup_wifi();
  
  if (WiFi.status() == WL_CONNECTED) {
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setBufferSize(512);  // 减小内存占用
    reconnect();
    if (mqtt.connected()) {
      sendSensorData();
      delay(500);  // 确保数据发送完成
    }
  }
  
  #ifdef DEEP_SLEEP
  goToSleep();
  #endif
}

void loop() {
  #ifndef DEEP_SLEEP
  // 非深度睡眠模式下的循环处理
  static unsigned long lastSend = 0;
  
  if (millis() - lastSend >= SLEEP_MINUTES * 60 * 1000) {
    if (!mqtt.connected()) reconnect();
    if (mqtt.connected()) sendSensorData();
    lastSend = millis();
  }
  
  mqtt.loop();
  delay(100);
  #endif
}
