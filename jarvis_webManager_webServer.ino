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

//Which pin is used to LEDs
#define led1 32
#define led2 33

// Which pin on the Arduino is connected to the NeoPixels?
#define PIN        12
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
hw_timer_t *Timer0_Cfg = NULL;	

//Setup EEPROM
#define EEPROM_SIZE 120  // 5 alarms * ~24 bytes each
#define MAX_ALARMS 5

//========================USEFUL VARIABLES=============================
unsigned int UTC = -3; // UTC = value in hour (SUMMER TIME) [For example: Paris=UTC+2 => UTC=2 / Brazil=UTC-3]
int Display_backlight = 2; // Adjust it 0 to 7
int led_ring_brightness = 30; // Adjust it 0 to 255
int led_ring_brightness_dark = 3;
int led_ring_brightness_flash = 180; // Adjust it 0 to 255 default value was [250]
const long utcOffsetInSeconds = 3600; // UTC + 1H / Offset in second

bool welcomeAlarmSetup;
bool ringLightStatus;
bool touchLessSensor;

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;

// ========================================================================
//Enables watchdog with setup to timeout for 12 sec
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 12000,
    .idle_core_mask = 1,
    .trigger_panic = true
  };
  
// ========================================================================

// When setting up the NeoPixel library, we tell it how many pixels,
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
TM1637Display display(display_CLK, display_DIO);
int flag = 0;
int flagHalfTimer = 0;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds*UTC);
bool res;

// Use pins 2 and 3 to communicate with DFPlayer Mini
static const uint8_t PIN_MP3_TX = 26; // Connects to module's RX 
static const uint8_t PIN_MP3_RX = 27; // Connects to module's TX 
SoftwareSerial softwareSerial(PIN_MP3_RX, PIN_MP3_TX);
// Create the Player object
DFRobotDFPlayerMini myDFPlayer;

//setup wifi server
WiFiServer server (8090);

// Store http request
String header;
String localIP;
// Var to store local IP from local WebServer
String urlServer;
// Hora Atual
unsigned long currentTime = millis();
// Vez anterior
unsigned long previousTime = 0; 
//Defina o tempo limite em milissegundos 
const long timeoutTime = 2000;
int Status_Led = LOW; 
//Create wifiManager object
WiFiManager manager;   

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

//setup oled 0'96
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define OLED_RESET -1

//Create object Adafruit_SSD1306 - display oled
Adafruit_SSD1306 displayOled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void IRAM_ATTR Timer0_ISR(){
  digitalWrite(keepAlive, !digitalRead(keepAlive));
  //updateDisplayOled(dayStamp, timeStamp, "15");
}

