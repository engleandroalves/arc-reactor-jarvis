//---------------------------------------Main Setup-------------------------------------------------
void setup() {

  // blue leds
  pinMode(led1, OUTPUT);
  pinMode(led2, OUTPUT);
  // switch on the blue leds
  digitalWrite(led1, 1);
  digitalWrite(led2, 1);

  ringLightStatus = true;

  //Keepalive connected on LED pin 2 dev kit board
  pinMode(keepAlive, OUTPUT);

  //load EEPROM config
  EEPROM.begin(EEPROM_SIZE);
  loadAlarmFromEEPROM();

  loadCityFromEEPROM();
  Serial.print("Loaded city from EEPROM: ");
  Serial.println(storedCity);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!displayOled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }

  //Start pixel setup and ensure all pixels off
  pixels.begin();  // INITIALIZE NeoPixel pixels object
  pixel_off();

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  displayOled.display();
  delay(2000);  // Pause for 2 seconds
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
  res = manager.autoConnect("IRON_MAN_ARC", "password");

  if (!res) {
    Serial.println("failed to connect and timeout occurred");
    myDFPlayer.play(2);  //  1_activate_wifi.mp3
    ESP.restart();       //reset and try again
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


  // ✅ Set timezone properly for localtime()
  setenv("TZ", "UTC-3", 1);
  tzset();
  //setup without summer time
  timeClient.setTimeOffset(utcOffsetInSeconds * UTC);
  timeClient.begin();

  flash_startup();  // white flash

  myDFPlayer.play(4);  //3_setup_complete_1.mp3
  delay(3200);
  myDFPlayer.play(5);  //4_intro.mp3
  delay(1000);

  

  //Check what’s loaded into alarms[] prevously
  for (int i = 0; i < MAX_ALARMS; i++) {
    if (alarms[i].days[0] != '\0') {
      Serial.print("Loaded alarm ");
      Serial.print(i);
      Serial.print(": ");
      Serial.print(alarms[i].hour);
      Serial.print(":");
      Serial.print(alarms[i].minute);
      Serial.print(" on ");
      Serial.println(alarms[i].days);
    }
  }

  temperature = fetchWeather(storedCity);
  Serial.print("Current temp in ");
  Serial.print(storedCity);
  Serial.print(": ");
  Serial.println(temperature);

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