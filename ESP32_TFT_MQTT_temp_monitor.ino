
/*
   For I2C SDA PIN 21
           SLC PIN 22

  NOTE - Error in the ESP32 header file "client.h". Need to edit and comment out the rows
  virtual int connect(IPAddress ip, uint16_t port, int timeout) =0;
  virtual int connect(const char *host, uint16_t port, int timeout) =0;





*/

//#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <ArduinoMqttClient.h>
//#include <analogWrite.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include "network_config.h"
#include "weathericons.h"

#define CHAR_LEN 255

struct Readings {                     // Array to hold the incoming measurement
  const char description[CHAR_LEN];   // Currently set to 3 chars long
  const char topic[CHAR_LEN];         // MQTT topic
  char output[CHAR_LEN];                      // To be output to screen - expected to be 2 chars long
  float currentValue;                 // Current value received
  float lastValue[STORED_READING];    // Defined that the zeroth element is the oldest
  byte changeChar;                    // To indicate change in status
  byte enoughData;                    // to inidate is a full set of STORED_READING number of data points received
  int dataType;                       // Type of data received
  int readingIndex;                   // Index of current reading max will be STORED_READING
  unsigned long lastMessageTime;      // Millis this was last updated
};

struct Settings {                     // Structure to hold the cincomming settings and outgoing confirmations
  const char description[CHAR_LEN];   // Currently set to 3 chars long
  const char topic[CHAR_LEN];         // MQTT topic
  const char confirmTopic[CHAR_LEN];      // To confirm setting changes back to broker
  float currentValue;                 // Current value received
  int dataType;                       // Type of data received
};

struct TftValues {
  bool on;
};

struct Weather {
  float temperature;
  int pressure;
  float humidity;
  char overal[CHAR_LEN];
  char description[CHAR_LEN];
  time_t updateTime;
};

// Array and TFT string settings
#define NO_READING "--"            // Screen output before any mesurement is received
#define DESC_ONOFF "ONO"
#define OUTPUT_TEMPERATURE 1
#define OUTPUT_WEATHER 2

// Character settings
#define CHAR_UP 1
#define CHAR_DOWN 2
#define CHAR_SAME 3
#define CHAR_STAR 42
#define CHAR_BLANK 32
#define CHAR_NO_MESSAGE 33
#define CHAR_NOT_ENOUGH_DATA 46
#define CHAR_ENOUGH_DATA CHAR_BLANK

// Data type definition for array
#define DATA_TEMPERATURE 0
#define DATA_HUMIDITY 1
#define DATA_SETTING 2
#define DATA_ONOFF 3

// Define constants used
#define MAX_NO_MESSAGE_SEC 3600LL        // Time before CHAR_NO_MESSAGE is set in seconds (long) 
#define TIME_RETRIES 100                 // Number of time to retry getting the time during setup
#define TIME_OFFSET 7200                 // Local time offset from UTC
#define WEATHER_UPDATE_INTERVAL 300      // Interval between weather updates
#define STATUS_MESSAGE_TIME 10           // Seconds an status message can be displayed

// Global Variables
Readings readings[] { READINGS_ARRAY };
Settings settings[] {SETTINGS_ARRAY };
Weather weather = {0.0, 0, 0.0, "", "", 0};
char statusMessage[CHAR_LEN];
bool statusMessageUpdated = false;
bool temperatureUpdated = true;
bool weatherUpdated = false;

TftValues tftValues;

WiFiClientSecure wifiClient;
WiFiClient wifiClientWeather;
MqttClient mqttClient(wifiClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, TIME_OFFSET);
TFT_eSPI tft = TFT_eSPI();

// Define eprom address
#define LCD_VALUES_ADDRESS 0

// for touch
#define TOUCH_ARRAY_SIZE 100
#define SENSITIVITY 4  // lower more sensitive
#define TOUCH_LOOPS_NEEDED 3  // number of touched loops to turn on 
#define TOUCH_LIGHT_DELAY 10L

// Monitor Heap size for fragmentation
size_t old_biggest_free_block = 0;

