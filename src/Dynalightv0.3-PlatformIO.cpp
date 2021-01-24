//-------------------ArduinoJson-LICENCE------------------------//
/*Copyright © 2014-2020 Benoit BLANCHON

Permission is hereby granted, free of charge, to any person obtaining a copy of 
this software and associated documentation files (the “Software”), to deal in 
the Software without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the 
Software, and to permit persons to whom the Software is furnished to do so, 
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all 
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION 
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
//----------------------FastLED-LICENCE -----------------------//
/*The MIT License (MIT)
 
  Copyright (c) 2013 FastLED

  Permission is hereby granted, free of charge, to any person obtaining a copy of
  this software and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the rights to
  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
  the Software, and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
//----------------------------INIT----------------------------//
#define FASTLED_INTERNAL // wyłączenie "pragma messages" z biblioteki FastLED
#include <Arduino.h>
#include <FastLED.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define NUM_LEDS 144 //max led - powyżej tej wartości animacja nie będzie wystarczająco płynna
#define DATA_PIN 21  //Zdefiniowane wyjście kontrolera - GPIO 21
#define MAX_DEBUG_MESS 128
struct DataStructure
{ //Struktura konfiguracji zapiswana do pliku json
  short numLED = 4;
  char ssid[32];
  char password[64];
};
DataStructure configData;

unsigned long pingStart = millis(); //zmienne zliczające czas od wysłania ping do ponga
unsigned long pingStop = 0;
unsigned long prevTime = 0;
unsigned long currTime = millis();
AsyncWebServer server(80); //obiekt serwera i WebSocket'u
AsyncWebSocket ws("/ws");

CRGB leds[NUM_LEDS];                                                      //tablica przechowująca kolor diod - rozmiar max wartosci(144)
bool enable[8] = {true, false, false, false, false, false, false, false}; //tablica zmiennych - aktualnie aktywy tryb
const uint8_t headerData[4] = {0xDE, 0xAD, 0xBE, 0xEF};                   //znany po obu stronach nagłówek - wysyłanie danych
const uint8_t headerConf[4] = {0xBA, 0xDC, 0x0D, 0xE0};                   //znany po obu stronach nagłówek - konfiguracja
const uint8_t headerSendIP[4] = {0xCA, 0xFE, 0xBA, 0xBE};                 //znany po obu stronach nagłówek - wysyłanie konf ip do komputera
unsigned long colorValue = 0x8D7535;
char *buffer = new char[MAX_DEBUG_MESS](); //Bufor do wysyłania wiadomości do konsoli online
//-----------------------END-INIT------------------------------//
//------------------Deklaracja funkcji-------------------------//
/* #region  Initialization of all functions */
void serialDataHeaderListener(bool *tempEnable);
void saveSerialConfig(uint8_t dataLenght);
char *splitDataSerial(char *recivedData, byte partPointerStart, byte partPointerStop);
void showDynaLight();
void solidColor(bool enable, unsigned long color);
bool asyncPeriodBool(unsigned long period);
void blinkLED(byte numBlinks, int onOffTime);
void debug(char *mess);
void notifyClientsStatus();
void notifyClientsColor();
void handleWebSocketMessage(AsyncWebSocketClient *client,
                            void *arg,
                            uint8_t *data,
                            size_t len);
void onEvent(AsyncWebSocket *server,
             AsyncWebSocketClient *client,
             AwsEventType type,
             void *arg,
             uint8_t *data,
             size_t len);
