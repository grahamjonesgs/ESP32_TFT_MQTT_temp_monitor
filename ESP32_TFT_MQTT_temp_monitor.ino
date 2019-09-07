/*

  NOTE - Error in the ESP32 header file "client.h". Need to edit and comment out the rows
  virtual int connect(IPAddress ip, uint16_t port, int timeout) =0;
  virtual int connect(const char *host, uint16_t port, int timeout) =0;

  // Define in user_setup.h in the library directory in TFT_eSPI
  #define TFT_MISO 22
  #define TFT_MOSI 23
  #define TFT_SCLK 18
  #define TFT_CS   21  // Chip select control pin
  #define TFT_DC    19  // Data Command control pin
  #define TFT_RST   5 // Reset pin (could connect to RST pin)
  #define TOUCH_CS 2     // Chip select pin (T_CS) of touch screen

  // Defned in this file
  #define LED_PIN 4

  For touch LSO need to common
  T_DO / TFT_MISO
  T_DIN  / TFT_MOSI
  T_CLK / SCLK

  Order of PIN on TFT         Ordered in one side of ESP32

  VCC                         VCC
  GND                         GND
  CS                          15
  Reset                       4
  D/C                         2
  MOSI                        5
  SCK                         18
  LED                         19
  MISO                        21
  T_CLK (paired with SCLK)    xx
  T_CS                        22
  T_DIN (paired with MOSI)    xx
  T_DO  (paired with MISO)    xx

  Touch caib settings
  uint16_t calData[5] = { 307, 3372, 446, 3129, 1 };
  tft.setTouch(calData);


*/

//#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <ArduinoMqttClient.h>
#include <analogWrite.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#include "GfxUi.h"          // Attached to this sketch
#include "SPIFFS_Support.h" // Attached to this sketch
#include "network_config.h"

#define CHAR_LEN 255

struct Readings {                     // Array to hold the incoming measurement
  const char description[CHAR_LEN];   // Currently set to 3 chars long
  const char topic[CHAR_LEN];         // MQTT topic
  char output[CHAR_LEN];              // To be output to screen - expected to be 2 chars long
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
  char icon[CHAR_LEN];
  long timeOffset;
  time_t updateTime;
};

