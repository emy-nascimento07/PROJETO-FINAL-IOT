#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// Configurações de Rede
const char *ssid = "SENAI-DEV2";
const char *password = "desenvolvimento";
// LEMBRE-SE: Troque IP_DO_SERVIDOR pelo IPv4 do seu computador (ex: 192.168.0.105)
const char *serverUrl = "http://IP_DO_SERVIDOR:3000/api/telemetria";

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

// Variáveis voláteis para funcionar dentro da interrupção instantânea
volatile bool aulaAtiva = true;
volatile bool cliqueDetectado = false;
volatile unsigned long ultimaInterrupcao = 0;

unsigned long ultimoEnvio = 0;
unsigned long ultimoClique = 0;

// Função executada instantaneamente pelo hardware ao apertar o botão
void IRAM_ATTR aoPressionarBotao()
{
  unsigned long agora = millis();
  if (agora - ultimaInterrupcao > 250)
  {
    aulaAtiva = !aulaAtiva;
    cliqueDetectado = true;
    ultimaInterrupcao = agora;

    // Muda os LEDs e o Relé no exato microssegundo do clique
    digitalWrite(PINO_LED_VERDE, aulaAtiva ? HIGH : LOW);
    digitalWrite(PINO_LED_VERMELHO, aulaAtiva ? LOW : HIGH);
    digitalWrite(PINO_RELE, aulaAtiva ? HIGH : LOW);
  }
}

void setup()
{
  Serial.begin(115200);
  dht.begin();

  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_RELE, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_VERMELHO, OUTPUT);

  atualizarSaidasFisicas();

  // Ativa a interrupção instantânea no pino do botão
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO), aoPressionarBotao, FALLING);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP: " + WiFi.localIP().toString());
}

void loop()
{
  // 1. Se o botão foi pressionado, avisa o servidor imediatamente
  if (cliqueDetectado)
  {
    cliqueDetectado = false;
    ultimoClique = millis();
    enviarTelemetria(true);
    ultimoEnvio = millis();
  }

  // 2. Envio periódico a cada 2 segundos
  if (millis() - ultimoEnvio > 2000)
  {
    ultimoEnvio = millis();
    enviarTelemetria(false);
  }
}

void atualizarSaidasFisicas()
{
  if (aulaAtiva)
  {
    digitalWrite(PINO_LED_VERDE, HIGH);
    digitalWrite(PINO_LED_VERMELHO, LOW);
    digitalWrite(PINO_RELE, HIGH);
  }
  else
  {
    digitalWrite(PINO_LED_VERDE, LOW);
    digitalWrite(PINO_LED_VERMELHO, HIGH);
    digitalWrite(PINO_RELE, LOW);
  }
}

void enviarTelemetria(bool comandoManual)
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  float temp = dht.readTemperature();
  float umid = dht.readHumidity();
  int luzBruta = analogRead(PINO_LDR);
  int luzPorcentagem = map(luzBruta, 0, 4095, 0, 100);

  if (isnan(temp))
    temp = 24.0;
  if (isnan(umid))
    umid = 50.0;

  HTTPClient http;
  http.setTimeout(800); // Evita que o Wi-Fi trave o código por muito tempo
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
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

  // Só aceita mudança do servidor se você não apertou o botão nos últimos 3 segundos
  if (httpResponseCode > 0 && !comandoManual && (millis() - ultimoClique > 3000))
  {
    String resposta = http.getString();
    JsonDocument resDoc;
    deserializeJson(resDoc, resposta);

    if (resDoc.containsKey("modoGlobal"))
    {
      String modoServidor = resDoc["modoGlobal"];
      bool novoEstado = (modoServidor == "ATIVA");
      if (novoEstado != aulaAtiva)
      {
        aulaAtiva = novoEstado;
        atualizarSaidasFisicas();
      }
    }
  }
  http.end();
}