void initWebSocket();
bool loadConfig();
bool saveConfig(DataStructure configData);
/* #endregion */
//--------------------Deklaracja-END---------------------------//
//-----------------------WebSocket-----------------------------//
/* #region  WebSocket */
void debug(char *mess)
{
  ws.textAll("M" + String(mess));
  memset(buffer, 0, MAX_DEBUG_MESS);
}
void notifyFPS(bool b)
{
  if (b)
  {
    ws.textAll("F" + String(FastLED.getFPS(), DEC));
  }
  else
  {
    ws.textAll("F" + String(0, DEC));
  }
}
void notifyClientsStatus()
{ //funkcja wysyła wszystkim klientom wiadomość o aktualnie aktywnej funkcji
  int a = -1;
  for (int i = 0; i <= 7; i++)
  {
    if (enable[i] == true)
      a = i;
  }
  ws.textAll(String("S") + String(a, DEC));
  sprintf(buffer, "Wybrano tryb %d - %s%s%s%s%s%s%s%s%s.\n", a,
          a == 0 ? "Dynalight" : "",
          a == 1 ? "Stałe podświetlenie" : "",
          a == 2 ? "" : "",
          a == 3 ? "" : "",
          a == 4 ? "" : "",
          a == 5 ? "" : "",
          a == 6 ? "" : "",
          a == 7 ? "" : "",
          a == -1 ? "Wyłącz wszytsko" : "");
  debug(buffer);
}

void notifyClientsColor()
{
  ws.textAll("C" + String(colorValue, HEX));
  sprintf(buffer, "Zmieniono kolor stałego podświetlenia na #%06lx.\n", colorValue);
  debug(buffer);
}

void handleWebSocketMessage(AsyncWebSocketClient *client,
                            void *arg,
                            uint8_t *data,
                            size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    char *tempData = (char *)data;
    char compare = tempData[0];
    if (compare == 'S')
    {
      int ModeState = atoi(tempData + 1);
      for (int i = 0; i <= 7; i++) //ustawienie wszystkich wartości na false w tablicy enable
        enable[i] = false;
      if (ModeState != -1)
      {
        enable[ModeState] = true;    //Mode ON
        solidColor(enable[1], colorValue); //wyświetl kolor po uruchomieniu funkcji
      }
      else
      {
        notifyFPS(false);
        FastLED.clear();
      }
      notifyClientsStatus(); //i powiadom o tym reszte klientów
    }
    if (compare == 'C')
    {
      unsigned long tempColor = strtoul(tempData + 1, NULL, 16);
      colorValue = tempColor;
      solidColor(enable[1], colorValue);
      notifyClientsColor();
    }
  }
}