void setup() {

  // blue leds
  pinMode(led1, OUTPUT); 
  pinMode(led2, OUTPUT);
  // switch on the blue leds
  digitalWrite(led1,1);
  digitalWrite(led2,1);

  ringLightStatus = true;

  //Keepalive connected on LED pin 2 dev kit board
  pinMode(keepAlive, OUTPUT);

  //load EEPROM config
  EEPROM.begin(EEPROM_SIZE);
  loadAlarmFromEEPROM();

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!displayOled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  displayOled.display();
  delay(2000); // Pause for 2 seconds
  displayOled.clearDisplay();

  displayOled.setTextSize(1);
  displayOled.setTextColor(SSD1306_WHITE);
  displayOled.setCursor(0, 0);
  displayOled.print("Iniciando ...");
  displayOled.display();

  Serial.begin(115200);
  Serial.println("\n Starting");

  // Init serial port for DFPlayer Mini
  softwareSerial.begin(9600);
  delay(1000);

  do {
    Serial.println("Initializing DFPlayer Mini Module...");
    myDFPlayer.begin(softwareSerial);
    delay(1000);
    //Set serial communictaion time out 500ms
    myDFPlayer.setTimeOut(500);
    delay(1000); 
    // Set volume to maximum (0 to 30).
    myDFPlayer.volume(30);
    delay(1000);
    //----Set different EQ----
    myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
    delay(1000);
    // Play the first MP3 file on the SD card
    myDFPlayer.play(3);
    delay(2000); 

    if (myDFPlayer.available()) {
      Serial.println("DFPlayer Mini module initialized!");
    } else {
      Serial.println("DFPlayer Mini with error!");
    }

  } while (!myDFPlayer.available());


  //manager.resetSettings();

  manager.setTimeout(200);
  //fetches ssid and password and tries to connect, if connections succeeds it starts an access point with the name called "IRON_MAN_ARC" and waits in a blocking loop for configuration
  res = manager.autoConnect("IRON_MAN_ARC","password");
  
  if(!res) {
    Serial.println("failed to connect and timeout occurred");
    myDFPlayer.play(2); //  1_activate_wifi.mp3
    ESP.restart(); //reset and try again
  }

  WiFi.begin();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("*************************************************************");
    localIP = WiFi.localIP().toString();
    Serial.println(localIP);
    Serial.println("*************************************************************");

    urlServer = "http://" + localIP;
    delay(160);
    server.begin();
    welcomeAlarmSetup = false;
  }

  displayOled.clearDisplay();
  displayOled.setCursor(0, 55);
  displayOled.print(urlServer);
  displayOled.display();

  timeClient.begin();

  display.setBrightness(Display_backlight);
  pixels.begin(); // INITIALIZE NeoPixel pixels object
  pixels.setBrightness(led_ring_brightness);

  for(int i=0; i<NUMPIXELS; i++){
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
    pixels.show();
    delay(50);
  }

  myDFPlayer.play(4); //3_setup_complete_1.mp3
  delay(3200);
  myDFPlayer.play(5); //4_intro.mp3
  delay(1000);

  flash_cuckoo();// white flash

  
  // Set timer frequency to 1Mhz
  Timer0_Cfg = timerBegin(1000000);
  // Attach onTimer function to our timer.
  timerAttachInterrupt(Timer0_Cfg, &Timer0_ISR);
  // Set alarm to call onTimer function every second (value in microseconds).
  // Repeat the alarm (third parameter) with unlimited count = 0 (fourth parameter).
  timerAlarm(Timer0_Cfg, 1000000, true, 0);

  //Enables Watchdog 
  /*esp_task_wdt_deinit();            //wdt is enabled by default, so we need to deinit it first
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);*/
  
}

