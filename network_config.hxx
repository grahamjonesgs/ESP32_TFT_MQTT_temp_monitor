// Update with WiFI and MQTT definitions
#define WIFI_SSID "xxxx"         
#define WIFI_PASSWORD "xxxx"

#define MQTT_SERVER "xxxx.xxxx.xxxx"  // server name or IP
#define MQTT_USER "xxxx"      // username 
#define MQTT_PASSWORD "xxxx"  // password
#define MQTT_PORT 8883              // MQTT port

#define ADAFRUIT_MQTT_SERVER "xxx.xxxx.xxx"  // server name or IP
#define ADAFRUIT_MQTT_USER "xxxxxx"      // username 
#define ADAFRUIT_MQTT_PASSWORD "xxxxx"  // password
#define ADAFRUIT_MQTT_PORT 8883              // MQTT port

#define OPEN_WEATHER_API_KEY "xxx"
#define WEATHERBIT_API_KEY "xxxxxxxxxxxxxx"
#define LOCATION "Munich"
//#define WEATHER_SERVER "api.openweathermap.org"
#define WEATHER_SERVER "api.weatherbit.io"

#define STORED_READING 6
#define READINGS_ARRAY  \

{"Bathroom", "xxxx/xxxx/bathroom/tempset-ambient/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_TEMPERATURE, 0, 0}, \
  {"Living", "xxxx/xxxx/livingroom/tempset-ambient/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_TEMPERATURE, 0,0},\
  {"Kitchen", "xxxx/xxxx/kitchen/tempset-ambient/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_TEMPERATURE, 0,0}, \
  {"Bedroom", "xxxx/xxxx/bedroom/tempset-ambient/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_TEMPERATURE, 0,0}, \
  {"Bathroom", "xxxx/xxxx/bathroom/tempset-humidity/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_HUMIDITY, 0,0}, \
  {"Living", "xxxx/xxxx/livingroom/tempset-humidity/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_HUMIDITY, 0,0},\
  {"Kitchen", "xxxx/xxxx/kitchen/tempset-humidity/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_HUMIDITY, 0,0}, \
  {"Bedroom", "xxxx/xxxx/bedroom/tempset-humidity/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_HUMIDITY, 0,0}, \
  {"living", "xxxx/xxxx/kitchen/battery/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_BATTERY, 0,0}, \
  {"Bathroom", "xxxx/xxxx/bedroom/battery/set", NO_READING, 0.0, {0.0}, CHAR_BLANK, CHAR_BLANK, DATA_BATTERY, 0,0}

  
#define SETTINGS_ARRAY \ 
  {DESC_BRIGHTNESS, "gBridge/xxx", "gBridge/xxx/set", 0.0, DATA_SETTING}, \
  {DESC_CONTRAST, "gBridge/xxxx", "gBridge/xxx/set", 0.0, DATA_SETTING}, \
  {DESC_ONOFF, "gBridge/xxx", "gBridge/xxx/set", 0.0, DATA_ONOFF}