void onEvent(AsyncWebSocket *server,
             AsyncWebSocketClient *client,
             AwsEventType type,
             void *arg,
             uint8_t *data,
             size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    sprintf(buffer, "\nWebSocket client #%u connected from %s\n", //wysyłamy informacje do konsioolki online przez WebSocket
            client->id(), client->remoteIP().toString().c_str());
    debug(buffer);
    notifyClientsStatus();
    notifyClientsColor();
    notifyFPS(true);
    pingStart = millis();
    client->ping();
    break;
  case WS_EVT_DISCONNECT:
    sprintf(buffer, "WebSocket client #%u disconnected\n", client->id());
    debug(buffer);
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(client, arg, data, len);
    pingStart = millis();
    client->ping();
    break;
  case WS_EVT_PONG:
    pingStop = millis();
    ws.text(client->id(), String("P") + String(pingStop - pingStart));
    ws.text(client->id(), String("R") + String(WiFi.RSSI()));
    break;
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()

{ //Inicializacja Websocket'u
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}
/* #endregion */
//---------------------WebSocket-End---------------------------//
//---------------------SPIFFS-funkcje--------------------------//
/* #region  SPIFFS */
bool loadConfig()
{
  File configFile = SPIFFS.open("/config.json", "r"); //Otworz plik config.json z pamiecia flash
  if (!configFile)
  {
    Serial.println("Nie udaco sie otworzyc kliku konfiguracyjnego...");
    return false;
  }
  size_t size = configFile.size();
  if (size > 256)
  { //max rozpiar pliku konfiguracyjnego - 256 znaki
    Serial.println("Rozmiar pliku jest za duży - Error.");
    return false;
  }
  std::unique_ptr<char[]> buf(new char[size]); //Alokujemy buffer do szczytania configuracji
  configFile.readBytes(buf.get(), size);       //klasa posiada konstruktor i dekonstruktor dlatego jej uzywamy

  StaticJsonBuffer<256> jsonBuffer; // alokacja pamięci dla pliku Json
  JsonObject &json = jsonBuffer.parseObject(buf.get());
  if (!json.success())
  {
    Serial.println("Błąd analizy pliku Json.");
    return false;
  }
  const short numLEDJson = atoi(json["numLED"]);
  const char *ssidJson = json["ssid"];
  const char *passwordJson = json["pass"];
  configData.numLED = numLEDJson;
  strcpy(configData.ssid, ssidJson);
  strcpy(configData.password, passwordJson);
  configFile.close();
  return true;
}
bool saveConfig(DataStructure configData)
{
  SPIFFS.remove("/config.json"); // Usuwamy zawartość pliku JSON, upewniajac sie że nie dopiszemy nic do istniejacych danych

  File file = SPIFFS.open("/config.json", FILE_WRITE); // otwieramy plik w trybie zapisywania (jezeli plik nie istnieje zostanie stworzony)
  if (!file)
  {
    Serial.println(F("Nie udało sie utworzyć pliku."));
    return false;
  }
  Serial.println("Utworzono plik konfiguracyjny.");
  StaticJsonBuffer<1024> jsonBuffer; // alokacja pamięci dla pliku Json

  JsonObject &json = jsonBuffer.createObject(); // tworzymy nowy obiekt Json

  json["numLED"] = configData.numLED; // przypisujemy wartości
  json["ssid"] = configData.ssid;
  json["pass"] = configData.password;

  if (json.printTo(file) == 0)
  {
    Serial.println(F("Blad zapisu do pliku..."));
    return false;
  }
  file.close();
  return true;
}
/* #endregion */
//----------------------SPIFFS-End-----------------------------//
//-------------------------SEPUP-------------------------------//
/* #region  SETUP */
void setup()
{
  //------Konfiguracja hardware'u i poł. szeregowego-------------//
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH); //wywaj dodany sczytywanie z eeprom'u zapisanej w konfiguracji wartości
  Serial.begin(921600);  //ustawiamy przepustowość łącza szeregowego
  Serial.setTimeout(10); //ustawiamy czas utracenia połączaniaSERIAL, jest bardzo niski bo nie działamy synchronicznie
  delay(100);            //migamy ledem na płytce sygnalizując że płytka się uruchomiła
  Serial.print("\r\n\r\n\r\n\r\n\r\n");
  delay(100);
  //-------------Inicjalizacja-SPIFFS----------------------------//
  if (!SPIFFS.begin(true))
  {
    Serial.printf("Blad podczas uruchamiania SPIFFS\r\n"); //Wgrywanie konfiguracji
    return;
  }
  else
  {
    if (loadConfig())
    {
      Serial.println("Wgrano plik konfiguracyjny");
      Serial.printf("numledjson config short: %d \n", configData.numLED);
    }
    else
    {
      memset(configData.ssid, 0, sizeof(configData.ssid));
      memset(configData.password, 0, sizeof(configData.password));
    }
  }
  //-----------------Definicja paska LED-----------------------//
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, configData.numLED).setCorrection(TypicalSMD5050);
  //----------------Łaczenie z siecią WiFi---------------------//
  Serial.println();
  WiFi.mode(WIFI_STA); //łączenie do sieci - jako klient AP
  WiFi.begin(configData.ssid, configData.password);
  Serial.printf("Laczenie do %s.\r\n", configData.ssid); //próba łączenia - jeżeli ssid jest nie poprawne zakonczy sie po ok 3 sek, jeżeli hało - 10s*3
  byte retryCounter = 1;
  while (WiFi.waitForConnectResult() != WL_CONNECTED && retryCounter <= 3)
  {
    Serial.printf("\r\nProba laczenia: %d.", retryCounter); //Łączenie z wifi
    retryCounter++;
  }
  digitalWrite(2, LOW);
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("\r\nPolaczono z %s.\r\nIP: %s\r\n", configData.ssid, WiFi.localIP().toString().c_str());
    initWebSocket();
    //-------------Uruchomienie-serwera-request----------------//
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { //wysyłamy strone do klienta
      request->send(SPIFFS, "/index.html", "text/html", false);
    });
    server.on("/json", HTTP_GET, [](AsyncWebServerRequest *request) { //wysyłamy strone do klienta
      request->send(SPIFFS, "/config.json", "text/html", false);
    });
    //---------------------------------------------------------//
    server.begin(); //Start server
    Serial.printf("HTTP server started\r\n");
    Serial.printf("Kontroler gotowy\r\n"); //wysyłamy komunikat
    blinkLED(5, 100);                      //migamy ledem na płytce sygnalizując że płytka się uruchomiła
    sprintf(buffer, "Połączono z %s.\nIP serwera : %s\n", configData.ssid, WiFi.localIP().toString().c_str());
    debug(buffer);
  }
  else
  {
    Serial.printf("\r\nNie polaczono z siecią WiFi... kontroler kontynuuje prace w trybie offline.\r\n");
    blinkLED(15, 100);
  }
}
/* #endregion */
//----------------------SETUP-END------------------------------//
//----------------------MAIN-LOOP------------------------------//
/* #region  MAIN LOOP */
void loop()
{
  serialDataHeaderListener(&enable[0]);
  ws.cleanupClients(); // czyszczenie nieaktywnych klientów - ponieważ JS po stronie klienta czasami nie zamyka poprawnie połączenia
  FastLED.show();
  while (Serial.available() > 0)
  { //Czyszczenie bufferu serial
    Serial.read();
  }
}
/* #endregion */
//----------------------MAIN-LOOP-END--------------------------//
//--------------------SERIAL-FUNCTIONS-------------------------//
/* #region  MAIN SERIAL CONNECTION FUNCTIONS */
void serialDataHeaderListener(bool *tempEnable)
{
  while (*tempEnable)
  {
    while (Serial.available() == 0 && *tempEnable)
    {
      if (asyncPeriodBool(5000))
      {
        notifyFPS(true);
      }
    }
    uint8_t b = Serial.read();
    bool similarToHeaderData = false;
    bool similarToHeaderConf = false;
    bool similarToHeaderSend = false;
    if (b == headerData[0])
    {
      similarToHeaderData = true;
      for (int i = 1; similarToHeaderData && (i < sizeof(headerData)); i++)
      {
        while (Serial.available() == 0 && *tempEnable)
        {
          if (asyncPeriodBool(5000))
            debug("w dynalight2\n");
        }
        b = Serial.read();
        if (b != headerData[i])
        {
          // jeżeli któryś bit nie zgadza się z nagłowkiem warunek nie zostaje spełniony
          similarToHeaderData = false;
          if (b == headerConf[0])
          {
            similarToHeaderConf = true;
          }
          else if (b == headerSendIP[0])
          {
            similarToHeaderSend = true;
          }
        }
      }
    }
    if (b == headerConf[0])
    {
      similarToHeaderConf = true;
      for (int i = 1; similarToHeaderConf && (i < sizeof(headerConf)); i++)
      {
        while (Serial.available() == 0)
        {
        } //kolejne dane
        b = Serial.read();
        if (b != headerConf[i])
        {
          // jeżeli któryś bit nie zgadza się z nagłowkiem warunek nie zostaje spełniony
          similarToHeaderConf = false;
          if (b == headerData[0])
          {
            similarToHeaderData = true;
          }
          else if (b == headerSendIP[0])
          {
            similarToHeaderSend = true;
          }
        }
      }
    }
    if (b == headerSendIP[0])
    {
      similarToHeaderSend = true;
      for (int i = 1; similarToHeaderSend && (i < sizeof(headerSendIP)); i++)
      {
        while (Serial.available() == 0)
        {
        } //kolejne dane
        b = Serial.read();
        if (b != headerSendIP[i])
        {
          // jeżeli któryś bit nie zgadza się z nagłowkiem warunek nie zostaje spełniony
          similarToHeaderSend = false;
          if (b == headerSendIP[0])
          {
            similarToHeaderData = true;
          }
          else if (b == headerConf[0])
          {
            similarToHeaderConf = true;
          }
        }
      }
    }
    if (similarToHeaderData && enable[0])
    {
      showDynaLight();
      break;
    }
    if (similarToHeaderConf)
    {
      Serial.println("Odczytano konfigurację\n");
      while (Serial.available() == 0)
      {
      }
      b = Serial.read();
      saveSerialConfig(b);
      break;
    }
    if (similarToHeaderSend)
    {
      Serial.printf("%c%c%c%c%c%s%c", headerSendIP[0],
                    headerSendIP[1],
                    headerSendIP[2],
                    headerSendIP[3],
                    WiFi.status(),
                    WiFi.localIP().toString().c_str(),
                    255);
      break;
    }
  }
}

