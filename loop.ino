void loop() {

  //Rearm watchdog
  //esp_task_wdt_reset();

  // switch on the ring in blue
  pixels.clear();  // Set all pixel colors to 'off'
  blue_light();

  // Update the time
  timeClient.update();
  //Serial.print("Time: ");
  time_t epochTime = timeClient.getEpochTime();
  struct tm* ptm = localtime(&epochTime);

  currentYear = ptm->tm_year + 1900;
  //Serial.print("Year: ");
  //Serial.println(currentYear);
  monthDay = ptm->tm_mday;
  //Serial.print("Month day: ");
  //Serial.println(monthDay);
  currentMonth = ptm->tm_mon + 1;
  //Serial.print("Month: ");
  //Serial.println(currentMonth);

  //setup for summer time
  //setupForSummerTime()

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
  updateDisplayOled(dayStamp, timeStamp, temperature);

  // Animation every hour
  if (timeClient.getMinutes() == 00 && flag == 0) {

    display_cuckoo();
    flag = 1;
  }
  if (timeClient.getMinutes() >= 01) {
    flag = 0;
  }

  // Animation every half hour
  if (timeClient.getMinutes() == 30 && flagHalfTimer == 0) {

    displayHalfTime();
    flagHalfTimer = 1;
  }
  if (timeClient.getMinutes() >= 31) {
    flagHalfTimer = 0;
  }


  int weekDayIndex = ptm->tm_wday;  // 0 = Sunday
  String days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  String today = days[weekDayIndex];  // 0=Sunday

  // Only check alarms once per new minute
  int nowMinute = timeClient.getMinutes();
  if (nowMinute != lastCheckedMinute) {
    lastCheckedMinute = nowMinute;

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

        Serial.print("Comparing today='");
        Serial.print(today);
        Serial.print("' with stored='");
        Serial.print(alarms[i].days);
        Serial.println("'");
      }
    }

    for (int i = 0; i < MAX_ALARMS; i++) {
      if (alarms[i].days[0] != '\0') {
        if (String(alarms[i].days).indexOf(today) >= 0 && timeClient.getHours() == alarms[i].hour && timeClient.getMinutes() == alarms[i].minute) {
          Serial.print("🚨 Ativou o alarme! ");
          Serial.print(alarms[i].hour);
          Serial.print(":");
          Serial.print(alarms[i].minute);
          Serial.print(" on ");
          Serial.println(today);
          myDFPlayer.play(2);
          break;
        }
      }
    }
  }

  if (counterTimer >= 120) {
    temperature = fetchWeather(storedCity);
    Serial.print("Current temp in ");
    Serial.print(storedCity);
    Serial.print(": ");
    Serial.println(temperature);
    counterTimer = 0;
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  WiFiClient client = server.available();
  if (client) {
    if (!welcomeAlarmSetup) {
      //myDFPlayer.play(6); //5
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
            //client.println("Content-type:text/html");
            client.println("Content-Type: text/html; charset=UTF-8");
            client.println("Connection: close");
            client.println();

            //**************************************************
            if (header.indexOf("GET /L1") >= 0) {
              Status_Led = !Status_Led;
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

              // Embedded City input form

              // Read current city from EEPROM
              loadCityFromEEPROM();

              // Embedded City input form
              client.println("<h3>Weather update</h3>");
              client.print("<p><strong>Current city:</strong> ");
              client.print(urlDecodeString(storedCity));
              client.println("</p>");
              client.println("<form action='/set-city' method='GET'>");
              client.println("<label for=\"city\">Select the city:</label><br>");
              client.println("<input type=\"text\" id=\"city\" name=\"city\" placeholder=\"Enter city name\">");
              client.println("<input type=\"submit\" value=\"Save\">");
              client.println("</form>");

              // Parse the new city from the request (GET /set-city?city=NewYork)
              if (header.indexOf("GET /set-city?city=") >= 0) {
                int start = header.indexOf("city=") + 5;
                int end = header.indexOf(" ", start);
                if (start > 4 && end > start) {
                  newCity = header.substring(start, end);

                  Serial.print("City received: ");
                  Serial.println(newCity);
                  Serial.print("City encoded: ");
                  Serial.println(newCityEncoded);

                  newCity.toCharArray(storedCity, sizeof(storedCity));
                  Serial.print("Stored City: ");
                  Serial.println(storedCity);

                  // Save to EEPROM
                  for (int i = 0; i < sizeof(storedCity); i++) {
                    EEPROM.write(i, storedCity[i]);
                    if (storedCity[i] == '\0') break;
                  }
                  EEPROM.commit();

                  Serial.print("City stored in EEPROM: ");
                  Serial.println(storedCity);
                } else {
                  Serial.println("City parameter malformed or missing.");
                }
              }

              // Embedded Alarm Form
              client.println("<h3>Set Alarm</h3>");
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

              client.println("<h3>Stored Alarms:</h3><ul>");
              for (int i = 0; i < MAX_ALARMS; i++) {
                if (alarms[i].days[0] != '\0') {
                  client.print("<li>⏰  ");
                  client.print(alarms[i].hour);
                  client.print(":");
                  if (alarms[i].minute < 10) client.print("0");
                  client.print(alarms[i].minute);
                  client.print(" on ");
                  client.print(alarms[i].days);
                  client.print("   <a href='/delete?index=");
                  client.print(i);
                  client.println("'><button>Delete</button></a></li>");
                }
              }

              if (header.indexOf("GET /delete?index=") >= 0) {
                int idxStart = header.indexOf("index=") + 6;
                int idxEnd = header.indexOf(" ", idxStart);
                int index = header.substring(idxStart, idxEnd).toInt();
                handleDeleteAlarm(index);
                client.println("<html><body><h3>Alarm Deleted!</h3><a href='/'>Back</a></body></html>");
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