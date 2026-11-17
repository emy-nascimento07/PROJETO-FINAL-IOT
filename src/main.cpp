#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// Configurações de Rede
const char* ssid = "SENAI-DEV2";
const char* password = "desenvolvimento";
const char* serverUrl = "http://192.168.56.1:3000/api/telemetria";

// Pinagem conforme Mapa de Hardware
#define PINO_DHT 4
#define PINO_LDR 34
#define PINO_BOTAO 0
#define PINO_RELE 26
#define PINO_LED_VERDE 25
#define PINO_LED_VERMELHO 27

DHT dht(PINO_DHT, DHT22);

void atualizarSaidasFisicas();
void enviarTelemetria(bool comandoManual);

bool aulaAtiva = true; // Estado inicial: MODO AULA ATIVA

bool ultimoEstadoBotao = HIGH;
unsigned long ultimoEnvio = 0;

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_RELE, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_VERMELHO, OUTPUT);

  atualizarSaidasFisicas();

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
}

void loop() {
  // 1. Leitura do Botão Físico (Chave Geral)
  bool estadoBotao = digitalRead(PINO_BOTAO);
  if (ultimoEstadoBotao == HIGH && estadoBotao == LOW) {
    delay(50); // Debounce
    if (digitalRead(PINO_BOTAO) == LOW) {
      aulaAtiva = !aulaAtiva; // Alterna estado
      atualizarSaidasFisicas();
      enviarTelemetria(true); // Envia imediatamente a mudança de estado
    }
  }
  ultimoEstadoBotao = estadoBotao;

  // 2. Envio periódico de telemetria a cada 2 segundos
  if (millis() - ultimoEnvio > 2000) {
    ultimoEnvio = millis();
    enviarTelemetria(false);
  }
}

void atualizarSaidasFisicas() {
  if (aulaAtiva) {
    digitalWrite(PINO_LED_VERDE, HIGH);
    digitalWrite(PINO_LED_VERMELHO, LOW);
    digitalWrite(PINO_RELE, HIGH); // Liga alimentação da mesa
  } else {
    digitalWrite(PINO_LED_VERDE, LOW);
    digitalWrite(PINO_LED_VERMELHO, HIGH);
    digitalWrite(PINO_RELE, LOW);  // Desliga mesa
  }
}

void enviarTelemetria(bool comandoManual) {
  if (WiFi.status() != WL_CONNECTED) return;

  float temp = dht.readTemperature();
  float umid = dht.readHumidity();
  int luzBruta = analogRead(PINO_LDR);
  int luzPorcentagem = map(luzBruta, 0, 4095, 0, 100);

  if (isnan(temp)) temp = 24.0;
  if (isnan(umid)) umid = 50.0;

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["grupo"] = 7;
  doc["nome"] = "Central Mestre (Prof.)";
  doc["temperatura"] = temp;
  doc["umidade"] = umid;
  doc["luminosidade"] = luzPorcentagem;
  doc["modoAula"] = aulaAtiva ? "ATIVA" : "ENCERRADA";
  doc["comandoFisico"] = comandoManual;

  String jsonString;
  serializeJson(doc, jsonString);

  int httpResponseCode = http.POST(jsonString);

  // Lê resposta do servidor (caso o professor clique no botão do Dashboard Web)
  if (httpResponseCode > 0) {
    String resposta = http.getString();
    StaticJsonDocument<256> resDoc;
    deserializeJson(resDoc, resposta);

    if (resDoc.containsKey("modoGlobal")) {
      String modoServidor = resDoc["modoGlobal"];
      bool novoEstado = (modoServidor == "ATIVA");
      if (novoEstado != aulaAtiva) {
        aulaAtiva = novoEstado;
        atualizarSaidasFisicas();
      }
    }
  }
  http.end();
}