struct Forecast {
  time_t dateTime;
  float maxTemp;
  float minTemp;
  char description[CHAR_LEN];
  char icon[CHAR_LEN];
  float moonPhase;
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
#define MAX_NO_MESSAGE_SEC 1800LL        // Time before CHAR_NO_MESSAGE is set in seconds (long) 
#define TIME_RETRIES 100                 // Number of time to retry getting the time during setup
#define WEATHER_UPDATE_INTERVAL 600      // Interval between weather updates
#define FORECAST_UPDATE_INTERVAL 1800     // Interval between forecast updates
#define FORECAST_DAYS 5                  // Number of day's forecast to request
#define STATUS_MESSAGE_TIME 10           // Seconds an status message can be displayed
#define LED_BRIGHT 255
#define LED_DIM 20
#define LED_PIN 4

// Global Variables
Readings readings[] { READINGS_ARRAY };
Settings settings[] {SETTINGS_ARRAY };
Weather weather = {0.0, 0, 0.0, "", "", "", 0, 0};
Forecast forecast[FORECAST_DAYS];
time_t forecastUpdateTime = 0;
char statusMessage[CHAR_LEN];
bool statusMessageUpdated = false;
bool temperature0Updated = true;
bool temperature1Updated = true;
bool temperature2Updated = true;
bool temperature3Updated = true;

bool weatherUpdated = false;

TftValues tftValues;

WiFiClientSecure wifiClient;
HTTPClient httpClientWeather;
MqttClient mqttClient(wifiClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
TFT_eSPI tft = TFT_eSPI();
GfxUi ui = GfxUi(&tft); // Jpeg and bmpDraw functions TODO: pull outside of a class

// Define eprom address
#define LCD_VALUES_ADDRESS 0

// Monitor Heap size for fragmentation
size_t old_biggest_free_block = 0;

void setup() {

  Serial.begin(115200);  // Default speed of esp32
  SPIFFS.begin();

  pinMode(LED_PIN, OUTPUT);
  xTaskCreatePinnedToCore( tft_output_t, "LCD Update", 8192 , NULL, 10, NULL, 0 ); // Highest priorit on this cpu to avoid coms errors
  network_connect();
  time_init();

  xTaskCreatePinnedToCore( get_weather_t, "Get Weather", 8192 , NULL, 3, NULL, 0 );
  xTaskCreatePinnedToCore( receive_mqtt_messages_t, "mqtt", 8192 , NULL, 1, NULL, 1 );
}


void get_weather_t(void * pvParameters ) {

  //const char apiKey[] = OPEN_WEATHER_API_KEY;
  const char apiKey[] = WEATHERBIT_API_KEY;
  const char weather_server[] = WEATHER_SERVER;
  const char location[] = LOCATION;
  char line[CHAR_LEN];
  String requestUrl;


  // https://api.weatherbit.io/v2.0/current?city=munich&key=c8b9c930deae44c1990332a5297982c5

  while (true) {
    delay(2000);
    if (now() - weather.updateTime > WEATHER_UPDATE_INTERVAL) {
      httpClientWeather.begin("http://" + String(WEATHER_SERVER) + "/v2.0/current?city=" + String(LOCATION) + "&key=" + String(apiKey));
      int httpCode = httpClientWeather.GET();
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          String payload = httpClientWeather.getString();

          DynamicJsonDocument root(5000);
          auto deseraliseError = deserializeJson(root, payload);
          float weatherTemperature = root["data"][0]["temp"];
          int weatherPressure = root["data"][0]["pres"];
          const char* weatherDescription = root["data"][0]["weather"]["description"];
          const char* weatherIcon = root["data"][0]["weather"]["icon"];

          weather.temperature = weatherTemperature;
          weather.pressure = weatherPressure;
          if (weatherDescription != 0) {
            strncpy(weather.description, weatherDescription, CHAR_LEN);
          }
          if (weatherIcon != 0) {
            strncpy(weather.icon, weatherIcon, CHAR_LEN);
          }

          weather.updateTime = now();
          weatherUpdated = true;
          strncpy(statusMessage, "Weather updated", CHAR_LEN);
          statusMessageUpdated = true;
        }
      } else
      {
        Serial.printf("[HTTP] GET... failed, error: %s\n", httpClientWeather.errorToString(httpCode).c_str());
      }
    }

    if (now() - forecastUpdateTime > FORECAST_UPDATE_INTERVAL) {
      httpClientWeather.begin("http://" + String(WEATHER_SERVER) + "/v2.0/forecast/daily?city=" + String(LOCATION) + +"&days=" + String(FORECAST_DAYS) + "&key=" + String(apiKey));
      int httpCode = httpClientWeather.GET();
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          String payload = httpClientWeather.getString();

          DynamicJsonDocument root(10000);
          auto deseraliseError = deserializeJson(root, payload);
          for (int i = 0; i < FORECAST_DAYS; i++) {


            struct Forecast {
              time_t dateTime;
              float maxTemp;
              float minTemp;
              char description[CHAR_LEN];
              char icon[CHAR_LEN];
              float moonPhase;
            };

            time_t forecastTime = root["data"][i]["ts"];
            float forecastMaxTemp = root["data"][i]["max_temp"];
            float forecastMinTemp = root["data"][i]["min_temp"];
            float forecastMoon = root["data"][i]["weather"]["moon_phase"];
            const char* forecastDescription = root["data"][i]["weather"]["description"];
            const char* forecastIcon = root["data"][i]["weather"]["icon"];

            forecast[i].dateTime = forecastTime;
            forecast[i].maxTemp = forecastMaxTemp;
            forecast[i].minTemp = forecastMinTemp;
            forecast[i].moonPhase = forecastMoon;
            if (forecastDescription != 0) {
              strncpy(forecast[i].description, forecastDescription, CHAR_LEN);
            }
            if (forecastIcon != 0) {
              strncpy(forecast[i].icon, forecastIcon, CHAR_LEN);
            }

          }
          forecastUpdateTime = now();
          strncpy(statusMessage, "Forecast updated", CHAR_LEN);
          statusMessageUpdated = true;
          weatherUpdated = true;
        }
      } else
      {
        Serial.printf("[HTTP] GET... failed, error: %s\n", httpClientWeather.errorToString(httpCode).c_str());
      }


