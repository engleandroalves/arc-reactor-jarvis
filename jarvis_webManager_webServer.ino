#include "TM1637Display.h"
#include "Adafruit_NeoPixel.h"
#include "NTPClient.h"
#include "WiFiManager.h"
#include "esp_task_wdt.h"  //Biblioteca do watchdog
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
#include "WebServer.h"
#include "DNSServer.h"
#include <SPI.h>
#include <Wire.h>
#include "Adafruit_GFX.h"
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
//#include <ESP32TimerInterrupt.h>
#include <WiFiClientSecure.h>  // For HTTPS
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <UrlEncode.h>

//Which pin is used to LEDs
#define led1 32
#define led2 33

// Which pin on the Arduino is connected to the NeoPixels?
#define PIN 12
#define display_CLK 18
#define display_DIO 19

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 35

// Adjust the color of the led ring [Original]
// For 'Deep Sky blue' color
#define red 0
#define green 191
#define blue 255
//to see more color option, see the page below:
//https://trimlightsandiego.com/rgb-led-color-codes/

//setup timer 0
#define keepAlive 2
hw_timer_t* Timer0_Cfg = NULL;

//Setup EEPROM
#define EEPROM_SIZE 120  // 5 alarms * ~24 bytes each
#define MAX_ALARMS 5

//============================================USEFUL VARIABLES============================================
bool welcomeAlarmSetup;
bool ringLightStatus;
bool touchLessSensor;
int counterTimer = 0;

//-----------------------------------------Working with Date and time-------------------------------------
unsigned int UTC = -3;         // UTC = value in hour (SUMMER TIME) [For example: Paris=UTC+2 => UTC=2 / Brazil=UTC-3]
int Display_backlight = 2;     // Adjust it 0 to 7
int led_ring_brightness = 30;  // Adjust it 0 to 255
int led_ring_brightness_dark = 3;
int led_ring_brightness_flash = 200;   // Adjust it 0 to 255 default value was [250]
const long utcOffsetInSeconds = 3600;  // UTC + 1H / Offset in second

int currentYear;
int currentMonth;
int monthDay;

// Hora Atual
unsigned long currentTime = millis();
// Vez anterior
unsigned long previousTime = 0;
//Defina o tempo limite em milissegundos
const long timeoutTime = 2000;

//-----------------------------------------Variables to save date and time--------------------------------
String formattedDate;
String dayStamp;
String timeStamp;
unsigned long lastCheckedMinute = -1;
int Status_Led = LOW;

//-----------------------------------------------Weather vars---------------------------------------------
String newCity;
String newCityEncoded;
char storedCity[32] = "Define your city";  // default
String countryCode = "BR";
//const String urlEncodedCidade = encodeURIComponent(cidade);
const char* apiKey = "12f403039882387d1d9f880d575027c9";  // <- You MUST replace this
const char* units = "metric";                             // or "imperial"
String temperature;

//---------------------------------Enables watchdog with setup to timeout for 12 sec----------------------
esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 12000,
  .idle_core_mask = 1,
  .trigger_panic = true
};

//--------------------------------------Ring light NeoPixel------------------------------------------------
// When setting up the NeoPixel library, we tell it how many pixels,
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
TM1637Display display(display_CLK, display_DIO);
int flag = 0;
int flagHalfTimer = 0;

//--------------------------------------Define NTP Client to get time--------------------------------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds* UTC);
bool res;

//-------------------------------------Setup DFPlayer Mini-------------------------------------------------
// Use pins 2 and 3 to communicate with DFPlayer Mini
static const uint8_t PIN_MP3_TX = 26;  // Connects to module's RX
static const uint8_t PIN_MP3_RX = 27;  // Connects to module's TX
SoftwareSerial softwareSerial(PIN_MP3_RX, PIN_MP3_TX);
// Create the Player object
DFRobotDFPlayerMini myDFPlayer;

//----------------------------------------Setup wifi server------------------------------------------------
WiFiServer server(8090);
// Store http request
String header;
String localIP;
// Var to store local IP from local WebServer
String urlServer;
//Create wifiManager object
WiFiManager manager;

//------------------------------------------------Setup Alarm-----------------------------------------------
// Creat a struct to define type Alarm
struct Alarm {
  uint8_t hour;
  uint8_t minute;
  char days[22];
};
Alarm alarms[MAX_ALARMS];