void loop() {

  //Rearm watchdog
  //esp_task_wdt_reset();

  // Update the time
  timeClient.update();

  //Serial.print("Time: ");
  //Serial.println(timeClient.getFormattedTime());
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime ((time_t *)&epochTime); 
  int currentYear = ptm->tm_year+1900;
  //Serial.print("Year: ");
  //Serial.println(currentYear);
  
  int monthDay = ptm->tm_mday;
  //Serial.print("Month day: ");
  //Serial.println(monthDay);

  int currentMonth = ptm->tm_mon+1;
  //Serial.print("Month: ");
  //Serial.println(currentMonth);

  //setup for summer time
  /*if((currentMonth*30 + monthDay) >= 121 && (currentMonth*30 + monthDay) < 331){
  timeClient.setTimeOffset(utcOffsetInSeconds*UTC);} // Change daylight saving time - Summer
  else {timeClient.setTimeOffset((utcOffsetInSeconds*UTC) - 3600);} // Change daylight saving time - Winter*/

  // switch on the ring in blue
  pixels.clear(); // Set all pixel colors to 'off'
  blue_light();

  //setup without summer time
  timeClient.setTimeOffset(utcOffsetInSeconds*UTC);
  // Get formattedTime as HH:mm:ss
  formattedDate = timeClient.getFormattedTime();

  // Get current Date
  dayStamp = String(monthDay) + "-" + String(currentMonth) + "-" + String(currentYear);
  //Serial.print("DATE: ");
  //Serial.println(dayStamp);
  // Extract current time
  timeStamp = formattedDate.substring(0, formattedDate.length());
  //Serial.print("TIME: ");
  //Serial.println(timeStamp);
  updateDisplayOled(dayStamp, timeStamp, "15");

  // Animation every hour
  if(timeClient.getMinutes()== 00 && flag==0)
  {
  
    display_cuckoo();
    flag=1;
  }
   if(timeClient.getMinutes()>=01)
  {
    flag=0;
  }

  // Animation every half hour
  if(timeClient.getMinutes() == 30 && flagHalfTimer == 0)
  {
  
    displayHalfTime();
    flagHalfTimer=1;
  }
   if(timeClient.getMinutes()>=31)
  {
    flagHalfTimer=0;
  }

  //Setup verify alarm from EEPROM
  int weekDayIndex = ptm->tm_wday; // 0=Sunday
  String days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  String today = days[weekDayIndex];

  for (int i = 0; i < MAX_ALARMS; i++) {
    if (alarms[i].days[0] != '\0') {
      if (String(alarms[i].days).indexOf(today) >= 0 &&
          timeClient.getHours() == alarms[i].hour &&
          timeClient.getMinutes() == alarms[i].minute) {
        myDFPlayer.play(1);
        delay(60000);
        break;
      }
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  WiFiClient client = server.available();
  if (client) {
    if (!welcomeAlarmSetup) {
      myDFPlayer.play(6); //5
      welcomeAlarmSetup = true;
    }
    currentTime = millis();
    previousTime = currentTime;
    Serial.println("New Client.");
    String currentLine = "";
    String requestContent = "";
    while (client.connected() && currentTime - previousTime <= timeoutTime) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        header += c;
        requestContent += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            //**************************************************
            if (header.indexOf("GET /L1") >= 0) {
              Status_Led =! Status_Led;
              if (Status_Led) {
                ringLightStatus = true;
              } else {
                ringLightStatus = false;
              }
            }

            if (header.indexOf("GET /set?") >= 0) {
              handleSetAlarm(header);
              client.println("<html><body><h3>Alarm Set!</h3><a href='/'>Back</a></body></html>");
            } else {
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name='viewport' content='width=device-width, initial-scale=1'>");
              client.println("<title> J.A.R.V.I.S </title>");
              client.println("<style>html { font-family: Helvetica; display: inline-block; text-align: center; background-color: #F0FFF0;} .button { background-color: #4CAF50; border-radius: 15px; color: white; padding: 10px 20px; font-size: 20px; margin: 2px; cursor: pointer;} .button2 { background-color: #555555; }</style></head>");
              client.println("<body><h1> J.A.R.V.I.S </h1><h5> Powered by engleandroalves</h5>");

              if (Status_Led) {
                client.print("<a href='/L1'><button class='button'>LED-ON</button></a>");
              } else {
                client.print("<a href='/L1'><button class='button button2'>LED-OFF</button></a>");
              }
              //client.println("<p>I am IRON MAN</p>");

              // Embedded Alarm Form
              client.println("<h2>Set Alarm</h2>");
              client.println("<form action='/set' method='GET'>");
              client.println("<p>Select Days:</p>");
              client.println("<input type='checkbox' name='day' value='Sun'>Sun");
              client.println("<input type='checkbox' name='day' value='Mon'>Mon");
              client.println("<input type='checkbox' name='day' value='Tue'>Tue");
              client.println("<input type='checkbox' name='day' value='Wed'>Wed");
              client.println("<input type='checkbox' name='day' value='Thu'>Thu");
              client.println("<input type='checkbox' name='day' value='Fri'>Fri");
              client.println("<input type='checkbox' name='day' value='Sat'>Sat<br><br>");
              client.println("<label for='hour'>Hour:</label>");
              client.print("<select name='hour'>");
              client.print(createOptions(23));
              client.println("</select>");
              client.println("<label for='minute'>Minute:</label>");
              client.print("<select name='minute'>");
              client.print(createOptions(59));
              client.println("</select><br><br>");
              client.println("<input type='submit' value='Set Alarm'></form>");

			  loadAlarmFromEEPROM();
        
			  client.println("<h3>Stored Alarms:</h3><ul>");
              for (int i = 0; i < MAX_ALARMS; i++) {
                if (alarms[i].days[0] != '\0') {
                  client.print("<li>⏰ ");
                  client.print(alarms[i].hour);
                  client.print(":");
                  if (alarms[i].minute < 10) client.print("0");
                  client.print(alarms[i].minute);
                  client.print(" on ");
                  client.print(alarms[i].days);
                  client.println("</li>");
                }
              }
              client.println("</body></html>");
            }
            client.println();
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    header = "";
    client.stop();
    Serial.println("Client Disconnected.");
    welcomeAlarmSetup = false;
  }


}

void saveAlarmToEEPROM() {
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

void handleAlarmSetup() {
  String html = alarm_html;
  html.replace("%HOUR_OPTIONS%", createOptions(23));
  html.replace("%MINUTE_OPTIONS%", createOptions(59));
  WiFiClient client = server.available();
  if (client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println("Connection: close");
    client.println();
    client.println(html);
    client.stop();
  }
}

void handleSetAlarm(String request) {
  selectedDays = "";
  int paramStart = request.indexOf("/set?");
  if (paramStart > 0) {
    String params = request.substring(paramStart + 5);
    int hIndex = params.indexOf("hour=");
    int mIndex = params.indexOf("minute=");
    if (hIndex > -1 && mIndex > -1) {
      int ampersand = params.indexOf("&", hIndex);
      if (ampersand > -1)
        alarmHour = params.substring(hIndex + 5, ampersand).toInt();
      else
        alarmHour = params.substring(hIndex + 5).toInt();

      ampersand = params.indexOf("&", mIndex);
      if (ampersand > -1)
        alarmMinute = params.substring(mIndex + 7, ampersand).toInt();
      else
        alarmMinute = params.substring(mIndex + 7).toInt();
    }

    int dIndex = 0;
    while ((dIndex = params.indexOf("day=", dIndex)) >= 0) {
      int valStart = dIndex + 4;
      int valEnd = params.indexOf("&", valStart);
      if (valEnd == -1) valEnd = params.length();
      String day = params.substring(valStart, valEnd);
      if (selectedDays.indexOf(day) == -1) selectedDays += day + ",";
      dIndex = valEnd;
    }
  }
  saveAlarmToEEPROM();
}

void updateDisplayOled(String dateStamp, String timeStamp, String temperature) {
  // write the time on the display
  displayOled.clearDisplay();
  displayOled.setTextSize(1);
  displayOled.setCursor(0, 0);
  displayOled.print(dayStamp);
  displayOled.setCursor(95, 0);
  displayOled.setTextSize(1);
  displayOled.print(temperature + "°C");
  displayOled.setTextSize(3);
  displayOled.setCursor(7, 20);
  displayOled.print(timeStamp.substring(0,5));
  displayOled.setTextSize(2);
  displayOled.setCursor(102, 20);
  displayOled.print(timeStamp.substring(6,9));
  displayOled.setTextSize(1);
  displayOled.setCursor(0, 55);
  displayOled.print(urlServer);
  displayOled.display();
}

void blue_light(){
  //Rearm watchdog
  //esp_task_wdt_reset();
  if (ringLightStatus == true || touchLessSensor == true) {
    pixels.setBrightness(led_ring_brightness);
  } else {
    pixels.setBrightness(led_ring_brightness_dark);
  }

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
}

void flash_cuckoo(){
  //Rearm watchdog
  //esp_task_wdt_reset();

	pixels.setBrightness(led_ring_brightness_flash);
    pixels.setPixelColor(0, pixels.Color(250,250,250));
    pixels.setPixelColor(1, pixels.Color(250,250,250));
    pixels.setPixelColor(2, pixels.Color(250,250,250));
    pixels.setPixelColor(3, pixels.Color(250,250,250));
    pixels.setPixelColor(4, pixels.Color(250,250,250));
    pixels.setPixelColor(5, pixels.Color(250,250,250));
    pixels.setPixelColor(6, pixels.Color(250,250,250));
    pixels.setPixelColor(7, pixels.Color(250,250,250));
    pixels.setPixelColor(8, pixels.Color(250,250,250));
    pixels.setPixelColor(9, pixels.Color(250,250,250));
    pixels.setPixelColor(10, pixels.Color(250,250,250));
    pixels.setPixelColor(11, pixels.Color(250,250,250));
    pixels.setPixelColor(12, pixels.Color(250,250,250));
    pixels.setPixelColor(13, pixels.Color(250,250,250));
    pixels.setPixelColor(14, pixels.Color(250,250,250));
    pixels.setPixelColor(15, pixels.Color(250,250,250));
    pixels.setPixelColor(16, pixels.Color(250,250,250));
    pixels.setPixelColor(17, pixels.Color(250,250,250));
    pixels.setPixelColor(18, pixels.Color(250,250,250));
    pixels.setPixelColor(19, pixels.Color(250,250,250));
    pixels.setPixelColor(20, pixels.Color(250,250,250));
    pixels.setPixelColor(21, pixels.Color(250,250,250));
    pixels.setPixelColor(22, pixels.Color(250,250,250));
    pixels.setPixelColor(23, pixels.Color(250,250,250));
    pixels.setPixelColor(24, pixels.Color(250,250,250));
    pixels.setPixelColor(25, pixels.Color(250,250,250));
    pixels.setPixelColor(26, pixels.Color(250,250,250));
    pixels.setPixelColor(27, pixels.Color(250,250,250));
    pixels.setPixelColor(28, pixels.Color(250,250,250));
    pixels.setPixelColor(29, pixels.Color(250,250,250));
    pixels.setPixelColor(30, pixels.Color(250,250,250));
    pixels.setPixelColor(31, pixels.Color(250,250,250));
    pixels.setPixelColor(32, pixels.Color(250,250,250));
    pixels.setPixelColor(33, pixels.Color(250,250,250));
    pixels.setPixelColor(34, pixels.Color(250,250,250));
    pixels.setPixelColor(35, pixels.Color(250,250,250));
    pixels.show();

  for (int i=led_ring_brightness_flash; i>10 ; i--){
  pixels.setBrightness(i);
  pixels.show();
  delay(7);
  }
  blue_light();

}

void display_cuckoo(){
  //Rearm watchdog
  //esp_task_wdt_reset();

  for (int i =0 ; i<88 ; i++){
    display.showNumberDecEx(i,0b01000000,true,2,0);
    display.showNumberDecEx(i,0b01000000,true,2,2);
  }

  display.showNumberDecEx(88,0b01000000,true,2,0);
  display.showNumberDecEx(88,0b01000000,true,2,2);
  
  flash_cuckoo();

  delay(2000);
}

void displayHalfTime() {
  for(int i=0; i<(NUMPIXELS/2);i++){
    pixels.setPixelColor(i, pixels.Color(255, 153, 51));
    pixels.show();
    delay(50);
  }
  for(int i=0; i<(NUMPIXELS/2);i++){
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
    pixels.show();
    delay(50);
  }
  for(int i=18; i<NUMPIXELS; i++){
    pixels.setPixelColor(i, pixels.Color(255, 153, 51));
    pixels.show();
    delay(50);
  }
  for(int i=18; i<NUMPIXELS; i++){
    pixels.setPixelColor(i, pixels.Color(red, green, blue));
    pixels.show();
    delay(50);
  }
}