void setup() {

  Serial.begin(115200);  // Default speed of esp32
  xTaskCreatePinnedToCore( tft_output_t, "LCD Update", 8192 , NULL, 10, NULL, 0 ); // Highest priorit on this cpu to avoid coms errors
  delay(3000);
  welcome_message();
  network_connect();
  time_init();
  mqtt_connect();

  xTaskCreatePinnedToCore( get_weather_t, "Get Weather", 8192 , NULL, 3, NULL, 0 );
  xTaskCreatePinnedToCore( receive_mqtt_messages_t, "mqtt", 8192 , NULL, 1, NULL, 1 );
}

void welcome_message() {

  strncpy(statusMessage, "Welcome to the Klauss-o-meter", CHAR_LEN);
  statusMessageUpdated = true;

  delay(1000);
}

void get_weather_t(void * pvParameters ) {

  const char apiKey[] = OPEN_WEATHER_API_KEY;
  const char weather_server[] = WEATHER_SERVER;
  const char location[] = LOCATION;

  while (true) {
    delay(2000);
    if (now() - weather.updateTime > WEATHER_UPDATE_INTERVAL) {
      if (wifiClientWeather.connect(weather_server, 80)) {
        wifiClientWeather.print(F("GET /data/2.5/weather?"));
        wifiClientWeather.print(F("q="));
        wifiClientWeather.print(location);
        wifiClientWeather.print(F("&appid="));
        wifiClientWeather.print(apiKey);
        wifiClientWeather.print(F("&cnt=3"));
        wifiClientWeather.println(F("&units=metric"));
        wifiClientWeather.println(F("Host: api.openweathermap.org"));
        wifiClientWeather.println(F("Connection: close"));
        wifiClientWeather.println();
      } else {
        Serial.println(F("unable to connect to weather server"));
      }
      delay(2000);
      String line = "";

      line = wifiClientWeather.readStringUntil('\n');
      if (line.length() != 0) {
        DynamicJsonDocument root(1000);
        auto deseraliseError = deserializeJson(root, line);
        float weatherTemperature = root["main"]["temp"];
        int weatherPressure = root["main"]["pressure"];
        int weatherHumidity = root["main"]["humidity"];
        const char* weatherOveral = root["weather"][0]["main"];
        const char* weatherDescription = root["weather"][0]["description"];

        weather.temperature = weatherTemperature;
        weather.pressure = weatherPressure;
        weather.humidity = weatherHumidity;
        strncpy(weather.description, weatherDescription, CHAR_LEN);
        strncpy(weather.overal, weatherOveral, CHAR_LEN);
        weather.updateTime = now();
        Serial.println("Weather Updated");
        weatherUpdated = true;
        strncpy(statusMessage, "Weather updated", CHAR_LEN);
        statusMessageUpdated = true;
      }

      wifiClientWeather.flush();
      wifiClientWeather.stop();
    }

  }
}

void network_connect() {

  Serial.print("Connect to WPA SSID: ");
  Serial.println(WIFI_SSID);
  strncpy(statusMessage, "Waiting for ", CHAR_LEN);
  strncat(statusMessage, WIFI_SSID, CHAR_LEN);
  statusMessageUpdated = true;

  while (WiFi.begin(WIFI_SSID, WIFI_PASSWORD) != WL_CONNECTED) {
    Serial.print(".");

    delay(500);
  }
  strncpy(statusMessage, "Connected to: ", CHAR_LEN);
  strncat(statusMessage, WIFI_SSID, CHAR_LEN);
  statusMessageUpdated = true;
}

void time_init() {
  timeClient.begin();
  for (int i = 0; i < TIME_RETRIES; i++) {
    bool retcode;
    retcode = timeClient.forceUpdate();
    if (retcode == true) {
      break;
    }
    timeClient.begin();
  }
  setTime(timeClient.getEpochTime());


  Serial.println(F("Epoch time is: "));
  Serial.println(timeClient.getEpochTime());
  Serial.print(F("Time is: "));
  Serial.println(timeClient.getFormattedTime());
}