      httpClientWeather.end();
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
  strncpy(statusMessage, "Connected to ", CHAR_LEN);
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

  strncpy(statusMessage, "Connecting to ", CHAR_LEN);
  strncat(statusMessage, MQTT_SERVER, CHAR_LEN);
  statusMessageUpdated = true;

  if (!mqttClient.connect(MQTT_SERVER, MQTT_PORT)) {
    Serial.print("MQTT connection failed");
    strncpy(statusMessage, "Can't connect to ", CHAR_LEN);
    strncat(statusMessage, MQTT_SERVER, CHAR_LEN);
    statusMessageUpdated = true;
    delay(2000);
    return;
  }

  Serial.println("Connected to the MQTT broker");

  for (int i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
    mqttClient.subscribe(readings[i].topic);
    readings[i].lastMessageTime = millis();
  }
  for (int i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
    mqttClient.subscribe(settings[i].topic);
  }
  strncpy(statusMessage, "Connected to ", CHAR_LEN);
  strncat(statusMessage, MQTT_SERVER, CHAR_LEN);
  statusMessageUpdated = true;

  delay(1000);
}

void tft_draw_string_centre(const char* message, int leftx, int rightx, int y, int font) {

  tft.drawString(message, leftx + (rightx - leftx - tft.textWidth(message, font)) / 2 , y , font);

}

void draw_temperature_icon (const char changeChar, const char* output, int x, int y) {


  switch (changeChar) {
    case CHAR_UP:
      ui.drawBmp("/temperature/inc.bmp", x, y);
      break;
    case CHAR_DOWN:
      ui.drawBmp("/temperature/dec.bmp", x, y);
      break;
    case CHAR_NO_MESSAGE:
      ui.drawBmp("/temperature/error.bmp", x, y);
      break;
    default:
      if (strcmp(output, NO_READING) != 0) {
        ui.drawBmp("/temperature/same.bmp", x, y);
      }
      break;

  }
}