void saveSerialConfig(uint8_t dataLenght)
{
  DataStructure configSerialTest;
  char *recivedData = new char[256]();
  int bytesRead = 0;
  while (bytesRead < dataLenght)
  {
    bytesRead += Serial.readBytes(recivedData + bytesRead, dataLenght - bytesRead);
  }
  Serial.println(recivedData);
  byte partPointerStart = 1;
  byte partPointerStop = recivedData[partPointerStart - 1] + 1;
  Serial.printf("start: %d, stop: %d\n", partPointerStart, partPointerStop);
  configSerialTest.numLED = atoi(splitDataSerial(recivedData, partPointerStart, partPointerStop));
  partPointerStart = partPointerStop + 1;
  partPointerStop = recivedData[partPointerStart - 1] + 1 + partPointerStop;
  Serial.printf("start: %d, stop: %d\n", partPointerStart, partPointerStop);
  strcpy(configSerialTest.ssid, splitDataSerial(recivedData, partPointerStart, partPointerStop));
  partPointerStart = partPointerStop + 1;
  partPointerStop = recivedData[partPointerStart - 1] + 1 + partPointerStop;
  Serial.printf("start: %d, stop: %d\n", partPointerStart, partPointerStop);
  strcpy(configSerialTest.password, splitDataSerial(recivedData, partPointerStart, partPointerStop));
  Serial.printf("test: %d, %s, %s|||\n", configSerialTest.numLED, configSerialTest.ssid, configSerialTest.password);
  //*****************************************************************************************//

  if (saveConfig(configSerialTest))
  {
    Serial.println("Zapisano konfiguracje");
    ESP.restart();
  }
  else
  {
    Serial.println("Blad zapisu konfiguracji");
  }
}
char *splitDataSerial(char *recivedData, byte partPointerStart, byte partPointerStop)
{
  byte i = partPointerStart;
  byte j = 0;
  char *part = new char[64]();
  for (i; i < partPointerStop; i++)
  {
    part[j] = recivedData[i];
    j++;
  }
  return part;
}
void showDynaLight()
{
  int bytesRead = 0;
  while (bytesRead < (configData.numLED * 3))
  {
    bytesRead += Serial.readBytes(((uint8_t *)leds) + bytesRead, (configData.numLED * 3) - bytesRead);
  }
}

void solidColor(bool enable, unsigned long color)
{
  if (enable)
  {
    for (int led = 0; led < configData.numLED; led++)
    {
      leds[led] = color;
    }
  }
}

/* #endregion */
//-----------------SERIAL-FUNCTIONS-END------------------------//
//-----------------ADDITIONAL-FUNCTIONS------------------------//
/* #region  Additional functions */

bool asyncPeriodBool(unsigned long period)
{
  currTime = millis();
  if (currTime - prevTime >= period)
  {
    prevTime = currTime;
    return true;
  }
  else
  {
    return false;
  }
}

void blinkLED(byte numBlinks, int onOffTime)
{ //funkcja miganie leda na płytce - do debugowania
  for (byte n = 0; n < numBlinks; n++)
  {
    digitalWrite(2, HIGH);
    delay(onOffTime);
    digitalWrite(2, LOW);
    delay(onOffTime);
  }
}

/* #endregion */