void mqtt_connect() {

  mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSWORD);
  Serial.println();
  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(MQTT_SERVER);

  strncpy(statusMessage, "Connecting to: ", CHAR_LEN);
  strncat(statusMessage, MQTT_SERVER, CHAR_LEN);
  statusMessageUpdated = true;

  while (!mqttClient.connect(MQTT_SERVER, MQTT_PORT)) {
    Serial.print("MQTT connection failed");
    strncpy(statusMessage, "Can't connect to: ", CHAR_LEN);
    strncat(statusMessage, MQTT_SERVER, CHAR_LEN);
    statusMessageUpdated = true;
    delay(5000);
    ESP.restart();
  }

  Serial.println("Connected to the MQTT broker");

  for (int i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
    mqttClient.subscribe(readings[i].topic);
    readings[i].lastMessageTime = millis();
  }
  for (int i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
    mqttClient.subscribe(settings[i].topic);
  }
  strncpy(statusMessage, "Connected to: ", CHAR_LEN);
  strncat(statusMessage, MQTT_SERVER, CHAR_LEN);
  statusMessageUpdated = true;

  delay(1000);
}

void tft_draw_string_centre(const char* message, int leftx, int rightx, int y, int font) {

  tft.drawString(message, leftx + (rightx - leftx - tft.textWidth(message, font)) / 2 , y , font);

}