void tft_output_t(void * pvParameters ) {
#define TEMP_LEFT 0
#define TEMP_RIGHT 220
#define TEMP_TOP 25
#define TEMP_BOTTOM 163

  //String weatherDescriptionFirstLetter;
  //String weatherDescriptionTemp;
  time_t statusChangeTime = 0;
  bool statusMessageDisplayed = false;

  tft.init();
  analogWrite(LED_PIN, LED_BRIGHT);
  tft.setRotation(0);
  ui.drawBmp("/images/beach.bmp", 0, 0);
  delay(5000);
  tft.fillScreen(TFT_BLACK);

  tftValues.on = true;
  // Set up the tittle message box
  tft.fillRect(0, 0, 240, 20, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft_draw_string_centre(" The Klauss-o-meter V1.0", 0, 240, 3, 2);

  // Set up the weather message box
  tft.drawLine(0, 170, 240, 170, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_draw_string_centre(" Weather ", 0, 240, 163, 2);


  while (true) {
    delay(100);
    if (tftValues.on) {
      analogWrite(LED_PIN, LED_BRIGHT);
    }
    else
    {
      analogWrite(LED_PIN, LED_DIM);
    }

    // Remove old status messages
    if (statusChangeTime + STATUS_MESSAGE_TIME < now() && statusMessageDisplayed) {
      tft.fillRect(0, 296, 240, 24, TFT_BLACK);
      statusMessageDisplayed = false;
    }

    if (statusMessageUpdated) {
      statusMessageUpdated = false;
      tft.fillRect(0, 296, 240, 24, TFT_BLACK);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft_draw_string_centre(statusMessage, 0, 240, 300, 2);
      statusMessageDisplayed = true;
      statusChangeTime = now();
    }

    // Update rooms
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    if (temperature0Updated) {
      temperature0Updated = false;
      tft.fillRect(TEMP_LEFT, TEMP_TOP, (TEMP_RIGHT - TEMP_LEFT) / 2, (TEMP_BOTTOM - TEMP_TOP) / 2, TFT_BLACK);
      tft_draw_string_centre(readings[0].description, TEMP_LEFT, (TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_TOP + 5, 2);
      tft_draw_string_centre(readings[0].output, TEMP_LEFT, (TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_TOP + 23, 6);
      draw_temperature_icon(readings[0].changeChar, readings[0].output, TEMP_LEFT + 85, TEMP_TOP + 23);
    }
    if (temperature1Updated) {
      temperature1Updated = false;
      tft.fillRect((TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_TOP, (TEMP_RIGHT - TEMP_LEFT) / 2, (TEMP_BOTTOM - TEMP_TOP) / 2, TFT_BLACK);
      tft_draw_string_centre(readings[1].description, (TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_RIGHT, TEMP_TOP + 5, 2);
      tft_draw_string_centre(readings[1].output, (TEMP_RIGHT - TEMP_LEFT) / 2,  TEMP_RIGHT, TEMP_TOP + 23, 6);
      draw_temperature_icon(readings[1].changeChar, readings[1].output, (TEMP_RIGHT - TEMP_LEFT) / 2 + 85, TEMP_TOP + 23);
    }
    if (temperature2Updated) {
      tft.fillRect(TEMP_LEFT, (TEMP_BOTTOM - TEMP_TOP) / 2 + TEMP_TOP, (TEMP_RIGHT - TEMP_LEFT) / 2 , (TEMP_BOTTOM - TEMP_TOP) / 2, TFT_BLACK);
      temperature2Updated = false;
      tft_draw_string_centre(readings[2].description, TEMP_LEFT, (TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_TOP + 70, 2);
      tft_draw_string_centre(readings[2].output, TEMP_LEFT, (TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_TOP + 88, 6);
      draw_temperature_icon(readings[2].changeChar, readings[2].output, TEMP_LEFT + 85, TEMP_TOP + 88);
    }
    if (temperature3Updated) {
      temperature3Updated = false;
      tft.fillRect((TEMP_RIGHT - TEMP_LEFT) / 2 , (TEMP_BOTTOM - TEMP_TOP) / 2 + TEMP_TOP, (TEMP_RIGHT - TEMP_LEFT) / 2, (TEMP_BOTTOM - TEMP_TOP) / 2, TFT_BLACK);
      tft_draw_string_centre(readings[3].description, (TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_RIGHT, TEMP_TOP + 70, 2);
      tft_draw_string_centre(readings[3].output, (TEMP_RIGHT - TEMP_LEFT) / 2, TEMP_RIGHT, TEMP_TOP + 88, 6);
      draw_temperature_icon(readings[3].changeChar, readings[3].output, (TEMP_RIGHT - TEMP_LEFT) / 2 + 85, TEMP_TOP + 88);
    }


    if (weatherUpdated) {
      weatherUpdated = false;
      tft.fillRect(21, 176, 198, 113, TFT_BLACK);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      String weatherTemp = String(weather.temperature, 1);
      tft.drawString(weatherTemp, 30 , 190, 6);
      weather.description[0] = toupper(weather.description[0]);
      //tft.drawString(weather.description, 30 , 240, 4);
      ui.drawBmp("/wbicons/" + String(weather.icon) + ".bmp", 140, 190);
      tft_draw_string_centre(dayShortStr(weekday(forecast[2].dateTime)), 0, 240 / 3, 230, 2);
      tft_draw_string_centre(dayShortStr(weekday(forecast[3].dateTime)), 240 / 3, 2 * 240 / 3, 230, 2);
      tft_draw_string_centre(dayShortStr(weekday(forecast[4].dateTime)), 2 * 240 / 3, 240, 230, 2);
      ui.drawBmp("/wbicons/" + String(forecast[2].icon) + ".bmp", (240 / 6) - 50 / 2, 250);
      ui.drawBmp("/wbicons/" + String(forecast[3].icon) + ".bmp", (3 * 240 / 6) - 50 / 2, 250);
      ui.drawBmp("/wbicons/" + String(forecast[4].icon) + ".bmp", (5 * 240 / 6) - 50 / 2, 250);

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
  switch (index) {
    case 0:
      temperature0Updated = true;
      break;
    case 1:
      temperature1Updated = true;
      break;
    case 2:
      temperature2Updated = true;
      break;
    case 3:
      temperature3Updated = true;
      break;
  }



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