String selectedDays = "";
int alarmHour = 0;
int alarmMinute = 0;

const char alarm_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head><title>ESP32 Alarm Setup</title></head>
  <body>
    <h2>Set Alarm</h2>
    <form action="/set" method="GET">
      <p>Select Days:</p>
      <input type="checkbox" name="day" value="Sun">Sun
      <input type="checkbox" name="day" value="Mon">Mon
      <input type="checkbox" name="day" value="Tue">Tue
      <input type="checkbox" name="day" value="Wed">Wed
      <input type="checkbox" name="day" value="Thu">Thu
      <input type="checkbox" name="day" value="Fri">Fri
      <input type="checkbox" name="day" value="Sat">Sat
      <br><br>

      <label for="hour">Hour:</label>
      <select name="hour">
        %HOUR_OPTIONS%
      </select>

      <label for="minute">Minute:</label>
      <select name="minute">
        %MINUTE_OPTIONS%
      </select>

      <br><br>
      <input type="submit" value="Set Alarm">
    </form>
  </body>
</html>
)rawliteral";

//--------------------------------------Setup display o'led----------------------------------------
//setup oled 0'96
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET -1

//Create object Adafruit_SSD1306 - display oled
Adafruit_SSD1306 displayOled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


//--------------------------------------Timer 0----------------------------------------
void IRAM_ATTR Timer0_ISR() {
  digitalWrite(keepAlive, !digitalRead(keepAlive));
  //updateDisplayOled(dayStamp, timeStamp, "15");
  counterTimer++;
}


void saveAlarmsToEEPROM() {
  int addr = 0;
  for (int i = 0; i < MAX_ALARMS; i++) {
    EEPROM.write(addr++, alarms[i].hour);
    EEPROM.write(addr++, alarms[i].minute);
    for (int j = 0; j < sizeof(alarms[i].days); j++) {
      EEPROM.write(addr++, alarms[i].days[j]);
    }
  }
  EEPROM.commit();
}

void loadAlarmFromEEPROM() {
  int addr = 0;
  for (int i = 0; i < MAX_ALARMS; i++) {
    alarms[i].hour = EEPROM.read(addr++);
    alarms[i].minute = EEPROM.read(addr++);
    for (int j = 0; j < sizeof(alarms[i].days); j++) {
      alarms[i].days[j] = EEPROM.read(addr++);
    }
  }
}

void handleAlarmSetup() {
  String html = alarm_html;
  html.replace("%HOUR_OPTIONS%", createOptions(23));
  html.replace("%MINUTE_OPTIONS%", createOptions(59));
  WiFiClient client = server.available();
  if (client) {
    client.println("HTTP/1.1 200 OK");
    //client.println("Content-type:text/html");
    client.println("Content-Type: text/html; charset=UTF-8");
    client.println("Connection: close");
    client.println();
    client.println(html);
    client.stop();
  }
}

void handleSetAlarm(String request) {
  String selectedDaysTemp = "";
  int hour = 0, minute = 0;

  int paramStart = request.indexOf("/set?");
  if (paramStart > 0) {
    String params = request.substring(paramStart + 5);
    int hIndex = params.indexOf("hour=");
    int mIndex = params.indexOf("minute=");

    if (hIndex > -1 && mIndex > -1) {
      int ampersand = params.indexOf("&", hIndex);
      if (ampersand > -1)
        hour = params.substring(hIndex + 5, ampersand).toInt();
      else
        hour = params.substring(hIndex + 5).toInt();

      ampersand = params.indexOf("&", mIndex);
      if (ampersand > -1)
        minute = params.substring(mIndex + 7, ampersand).toInt();
      else
        minute = params.substring(mIndex + 7).toInt();
    }

    int dIndex = 0;
    bool first = true;
    while ((dIndex = params.indexOf("day=", dIndex)) >= 0) {
      int valStart = dIndex + 4;
      int valEnd = params.indexOf("&", valStart);
      if (valEnd == -1) valEnd = params.length();
      String day = params.substring(valStart, valEnd);
      if (selectedDaysTemp.indexOf(day) == -1) {
        if (!first) selectedDaysTemp += ",";
        selectedDaysTemp += day;
        first = false;
      }
      dIndex = valEnd;
    }
  }

  // Now insert into first available empty slot
  for (int i = 0; i < MAX_ALARMS; i++) {
    if (alarms[i].days[0] == '\0') {
      alarms[i].hour = hour;
      alarms[i].minute = minute;
      selectedDaysTemp.toCharArray(alarms[i].days, sizeof(alarms[i].days));
      break;
    }
  }

  saveAlarmsToEEPROM();
}

