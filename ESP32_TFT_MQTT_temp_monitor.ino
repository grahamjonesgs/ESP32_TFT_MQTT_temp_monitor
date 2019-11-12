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

   Touch caib settings
  uint16_t calData[5] = { 307, 3372, 446, 3129, 1 };
  tft.setTouch(calData);

*/

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

struct ForecastDays {
  time_t dateTime;
  float maxTemp;
  float minTemp;
  char description[CHAR_LEN];
  char icon[CHAR_LEN];
  float moonPhase;
};


struct ForecastHours {
  char localTime[CHAR_LEN];
  char icon[CHAR_LEN];
};

// Array and TFT string settings
#define NO_READING "--"            // Screen output before any mesurement is received
#define DESC_ONOFF "ONO"

// Character settings
#define CHAR_UP 1
#define CHAR_DOWN 2
#define CHAR_SAME 3
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
#define FORECAST_DAYS_UPDATE_INTERVAL 3600     // Interval between days forecast updates
#define FORECAST_HOURS_UPDATE_INTERVAL 1800     // Interval between days forecast updates
#define FORECAST_DAYS 16                  // Number of day's forecast to request
#define FORECAST_HOURS 16                  // Number of hours's forecast to request
#define STATUS_MESSAGE_TIME 5           // Seconds an status message can be displayed
#define MAX_WIFI_RETRIES 3
#define LED_BRIGHT 255
#define LED_DIM 20
#define LED_PIN 4
#define PRESS_DEBOUNCE 1
#define TOUCH_CALIBRATION { 330, 3303, 450, 3116, 1 }

// Screen types
#define MAIN_SCREEN 0
#define FORECAST_SCREEN 1
int displayType = MAIN_SCREEN;

// Global Variables
Readings readings[] { READINGS_ARRAY };
Settings settings[] {SETTINGS_ARRAY };
Weather weather = {0.0, 0, 0.0, "", "", "", 0, 0};
ForecastDays forecastDays[FORECAST_DAYS];
ForecastHours forecastHours[FORECAST_HOURS];
time_t forecastDaysUpdateTime = 0;
time_t forecastHoursUpdateTime = 0;
char statusMessage[CHAR_LEN];
bool statusMessageUpdated = false;
bool temperatureUpdated[4] = {true, true, true, true};
bool weatherUpdated = false;
bool forecastDaysUpdated = false;
bool forecastHoursUpdated = false;

TftValues tftValues;

WiFiClientSecure wifiClient;
MqttClient mqttClient(wifiClient);

HTTPClient httpClientWeather;
HTTPClient httpClientInsta;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

TFT_eSPI tft = TFT_eSPI();
GfxUi ui = GfxUi(&tft);

// Monitor Heap size for fragmentation
size_t old_biggest_free_block = 0;

void setup() {

  Serial.begin(115200);  // Default speed of esp32
  SPIFFS.begin();
  pinMode(LED_PIN, OUTPUT);
  xTaskCreatePinnedToCore( tft_output_t, "TFT Update", 8192 , NULL, 10, NULL, 0 ); // Highest priorit on this cpu to avoid coms errors
  network_connect();
  time_init();

  xTaskCreatePinnedToCore( get_weather_t, "Get Weather", 8192 , NULL, 3, NULL, 0 );
  xTaskCreatePinnedToCore( receive_mqtt_messages_t, "mqtt", 8192 , NULL, 1, NULL, 1 );

}

