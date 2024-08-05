#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#define sensorDHT 25
#define sensorUmidadeSolo 34
#define rele 23
#define buzzer 14

DHT_Unified dht(sensorDHT, DHT11);

AsyncWebServer server(80);

const char* ssid_esp = "ESP32_AGRINHO"; 
const char* senha_esp = "12345678";

const char* ssid_rede_wifi;
const char* senha_rede_wifi;

const char index_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML>
  <html>

  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Form</title>
    <style>
      body {
        background-color: rgb(0, 0, 43);
        display: flex;
        justify-content: center;
        align-items: center;
        height: 100vh;
        color: white;
        font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif;
      }

      label {
        display: block;
      }

      input {
        border: 2px solid rgb(0, 0, 43);
        background-color: rgb(255, 255, 255);
      }

      input:focus {
        outline: none;
        border: 2px solid rgb(91, 156, 255);
      }

      button {
        margin-top: 20px;
        padding: 10px;
        width: 100%;
        border: 2px solid rgb(0, 0, 43);
        background-color: rgb(91, 156, 255);
        color: white;
        font-weight: bolder;
        cursor: pointer;
      }
    </style>
  </head>

  <body>
    <form action="/conectar">
      <div class="container">
        <div>
          <label for="SSID">Rede</label>
          <input type="text" name="SSID" />
        </div>
        <div>
          <label for="senha">Senha</label>
          <input type="password" name="senha" />
        </div>
      </div>

      <button type="submit">Enviar</button>
    </form>
  </body>

  </html>
  )rawliteral";

const char connect_page[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML>
  <html>

  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta charset="UTF-8">
    <title>Credenciais Enviadas</title>
    <style>
      body {
        background-color: rgb(0, 0, 43);
        display: flex;
        justify-content: center;
        align-items: center;
        height: 100vh;
        text-align: center;
        color: white;
        font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif;
      }
    </style>
  </head>

  <body>
    <div>
      <h1>Credenciais enviadas!</h1>
      <p>Aguarde o ESP32 conectar-se à rede e inicie o app.</p>
    </div>
  </body>

  </html>
  )rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void conectar(String ssid, String senha){
  WiFi.disconnect();

  Serial.println("Conenctando");
  WiFi.begin(ssid, senha);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado!");
  } else {
    conectar(ssid, senha);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(rele, OUTPUT);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_esp, senha_esp);

  Serial.println();
  Serial.print("Endereço IP do ponto de acesso: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/conectar", HTTP_GET, [](AsyncWebServerRequest *request) {
    String input_ssid = request->getParam("SSID")->value();
    String input_senha = request->getParam("senha")->value();

    ssid_rede_wifi = input_ssid.c_str();
    senha_rede_wifi = input_senha.c_str();

    conectar(ssid_rede_wifi, senha_rede_wifi);

    request->send(200, "text/html", connect_page);
  });

  server.onNotFound(notFound);
  server.begin();
}

void loop() {
  digitalWrite(rele, HIGH);

  int umidadeSolo = analogRead(sensorUmidadeSolo);
  umidadeSolo = map(umidadeSolo, 0, 4095, 100, 0);

  sensors_event_t event;

  dht.humidity().getEvent(&event);
  float umidade = event.relative_humidity;

  dht.temperature().getEvent(&event);
  float temp = event.temperature;
  int temperatura = round(temp);

  if (umidade > 100) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED){
    HTTPClient http;
    http.begin("https://esp32-agrinho.vercel.app/api/dados");
    http.addHeader("Content-Type", "application/json");

    int httpGetCode = http.GET();

    Serial.println("===========================================");

    if (httpGetCode != HTTP_CODE_OK) {
      Serial.print("[HTTP] GET: Erro ao dar GET: ");
      Serial.println(http.errorToString(httpGetCode).c_str());
      
      tone(buzzer, 2000);
      delay(1000);
      noTone(buzzer);

      conectar(ssid_rede_wifi, senha_rede_wifi);
      return;
    }

    Serial.print("[HTTP] GET: ");
    Serial.println(httpGetCode);

    String payload = http.getString();
    DynamicJsonDocument jsonGetDocument(1024);

    deserializeJson(jsonGetDocument, payload);
    JsonArray jsonArray = jsonGetDocument.as<JsonArray>();
    JsonObject jsonObject = jsonArray[0];

    bool automatico = jsonObject["automatico"];
    bool regar = jsonObject["regar"];

    StaticJsonDocument<200> jsonPutDocument;
    jsonPutDocument["planta"] = 1;
    jsonPutDocument["umidade"] = umidade;
    jsonPutDocument["temperatura"] = temperatura;
    jsonPutDocument["solo"] = umidadeSolo;
    jsonPutDocument["automatico"] = automatico;

    if (regar){
      Serial.println("Regando...");
      digitalWrite(rele, LOW);
      delay(1000);
      digitalWrite(rele, HIGH);
      jsonPutDocument["regar"] = false;
    }

    String jsonString;
    serializeJson(jsonPutDocument, jsonString);

    int httpCode = http.PUT(jsonString);

    if (httpCode > 0) {
      Serial.printf("[HTTP] PUT: %d\n", httpCode);
      Serial.println(jsonString);
    } else {
      Serial.printf("[HTTP] PUT... failed, error: %s\n", http.errorToString(httpCode).c_str());
      tone(buzzer, 2000);
      delay(1000);
      noTone(buzzer);
    }

    if (automatico && umidadeSolo < 10) {
      Serial.printf("Solo com baixa umidade: %d%\n", umidadeSolo);
      Serial.println("Regando...");

      digitalWrite(rele, LOW);
      delay(1000);

      digitalWrite(rele, HIGH);
    }
  }

  delay(1000);
}