void handleDeleteAlarm(int index) {
  if (index < 0 || index >= MAX_ALARMS) return;

  // Shift remaining alarms
  for (int i = index; i < MAX_ALARMS - 1; i++) {
    alarms[i] = alarms[i + 1];
  }

  // Clear the last alarm slot
  alarms[MAX_ALARMS - 1].hour = 0;
  alarms[MAX_ALARMS - 1].minute = 0;
  memset(alarms[MAX_ALARMS - 1].days, 0, sizeof(alarms[MAX_ALARMS - 1].days));

  saveAlarmsToEEPROM();  // Save updated list
}

/*void saveCityToEEPROM() {
  for (int i = 0; i < sizeof(storedCity); i++) {
    EEPROM.write(100 + i, storedCity[i]); // store from position 100+
  }
  EEPROM.commit();
}*/
// Save city to EEPROM
void saveCityToEEPROM(const char* city) {
  for (int i = 0; i < sizeof(storedCity); i++) {
    EEPROM.write(i, city[i]);
    if (city[i] == '\0') break;
  }
  EEPROM.commit();
  Serial.print("Saved to EEPROM: ");
  Serial.println(storedCity);
}

// Load city from EEPROM
void loadCityFromEEPROM() {
  for (int i = 0; i < sizeof(storedCity); i++) {
    storedCity[i] = EEPROM.read(i);
    if (storedCity[i] == '\0') break;
  }
}