void get_weather_t(void * pvParameters ) {

  //const char apiKey[] = OPEN_WEATHER_API_KEY;
  const char apiKey[] = WEATHERBIT_API_KEY;
  String requestUrl;

  while (true) {
    delay(2000);
    if (now() - weather.updateTime > WEATHER_UPDATE_INTERVAL) {
      httpClientWeather.begin("http://" + String(WEATHER_SERVER) + "/v2.0/current?city=" + String(LOCATION) + "&key=" + String(apiKey));
      int httpCode = httpClientWeather.GET();
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          String payload = httpClientWeather.getString();

          DynamicJsonDocument root(5000);
          deserializeJson(root, payload);
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

    if (now() - forecastDaysUpdateTime > FORECAST_DAYS_UPDATE_INTERVAL) {
      httpClientWeather.begin("http://" + String(WEATHER_SERVER) + "/v2.0/forecast/daily?city=" + String(LOCATION) + +"&days=" + String(FORECAST_DAYS) + "&key=" + String(apiKey));
      int httpCode = httpClientWeather.GET();
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          String payload = httpClientWeather.getString();

          DynamicJsonDocument root(20000);
          deserializeJson(root, payload);
          for (int i = 0; i < FORECAST_DAYS; i++) {

            time_t forecastTime = root["data"][i]["ts"];
            float forecastMaxTemp = root["data"][i]["max_temp"];
            float forecastMinTemp = root["data"][i]["min_temp"];
            float forecastMoon = root["data"][i]["moon_phase"];
            const char* forecastDescription = root["data"][i]["weather"]["description"];
            const char* forecastIcon = root["data"][i]["weather"]["icon"];
            forecastDays[i].dateTime = forecastTime;
            forecastDays[i].maxTemp = forecastMaxTemp;
            forecastDays[i].minTemp = forecastMinTemp;
            forecastDays[i].moonPhase = forecastMoon;
            if (forecastDescription != 0) {
              strncpy(forecastDays[i].description, forecastDescription, CHAR_LEN);
            }
            if (forecastIcon != 0) {
              strncpy(forecastDays[i].icon, forecastIcon, CHAR_LEN);
            }

          }
          forecastDaysUpdateTime = now();
          strncpy(statusMessage, "Forecast days updated", CHAR_LEN);
          statusMessageUpdated = true;
          forecastDaysUpdated = true;
        }
      } else
      {
        Serial.printf("[HTTP] GET... failed, error: %s\n", httpClientWeather.errorToString(httpCode).c_str());
      }
    }

    if (now() - forecastHoursUpdateTime > FORECAST_HOURS_UPDATE_INTERVAL) {
      httpClientWeather.begin("http://" + String(WEATHER_SERVER) + "/v2.0/forecast/hourly?city=" + String(LOCATION) + +"&hours=" + String(FORECAST_HOURS) + "&key=" + String(apiKey));
      int httpCode = httpClientWeather.GET();
      if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
          String payload = httpClientWeather.getString();

          DynamicJsonDocument root(20000);
          deserializeJson(root, payload);
          for (int i = 0; i < FORECAST_HOURS; i++) {

            const char* localTime = root["data"][i]["timestamp_local"];
            const char* forecastIcon = root["data"][i]["weather"]["icon"];
            if (forecastIcon != 0) {
              strncpy(forecastHours[i].icon, forecastIcon, CHAR_LEN);
            }
            if (localTime != 0) {
              if (strlen(localTime) > 12) {
                strncpy(forecastHours[i].localTime, &localTime[11], CHAR_LEN);
                forecastHours[i].localTime[5] = 0;
              }
            }
          }
          forecastHoursUpdateTime = now();
          strncpy(statusMessage, "Hourly forecast updated", CHAR_LEN);
          statusMessageUpdated = true;
          forecastHoursUpdated = true;
        }
      } else
      {
        Serial.printf("[HTTP] GET... failed, error: %s\n", httpClientWeather.errorToString(httpCode).c_str());
      }
    }
    httpClientWeather.end();
  }
}


void network_connect() {

  int retryLoops = 0;
  Serial.print("Connect to WPA SSID: ");
  Serial.println(WIFI_SSID);
  strncpy(statusMessage, "Waiting for ", CHAR_LEN);
  strncat(statusMessage, WIFI_SSID, CHAR_LEN);
  statusMessageUpdated = true;

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (int retcode = WiFi.status() != WL_CONNECTED) {
    delay(5000);
    Serial.println("WiFi return code is: " + String(retcode));
    strncpy(statusMessage, "Waiting for ", CHAR_LEN);
    strncat(statusMessage, WIFI_SSID, CHAR_LEN);
    statusMessageUpdated = true;

    retryLoops++;
    if (retryLoops > MAX_WIFI_RETRIES) {
      strncpy(statusMessage, "Error with WiFi, reboot ", CHAR_LEN);
      statusMessageUpdated = true;
      delay(2000);
      ESP.restart();
    }

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

  Serial.println();
  Serial.println(F("Epoch time is : "));
  Serial.println(timeClient.getEpochTime());
  Serial.print(F("Time is : "));
  Serial.println(timeClient.getFormattedTime());
}

void mqtt_connect() {

  mqttClient.setUsernamePassword(MQTT_USER, MQTT_PASSWORD);
  Serial.println();
  Serial.print("Attempting to connect to the MQTT broker : ");
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
        //ui.drawBmp("/temperature/same.bmp", x, y);
      }
      break;
  }
}