void tft_output_t(void * pvParameters ) {
  String weatherDescriptionFirstLetter;
  String weatherDescriptionTemp;
  time_t statusChangeTime = 0;
  bool statusMessageDisplayed = false;

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  tftValues.on = true;
  // Set up the tittle message box
  tft.fillRect(0, 0, 240, 20, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft_draw_string_centre(" The Klauss-o-meter V1.0", 0, 240, 3, 2);
 
  // Set up the room message box
  //tft.drawRect(20, 25, 200, 140, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Set up the weather message box
  tft.drawRect(20, 170, 200, 120, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(" Weather ", 30 , 163, 2);

  // Set up the status message box
  tft.fillRect(0, 296, 240, 24, TFT_CYAN);
  tft.setTextColor(TFT_BLACK, TFT_CYAN);
  tft.drawString("", 30 , 300, 2);


  while (true) {
    delay(100);
    // Update Status Message

    if (statusChangeTime + STATUS_MESSAGE_TIME < now() && statusMessageDisplayed) {
      tft.fillRect(0, 296, 240, 24, TFT_CYAN);
      statusMessageDisplayed = false;
    }

    if (statusMessageUpdated) {
      statusMessageUpdated = false;
      tft.fillRect(0, 296, 240, 24, TFT_CYAN);
      tft.setTextColor(TFT_BLACK, TFT_CYAN);
      tft_draw_string_centre(statusMessage, 0, 240, 300, 2);
      statusMessageDisplayed = true;
      statusChangeTime = now();
    }

    // Update rooms
    if (temperatureUpdated) {
      temperatureUpdated = false;
      tft.fillRect(21, 25, 198, 138, TFT_BLACK);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);


      // The zones for the rooms are x 21 to 219 width of 198. Give centre boarder of 10 points gives
      // First zone 21 to 115, second 125 to 219, width of 94.

      tft_draw_string_centre(readings[0].description, 21, 115, 30, 2);
      tft_draw_string_centre(readings[0].output, 21, 115, 50, 6);
      tft_draw_string_centre(readings[1].description, 125, 219, 30, 2);
      tft_draw_string_centre(readings[1].output, 125, 219, 50, 6);
      tft_draw_string_centre(readings[2].description, 21, 115, 90, 2);
      tft_draw_string_centre(readings[2].output, 21, 115, 110, 6);
      tft_draw_string_centre(readings[3].description, 125, 219, 90, 2);
      tft_draw_string_centre(readings[3].output, 125, 219, 110, 6);

    }
    if (weatherUpdated) {
      weatherUpdated = false;
      tft.fillRect(21, 176, 198, 113, TFT_BLACK);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      String weatherTemp = String(weather.temperature, 1);
      tft.drawString(weatherTemp, 30 , 190, 6);
      //tft.drawString(weather.overal, 100 , 200, 4);
      weatherDescriptionFirstLetter = String(weather.description).substring(0, 1);
      weatherDescriptionFirstLetter.toUpperCase();
      weatherDescriptionTemp = weatherDescriptionFirstLetter + String(weather.description).substring(1);
      tft.drawString(weatherDescriptionTemp, 30 , 240, 4);
      //tft.setSwapBytes(true);
      //tft.pushImage(100, 100, 99, 100, chancesnow);
       //tft.drawBitmap(100,100,icon_09d,99,100,TFT_BLUE);
    }
  }
}

void update_temperature(char* recMessage, int index) {

  float averageHistory;
  float totalHistory = 0.0;

  readings[index].currentValue = atof(recMessage);
  sprintf(readings[index].output, "%2.0f", readings[index].currentValue);

  if (readings[index].readingIndex == 0) {
    readings[index].changeChar = CHAR_BLANK;  // First reading of this boot
    readings[index].lastValue[0] = readings[index].currentValue;
  }
  else
  {
    for (int i = 0; i < readings[index].readingIndex; i++) {
      totalHistory = totalHistory +  readings[index].lastValue[i];
    }
    averageHistory = totalHistory / readings[index].readingIndex;

    if (readings[index].currentValue > averageHistory) {
      readings[index].changeChar = CHAR_UP;
    }
    if (readings[index].currentValue < averageHistory) {
      readings[index].changeChar = CHAR_DOWN;
    }
    if (readings[index].currentValue == averageHistory) {
      readings[index].changeChar = CHAR_SAME;
    }

    if (readings[index].readingIndex == STORED_READING) {
      readings[index].readingIndex--;
      readings[index].enoughData = CHAR_ENOUGH_DATA;      // Set flag that we have all the readings
      for (int i = 0; i < STORED_READING - 1; i++) {
        readings[index].lastValue[i] = readings[index].lastValue[i + 1];
      }
    }
    else {
      readings[index].enoughData = CHAR_NOT_ENOUGH_DATA;
    }

    readings[index].lastValue[readings[index].readingIndex] = readings[index].currentValue; // update with latest value
  }

  readings[index].readingIndex++;
  readings[index].lastMessageTime = millis();
  temperatureUpdated = true;

  strncpy(statusMessage, "Update received for ", CHAR_LEN);
  strncat(statusMessage, readings[index].description , CHAR_LEN);
  statusMessageUpdated = true;
}

void update_mqtt_settings() {

  for (int i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
    if (settings[i].description == DESC_ONOFF) {
      mqttClient.beginMessage(settings[i].confirmTopic);

      mqttClient.print(tftValues.on);
      mqttClient.endMessage();
    }
  }
}

void update_on_off(char* recMessage, int index) {

  if (strcmp(recMessage, "1") == 0) {
    tftValues.on = true;
    settings[index].currentValue = 1;
  }
  if (strcmp(recMessage, "0") == 0) {
    tftValues.on = false;
    settings[index].currentValue = 0;
  }
  update_mqtt_settings();
}

void receive_mqtt_messages_t(void * pvParams) {
  int messageSize = 0;
  String topic;
  char recMessage[CHAR_LEN] = {0};
  int index;
  bool readingMessageReceived;

  while (true) {
    delay(10);
    if (!mqttClient.connected()) {
      Serial.println("MQTT error detected");
      mqtt_connect();
    }

    messageSize = mqttClient.parseMessage();
    if (messageSize) {   //Message received
      topic = mqttClient.messageTopic();
      mqttClient.read((unsigned char*)recMessage, (size_t)sizeof(recMessage)); //Distructive read of message
      recMessage[messageSize] = 0;
      Serial.println("Topic: " + String(topic) + " Msg: " + recMessage);
      readingMessageReceived = false;               // To check if non reading message
      for (int i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
        if (topic == readings[i].topic) {
          index = i;
          if (readings[i].dataType == DATA_TEMPERATURE) {
            update_temperature(recMessage, index);
          }
          if (readings[i].dataType == DATA_HUMIDITY) {
            //update_temperature(recMessage, index);
          }
        }
      }
      for (int i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
        if (topic == settings[i].topic) {
          index = i;
          if (settings[i].dataType == DATA_ONOFF) {
            update_on_off(recMessage, index);
          }
        }
      }
    }
  }
}

void loop() {

  size_t free_heap = 0;
  size_t new_biggest_free_block = 0;

  delay(500);
  timeClient.update();

  free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  new_biggest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (old_biggest_free_block != new_biggest_free_block) {
    old_biggest_free_block = new_biggest_free_block;
    Serial.print(F("Heap data: "));
    Serial.print(free_heap);
    Serial.print(F(" largest free block "));
    Serial.println(new_biggest_free_block);
  }
  for (int i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
    if ((millis() > readings[i].lastMessageTime + (MAX_NO_MESSAGE_SEC * 1000)) && (readings[i].output != NO_READING)) {
      readings[i].changeChar = CHAR_NO_MESSAGE;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    network_connect();
  }
}
