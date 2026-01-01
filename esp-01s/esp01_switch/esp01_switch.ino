#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Servo.h>

// ============== 设备配置 ==============
// 取消注释选择设备类型（三选一）
#define INDOOR_SENSOR  // 室内温湿度传感器
//#define OUTDOOR_SENSOR  // 室外温湿度传感器
//#define LED_CONTROLLER  // LED控制器

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
const char* MQTT_USER = "admin";
const char* MQTT_PASS = "Zf2279255563";

// ============== 硬件配置 ==============
#define ServoPwmPin 1
#define ServoPwmEnablePin 3
#define DHTPIN 2         // GPIO2连接DHT22（传感器模式）
#define DHTTYPE DHT22
#define SENSOR_RETRIES 3
#define DEFAULT_TEMP 22
#define DEFAULT_HUM 70

const int minPulseWidth = 500;   // 对应舵机最小角度（通常是-90度或0度）
const int maxPulseWidth = 2500;  // 对应舵机最大角度（通常是+90度或180度）
const int pwmPeriod = 20000;     // PWM周期20ms

Servo myservo;

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

  // 构造预期的命令主题
  char expectedTopic[100];
  snprintf(expectedTopic, sizeof(expectedTopic), "homeassistant/switch/%s/set", DEVICE_ID);

  // 使用C字符串比较函数检查主题
  if (strcmp(topic, expectedTopic) == 0) {
    Serial.println("Topic matches LED control command");

    if (strcmp(message, "ON") == 0) {
      updateLEDState(true);
      char stateTopic[100];
      snprintf(stateTopic, sizeof(stateTopic), "homeassistant/switch/%s/state", DEVICE_ID);
      mqtt.publish(stateTopic, "ON");
      Serial.println("LED set to ON");
    } else if (strcmp(message, "OFF") == 0) {
      updateLEDState(false);
      char stateTopic[100];
      snprintf(stateTopic, sizeof(stateTopic), "homeassistant/switch/%s/state", DEVICE_ID);
      mqtt.publish(stateTopic, "OFF");
      Serial.println("LED set to OFF");
    } else {
      Serial.print("Unsupported message: ");
      Serial.println(message);
    }
  } else {
    Serial.print("Received topic does not match: ");
    Serial.println(expectedTopic);
  }
}

// 更新LED状态
void updateLEDState(bool state) {
  // 修正：ESP-01S板载LED低电平点亮
  Serial.print("LED set to: ");
  Serial.println(state ? "ON" : "OFF");
  digitalWrite(ServoPwmEnablePin, HIGH);
  myservo.attach(1, 544, 2400);  // 自定义最小最大脉宽
  myservo.write(90);
  if (state) {
    myservo.write(135);              // tell servo to go to position in variable 'pos'
    delay(1000);
  } else {
    myservo.write(45);              // tell servo to go to position in variable 'pos'
    delay(1000);
  }
  myservo.write(90);
  myservo.detach();
  digitalWrite(ServoPwmEnablePin, LOW);
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
      "device_class":"temperature",
      "state_topic":"homeassistant/sensor/%s/state",
      "unit_of_measurement":"°C",
      "value_template":"{{ value_json.temperature | default(22) }}",
      "unique_id":"%s_temp",
      %s
    })",
           LOCATION, DEVICE_ID, DEVICE_ID, deviceInfo
          );

  char humConfig[512];
  snprintf(humConfig, sizeof(humConfig),
           R"({
      "name":"%s Humidity",
      "device_class":"humidity",
      "state_topic":"homeassistant/sensor/%s/state",
      "unit_of_measurement":"%%",
      "value_template":"{{ value_json.humidity | default(70) }}",
      "unique_id":"%s_hum",
      %s
    })",
           LOCATION, DEVICE_ID, DEVICE_ID, deviceInfo
          );
#endif

  // LED开关自动发现 - 修复后的配置
#ifdef INDOOR_SENSOR
  char switchConfig[512];
  snprintf(switchConfig, sizeof(switchConfig),
           R"({
      "name":"%s LED",
      "command_topic":"homeassistant/switch/%s/set",
      "state_topic":"homeassistant/switch/%s/state",
      "payload_off":"OFF",
      "payload_on":"ON",
      "device_class":"switch",
      "unique_id":"%s_switch",
      %s
    })",
           LOCATION, DEVICE_ID, DEVICE_ID, DEVICE_ID, deviceInfo
          );
#endif

  // 发布自动发现配置
#if defined(INDOOR_SENSOR) || defined(OUTDOOR_SENSOR)
  {
    char discovTemp[100];
    snprintf(discovTemp, sizeof(discovTemp), "homeassistant/sensor/%s_temp/config", DEVICE_ID);
    mqtt.publish(discovTemp, tempConfig, true);
    Serial.println("Published temperature config");

    char discovHum[100];
    snprintf(discovHum, sizeof(discovHum), "homeassistant/sensor/%s_hum/config", DEVICE_ID);
    mqtt.publish(discovHum, humConfig, true);
    Serial.println("Published humidity config");

    char discovSwitch[100];
    snprintf(discovSwitch, sizeof(discovSwitch), "homeassistant/switch/%s/config", DEVICE_ID);


    Serial.println("Publishing switch config:");
    Serial.println(switchConfig);

    if (mqtt.publish(discovSwitch, switchConfig, true)) {
      Serial.println("Switch config published successfully");
    } else {
      Serial.println("Failed to publish switch config");
    }

    char stateTopic[100];
    snprintf(stateTopic, sizeof(stateTopic), "homeassistant/switch/%s/state", DEVICE_ID);
    mqtt.publish(stateTopic, "OFF");
    Serial.println("Published initial LED state: OFF");
  }


#endif

#ifdef INDOOR_SENSOR
  {
    char discovSwitch[100];
    snprintf(discovSwitch, sizeof(discovSwitch), "homeassistant/switch/%s/config", DEVICE_ID);

    Serial.println("Publishing switch config:");
    Serial.println(switchConfig);

    if (mqtt.publish(discovSwitch, switchConfig, true)) {
      Serial.println("Switch config published successfully");
    } else {
      Serial.println("Failed to publish switch config");
    }

    char stateTopic[100];
    snprintf(stateTopic, sizeof(stateTopic), "homeassistant/switch/%s/state", DEVICE_ID);
    mqtt.publish(stateTopic, "OFF");
    Serial.println("Published initial LED state: OFF");
  }

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
    delay(2000);
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
#ifdef INDOOR_SENSOR
      char commandTopic[100];
      snprintf(commandTopic, sizeof(commandTopic), "homeassistant/switch/%s/set", DEVICE_ID);
      if (mqtt.subscribe(commandTopic)) {
        Serial.print("Subscribed to: ");
        Serial.println(commandTopic);
      } else {
        Serial.println("Subscription failed");
      }
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
           "{\"temperature\":%.2f,\"humidity\":%.2f}",
           temperature, humidity
          );

  char stateTopic[100];
  snprintf(stateTopic, sizeof(stateTopic), "homeassistant/sensor/%s/state", DEVICE_ID);

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

#ifdef INDOOR_SENSOR
  pinMode(ServoPwmEnablePin, OUTPUT);
  digitalWrite(ServoPwmEnablePin, LOW);
  myservo.attach(1, 544, 2400);  // 自定义最小最大脉宽
  myservo.write(90);
  Serial.println("LED Controller initialized");
  myservo.detach();
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