void tft_output_t(void * pvParameters ) {

  uint16_t x = 0, y = 0;
  boolean pressed;
  time_t lastPressed = 0;

  struct TempZone {
    int x;
    int y;
    int xSize;
    int ySize;
  };

  int TITLE_LEFT = 0;
  int TITLE_RIGHT = 320;
  int TITLE_TOP = 0;
  int TITLE_BOTTOM = 15;

  int TEMP_LEFT = 0;
  int TEMP_RIGHT = 320;
  int TEMP_TOP = 15;
  int TEMP_BOTTOM = 80;

  int WEATHER_LEFT = 0;
  int WEATHER_RIGHT  = 320;
  int WEATHER_TOP = 90;
  int WEATHER_BOTTOM = 159;

  int HOURS_FORECAST_LEFT = 0;
  int HOURS_FORECAST_RIGHT = 320;
  int HOURS_FORECAST_TOP = 150;
  int HOURS_FORECAST_BOTTOM = 215;

  int DAYS_FORECAST_LEFT = 0;
  int DAYS_FORECAST_RIGHT = 320;
  int DAYS_FORECAST_TOP = 90;
  int DAYS_FORECAST_BOTTOM = 215;

  int STATUS_LEFT = 0;
  int STATUS_RIGHT = 320;
  int STATUS_TOP = 216;
  int STATUS_BOTTOM = 240;

  int DISPLAY_DAYS = 5;
  int DISPLAY_HOURS = 5;

  TempZone tempZone[4] = {\
    {TEMP_LEFT,                                  TEMP_TOP,     (TEMP_RIGHT - TEMP_LEFT) / 4,  TEMP_BOTTOM - TEMP_TOP},
    {TEMP_LEFT + (TEMP_RIGHT - TEMP_LEFT) / 4,   TEMP_TOP,     (TEMP_RIGHT - TEMP_LEFT) / 4,  TEMP_BOTTOM - TEMP_TOP},
    {TEMP_LEFT + 2 * (TEMP_RIGHT - TEMP_LEFT) / 4, TEMP_TOP,     (TEMP_RIGHT - TEMP_LEFT) / 4,  TEMP_BOTTOM - TEMP_TOP},
    {TEMP_LEFT + 3 * (TEMP_RIGHT - TEMP_LEFT) / 4, TEMP_TOP,     (TEMP_RIGHT - TEMP_LEFT) / 4,  TEMP_BOTTOM - TEMP_TOP}
  };

  time_t statusChangeTime = 0;
  bool statusMessageDisplayed = false;
  tft.init();
  tft.fillScreen(TFT_BLACK);
  analogWrite(LED_PIN, LED_BRIGHT);
  tft.setRotation(3);
  ui.drawJpeg("/images/logo.jpg", 0, 0);
  delay(5000);
  tft.fillScreen(TFT_BLACK);
  tftValues.on = true;

  tft.fillRect(TITLE_LEFT, TITLE_TOP, TITLE_RIGHT - TITLE_LEFT, TITLE_BOTTOM - TITLE_TOP, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft_draw_string_centre(" The Klauss-o-meter V2.0", TITLE_LEFT, TITLE_RIGHT, TITLE_TOP, 2);

  // Set up the weather message box
  tft.drawLine(WEATHER_LEFT, WEATHER_TOP, WEATHER_RIGHT, WEATHER_TOP, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_draw_string_centre(LOCATION, WEATHER_LEFT, WEATHER_RIGHT, WEATHER_TOP - 7 , 2);

  while (true) {
    delay(100);
    if (tftValues.on) {
      analogWrite(LED_PIN, LED_BRIGHT);
    }
    else
    {
      analogWrite(LED_PIN, LED_DIM);
    }

    pressed = tft.getTouch(&x, &y);
    if (pressed && (now() - lastPressed > PRESS_DEBOUNCE)) {
      lastPressed = now();
      switch (displayType) {

        case MAIN_SCREEN:
          displayType = FORECAST_SCREEN;
          forecastDaysUpdated = true;
          weatherUpdated = true;
          break;

        case FORECAST_SCREEN:
        default:
          displayType = MAIN_SCREEN;
          forecastHoursUpdated = true;
      }
    }


    // Remove old status messages
    if (statusChangeTime + STATUS_MESSAGE_TIME < now() && statusMessageDisplayed) {
      tft.fillRect(STATUS_LEFT, STATUS_TOP, STATUS_RIGHT - STATUS_LEFT, STATUS_BOTTOM - STATUS_TOP, TFT_BLACK);
      statusMessageDisplayed = false;
    }

    if (statusMessageUpdated) {
      statusMessageUpdated = false;
      tft.fillRect(STATUS_LEFT, STATUS_TOP, STATUS_RIGHT - STATUS_LEFT, STATUS_BOTTOM - STATUS_TOP, TFT_BLACK);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft_draw_string_centre(statusMessage, STATUS_LEFT, STATUS_RIGHT, STATUS_TOP + 4, 2);
      statusMessageDisplayed = true;
      statusChangeTime = now();
    }
    yield();
    // Update rooms
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    for (int i = 0; i < 4; i++) {
      if (temperatureUpdated[i]) {
        temperatureUpdated[i] = false;
        tft.fillRect(tempZone[i].x, tempZone[i].y, tempZone[i].xSize, tempZone[i].ySize, TFT_BLACK);
        tft.drawString(readings[i].description, tempZone[i].x, tempZone[i].y + 5, 2);
        tft.drawString(readings[i].output, tempZone[i].x, tempZone[i].y + 23, 6);
        draw_temperature_icon(readings[i].changeChar, readings[i].output, tempZone[i].x + 55, tempZone[i].y + 28);
      }
    }

    yield();
    if (displayType == MAIN_SCREEN) {
      if (weatherUpdated && weather.updateTime != 0) {
        weatherUpdated = false;
        tft.fillRect(WEATHER_LEFT, WEATHER_TOP + 8, WEATHER_RIGHT - WEATHER_LEFT, WEATHER_BOTTOM - WEATHER_TOP - 15, TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        String weatherTemp = String(weather.temperature, 1);
        tft.drawString(weatherTemp, WEATHER_LEFT + 5 , WEATHER_TOP + 15, 6);
        weather.description[0] = toupper(weather.description[0]);
        tft.drawString(weather.description, WEATHER_LEFT + 105, WEATHER_TOP + 25 , 4);
      }
      yield();

      if (forecastHoursUpdated && forecastHoursUpdateTime != 0) {
        forecastHoursUpdated = false;
        tft.fillRect(HOURS_FORECAST_LEFT, HOURS_FORECAST_TOP, HOURS_FORECAST_RIGHT - HOURS_FORECAST_LEFT, HOURS_FORECAST_BOTTOM - HOURS_FORECAST_TOP, TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        for (int i = 0; i < DISPLAY_HOURS; i++) {
          tft_draw_string_centre(forecastHours[i].localTime, i * HOURS_FORECAST_RIGHT / DISPLAY_HOURS, (i + 1) * HOURS_FORECAST_RIGHT / DISPLAY_HOURS, HOURS_FORECAST_TOP + 5, 2);
          ui.drawBmp("/wbicons/" + String(forecastHours[i].icon) + ".bmp", i * HOURS_FORECAST_RIGHT / DISPLAY_HOURS + 10, HOURS_FORECAST_TOP + 20);

        }
      }
    }

    else
    {

      if (displayType == FORECAST_SCREEN && forecastHoursUpdateTime != 0 && forecastDaysUpdated) {
        forecastDaysUpdated = false;
        tft.fillRect(DAYS_FORECAST_LEFT, DAYS_FORECAST_TOP + 9, DAYS_FORECAST_RIGHT - DAYS_FORECAST_LEFT, DAYS_FORECAST_BOTTOM - DAYS_FORECAST_TOP - 10, TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Max", 0 , DAYS_FORECAST_TOP + 70, 2);
        tft.drawString("Min", 0 , DAYS_FORECAST_TOP + 90, 2);

        for (int i = 0; i <  DISPLAY_DAYS; i++) {
          tft_draw_string_centre(dayShortStr(weekday(forecastDays[i + 2].dateTime)), i * DAYS_FORECAST_RIGHT / 4, (i + 1) * DAYS_FORECAST_RIGHT / 4, DAYS_FORECAST_TOP + 5, 2);
          ui.drawBmp("/wbicons/" + String(forecastDays[i + 2].icon) + ".bmp", i * DAYS_FORECAST_RIGHT / 4 + 15, DAYS_FORECAST_TOP + 20);
          char minTemp[CHAR_LEN];
          char maxTemp[CHAR_LEN];
          String(forecastDays[i + 2].maxTemp, 0).toCharArray(maxTemp, CHAR_LEN);
          String(forecastDays[i + 2].minTemp, 0).toCharArray(minTemp, CHAR_LEN);
          tft_draw_string_centre(maxTemp, i * DAYS_FORECAST_RIGHT / 4, (i + 1) * DAYS_FORECAST_RIGHT / 4, DAYS_FORECAST_TOP + 70, 2);
          tft_draw_string_centre(minTemp, i * DAYS_FORECAST_RIGHT / 4, (i + 1) * DAYS_FORECAST_RIGHT / 4, DAYS_FORECAST_TOP + 90, 2);

        }
      }
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
  temperatureUpdated[index] = true;
  strncpy(statusMessage, "Update received for ", CHAR_LEN);
  strncat(statusMessage, readings[index].description , CHAR_LEN);
  statusMessageUpdated = true;
}

void update_mqtt_settings() {

  for (int i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
    if (strcmp(settings[i].description,DESC_ONOFF)==0) {
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
      for (int i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
        if (topic==String(readings[i].topic)) {
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
        if (topic==String(settings[i].topic)) {
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
    if ((millis() > readings[i].lastMessageTime + (MAX_NO_MESSAGE_SEC * 1000)) && (strcmp(readings[i].output,NO_READING)!=0) && (readings[i].changeChar == CHAR_NO_MESSAGE)) {
      readings[i].changeChar = CHAR_NO_MESSAGE;
      sprintf(readings[i].output, NO_READING);
      temperatureUpdated[i] = true;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    network_connect();
  }
}