String urlDecodeString(String input) {
  String decoded = "";
  char temp[] = "0x00";

  for (unsigned int i = 0; i < input.length(); i++) {
    if (input[i] == '+') {
      decoded += ' ';
    } else if (input[i] == '%') {
      if (i + 2 < input.length()) {
        temp[2] = input[i + 1];
        temp[3] = input[i + 2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}

String fetchWeather(const char* cityName) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return "N/A";
  }

  HTTPClient http;
  String weatherURL = "http://api.openweathermap.org/data/2.5/weather?q=" + String(cityName) + "&appid=" + apiKey + "&units=" + units;
  Serial.println("URL do tempo: " + weatherURL);

  http.begin(weatherURL);
  int httpCode = http.GET();
  String temperature = "N/A";

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println(payload);

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      float temp = doc["main"]["temp"];
      temperature = String(temp, 1) + "°C";
    } else {
      Serial.print("JSON error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpCode);
  }

  http.end();
  return temperature;
}

String createOptions(int max) {
  String options = "";
  for (int i = 0; i <= max; i++) {
    options += "<option value='" + String(i) + "'>" + String(i) + "</option>";
  }
  return options;
}

String processor(const String& var) {
  if (var == "HOUR_OPTIONS") return createOptions(23);
  if (var == "MINUTE_OPTIONS") return createOptions(59);
  return String();
}

void updateDisplayOled(String dateStamp, String timeStamp, String temperature) {
  // write the time on the display
  displayOled.clearDisplay();
  displayOled.setTextSize(1);
  displayOled.setCursor(0, 0);
  displayOled.print(dayStamp);
  displayOled.setCursor(85, 0);
  displayOled.setTextSize(1);
  displayOled.print(temperature.substring(0, 4));
  displayOled.print(char(247));
  displayOled.print("C");
  displayOled.setTextSize(3);
  displayOled.setCursor(7, 20);
  displayOled.print(timeStamp.substring(0, 5));
  displayOled.setTextSize(2);
  displayOled.setCursor(102, 20);
  displayOled.print(timeStamp.substring(6, 9));
  displayOled.setTextSize(1);
  displayOled.setCursor(0, 55);
  displayOled.print(urlServer);
  displayOled.display();
}

void blue_light() {
  //Rearm watchdog
  //esp_task_wdt_reset();

  if (ringLightStatus == true || touchLessSensor == true) {
    pixels.setBrightness(led_ring_brightness);
    pixels.setPixelColor(0, pixels.Color(red, green, blue));
    pixels.setPixelColor(1, pixels.Color(red, green, blue));
    pixels.setPixelColor(2, pixels.Color(red, green, blue));
    pixels.setPixelColor(3, pixels.Color(red, green, blue));
    pixels.setPixelColor(4, pixels.Color(red, green, blue));
    pixels.setPixelColor(5, pixels.Color(red, green, blue));
    pixels.setPixelColor(6, pixels.Color(red, green, blue));
    pixels.setPixelColor(7, pixels.Color(red, green, blue));
    pixels.setPixelColor(8, pixels.Color(red, green, blue));
    pixels.setPixelColor(9, pixels.Color(red, green, blue));
    pixels.setPixelColor(10, pixels.Color(red, green, blue));
    pixels.setPixelColor(11, pixels.Color(red, green, blue));
    pixels.setPixelColor(12, pixels.Color(red, green, blue));
    pixels.setPixelColor(13, pixels.Color(red, green, blue));
    pixels.setPixelColor(14, pixels.Color(red, green, blue));
    pixels.setPixelColor(15, pixels.Color(red, green, blue));
    pixels.setPixelColor(16, pixels.Color(red, green, blue));
    pixels.setPixelColor(17, pixels.Color(red, green, blue));
    pixels.setPixelColor(18, pixels.Color(red, green, blue));
    pixels.setPixelColor(19, pixels.Color(red, green, blue));
    pixels.setPixelColor(20, pixels.Color(red, green, blue));
    pixels.setPixelColor(21, pixels.Color(red, green, blue));
    pixels.setPixelColor(22, pixels.Color(red, green, blue));
    pixels.setPixelColor(23, pixels.Color(red, green, blue));
    pixels.setPixelColor(24, pixels.Color(red, green, blue));
    pixels.setPixelColor(25, pixels.Color(red, green, blue));
    pixels.setPixelColor(26, pixels.Color(red, green, blue));
    pixels.setPixelColor(27, pixels.Color(red, green, blue));
    pixels.setPixelColor(28, pixels.Color(red, green, blue));
    pixels.setPixelColor(29, pixels.Color(red, green, blue));
    pixels.setPixelColor(30, pixels.Color(red, green, blue));
    pixels.setPixelColor(31, pixels.Color(red, green, blue));
    pixels.setPixelColor(32, pixels.Color(red, green, blue));
    pixels.setPixelColor(33, pixels.Color(red, green, blue));
    pixels.setPixelColor(34, pixels.Color(red, green, blue));
    pixels.setPixelColor(35, pixels.Color(red, green, blue));
    pixels.show();
  } else {
    pixel_off();
  }
}

void flash_startup() {
  //Rearm watchdog
  //esp_task_wdt_reset();

  for (int i = 0; i < (NUMPIXELS + 2); i++) {
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
    if(i>1) {
      pixels.setPixelColor(i - 2, pixels.Color(0, 0, 0));
    }
    pixels.show();
    delay(35);
  }

  for (int i = 0; i < (NUMPIXELS + 2); i++) {
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
    if(i>1) {
      pixels.setPixelColor(i - 2, pixels.Color(0, 0, 0));
    }
    pixels.show();
    delay(15);
  }

  for (int i = 0; i < (NUMPIXELS + 2); i++) {
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
    if(i>1) {
      pixels.setPixelColor(i - 2, pixels.Color(0, 0, 0));
    }
    pixels.show();
    delay(5);
  }

  pixels.setBrightness(led_ring_brightness_flash);
  pixels.setPixelColor(0, pixels.Color(250, 250, 250));
  pixels.setPixelColor(1, pixels.Color(250, 250, 250));
  pixels.setPixelColor(2, pixels.Color(250, 250, 250));
  pixels.setPixelColor(3, pixels.Color(250, 250, 250));
  pixels.setPixelColor(4, pixels.Color(250, 250, 250));
  pixels.setPixelColor(5, pixels.Color(250, 250, 250));
  pixels.setPixelColor(6, pixels.Color(250, 250, 250));
  pixels.setPixelColor(7, pixels.Color(250, 250, 250));
  pixels.setPixelColor(8, pixels.Color(250, 250, 250));
  pixels.setPixelColor(9, pixels.Color(250, 250, 250));
  pixels.setPixelColor(10, pixels.Color(250, 250, 250));
  pixels.setPixelColor(11, pixels.Color(250, 250, 250));
  pixels.setPixelColor(12, pixels.Color(250, 250, 250));
  pixels.setPixelColor(13, pixels.Color(250, 250, 250));
  pixels.setPixelColor(14, pixels.Color(250, 250, 250));
  pixels.setPixelColor(15, pixels.Color(250, 250, 250));
  pixels.setPixelColor(16, pixels.Color(250, 250, 250));
  pixels.setPixelColor(17, pixels.Color(250, 250, 250));
  pixels.setPixelColor(18, pixels.Color(250, 250, 250));
  pixels.setPixelColor(19, pixels.Color(250, 250, 250));
  pixels.setPixelColor(20, pixels.Color(250, 250, 250));
  pixels.setPixelColor(21, pixels.Color(250, 250, 250));
  pixels.setPixelColor(22, pixels.Color(250, 250, 250));
  pixels.setPixelColor(23, pixels.Color(250, 250, 250));
  pixels.setPixelColor(24, pixels.Color(250, 250, 250));
  pixels.setPixelColor(25, pixels.Color(250, 250, 250));
  pixels.setPixelColor(26, pixels.Color(250, 250, 250));
  pixels.setPixelColor(27, pixels.Color(250, 250, 250));
  pixels.setPixelColor(28, pixels.Color(250, 250, 250));
  pixels.setPixelColor(29, pixels.Color(250, 250, 250));
  pixels.setPixelColor(30, pixels.Color(250, 250, 250));
  pixels.setPixelColor(31, pixels.Color(250, 250, 250));
  pixels.setPixelColor(32, pixels.Color(250, 250, 250));
  pixels.setPixelColor(33, pixels.Color(250, 250, 250));
  pixels.setPixelColor(34, pixels.Color(250, 250, 250));
  pixels.setPixelColor(35, pixels.Color(250, 250, 250));
  pixels.show();

  for (int i = led_ring_brightness_flash; i > 10; i--) {
    pixels.setBrightness(i);
    pixels.show();
    delay(7);
  }
  blue_light();
}

void flash_cuckoo() {
  //Rearm watchdog
  //esp_task_wdt_reset();

  myDFPlayer.play(8);  //7__clock_time_0.mp3
  delay(200);

  pixels.setBrightness(led_ring_brightness_flash);
  pixels.setPixelColor(0, pixels.Color(250, 250, 250));
  pixels.setPixelColor(1, pixels.Color(250, 250, 250));
  pixels.setPixelColor(2, pixels.Color(250, 250, 250));
  pixels.setPixelColor(3, pixels.Color(250, 250, 250));
  pixels.setPixelColor(4, pixels.Color(250, 250, 250));
  pixels.setPixelColor(5, pixels.Color(250, 250, 250));
  pixels.setPixelColor(6, pixels.Color(250, 250, 250));
  pixels.setPixelColor(7, pixels.Color(250, 250, 250));
  pixels.setPixelColor(8, pixels.Color(250, 250, 250));
  pixels.setPixelColor(9, pixels.Color(250, 250, 250));
  pixels.setPixelColor(10, pixels.Color(250, 250, 250));
  pixels.setPixelColor(11, pixels.Color(250, 250, 250));
  pixels.setPixelColor(12, pixels.Color(250, 250, 250));
  pixels.setPixelColor(13, pixels.Color(250, 250, 250));
  pixels.setPixelColor(14, pixels.Color(250, 250, 250));
  pixels.setPixelColor(15, pixels.Color(250, 250, 250));
  pixels.setPixelColor(16, pixels.Color(250, 250, 250));
  pixels.setPixelColor(17, pixels.Color(250, 250, 250));
  pixels.setPixelColor(18, pixels.Color(250, 250, 250));
  pixels.setPixelColor(19, pixels.Color(250, 250, 250));
  pixels.setPixelColor(20, pixels.Color(250, 250, 250));
  pixels.setPixelColor(21, pixels.Color(250, 250, 250));
  pixels.setPixelColor(22, pixels.Color(250, 250, 250));
  pixels.setPixelColor(23, pixels.Color(250, 250, 250));
  pixels.setPixelColor(24, pixels.Color(250, 250, 250));
  pixels.setPixelColor(25, pixels.Color(250, 250, 250));
  pixels.setPixelColor(26, pixels.Color(250, 250, 250));
  pixels.setPixelColor(27, pixels.Color(250, 250, 250));
  pixels.setPixelColor(28, pixels.Color(250, 250, 250));
  pixels.setPixelColor(29, pixels.Color(250, 250, 250));
  pixels.setPixelColor(30, pixels.Color(250, 250, 250));
  pixels.setPixelColor(31, pixels.Color(250, 250, 250));
  pixels.setPixelColor(32, pixels.Color(250, 250, 250));
  pixels.setPixelColor(33, pixels.Color(250, 250, 250));
  pixels.setPixelColor(34, pixels.Color(250, 250, 250));
  pixels.setPixelColor(35, pixels.Color(250, 250, 250));
  pixels.show();

  for (int i = led_ring_brightness_flash; i > 10; i--) {
    pixels.setBrightness(i);
    pixels.show();
    delay(7);
  }
  blue_light();
}

void display_cuckoo() {
  //Rearm watchdog
  //esp_task_wdt_reset();

  for (int i = 0; i < 88; i++) {
    display.showNumberDecEx(i, 0b01000000, true, 2, 0);
    display.showNumberDecEx(i, 0b01000000, true, 2, 2);
  }

  display.showNumberDecEx(88, 0b01000000, true, 2, 0);
  display.showNumberDecEx(88, 0b01000000, true, 2, 2);

  flash_cuckoo();

  delay(2000);
}

void pixel_off() {
  pixels.setBrightness(led_ring_brightness_dark);
  pixels.setPixelColor(0, pixels.Color(0,0,0));
  pixels.setPixelColor(1, pixels.Color(0,0,0));
  pixels.setPixelColor(2, pixels.Color(0,0,0));
  pixels.setPixelColor(3, pixels.Color(0,0,0));
  pixels.setPixelColor(4, pixels.Color(0,0,0));
  pixels.setPixelColor(5, pixels.Color(0,0,0));
  pixels.setPixelColor(6, pixels.Color(0,0,0));
  pixels.setPixelColor(7, pixels.Color(0,0,0));
  pixels.setPixelColor(8, pixels.Color(0,0,0));
  pixels.setPixelColor(9, pixels.Color(0,0,0));
  pixels.setPixelColor(10, pixels.Color(0,0,0));
  pixels.setPixelColor(11, pixels.Color(0,0,0));
  pixels.setPixelColor(12, pixels.Color(0,0,0));
  pixels.setPixelColor(13, pixels.Color(0,0,0));
  pixels.setPixelColor(14, pixels.Color(0,0,0));
  pixels.setPixelColor(15, pixels.Color(0,0,0));
  pixels.setPixelColor(16, pixels.Color(0,0,0));
  pixels.setPixelColor(17, pixels.Color(0,0,0));
  pixels.setPixelColor(18, pixels.Color(0,0,0));
  pixels.setPixelColor(19, pixels.Color(0,0,0));
  pixels.setPixelColor(20, pixels.Color(0,0,0));
  pixels.setPixelColor(21, pixels.Color(0,0,0));
  pixels.setPixelColor(22, pixels.Color(0,0,0));
  pixels.setPixelColor(23, pixels.Color(0,0,0));
  pixels.setPixelColor(24, pixels.Color(0,0,0));
  pixels.setPixelColor(25, pixels.Color(0,0,0));
  pixels.setPixelColor(26, pixels.Color(0,0,0));
  pixels.setPixelColor(27, pixels.Color(0,0,0));
  pixels.setPixelColor(28, pixels.Color(0,0,0));
  pixels.setPixelColor(29, pixels.Color(0,0,0));
  pixels.setPixelColor(30, pixels.Color(0,0,0));
  pixels.setPixelColor(31, pixels.Color(0,0,0));
  pixels.setPixelColor(32, pixels.Color(0,0,0));
  pixels.setPixelColor(33, pixels.Color(0,0,0));
  pixels.setPixelColor(34, pixels.Color(0,0,0));
  pixels.setPixelColor(35, pixels.Color(0,0,0));
  pixels.show();
}

void displayHalfTime() {
  for (int i = 0; i < (NUMPIXELS); i++) {
    pixels.setPixelColor(i, pixels.Color(255, 153, 51));
    pixels.show();
    delay(100);
  }
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
    pixels.show();
    delay(100);
  }
}

void setupForSummerTime() {
  if ((currentMonth * 30 + monthDay) >= 121 && (currentMonth * 30 + monthDay) < 331) {
    timeClient.setTimeOffset(utcOffsetInSeconds * UTC);  // Change daylight saving time - Summer
  } else {
    timeClient.setTimeOffset((utcOffsetInSeconds * UTC) - 3600);  // Change daylight saving time - Winter*/
  }
}