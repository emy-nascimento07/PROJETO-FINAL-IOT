#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ArduinoOTA.h>

// ============================================================================
// 1. CONFIGURAÇÕES DE REDE (SÓ MEXA AQUI SE O WI-FI OU O IP DO BACKEND MUDAR)
// ============================================================================
const char *ssid = "SENAI-DEV2";
const char *password = "desenvolvimento";

// Troque apenas o IP abaixo pelo IPv4 do PC onde o server.js estiver rodando:
const char *serverUrl = "http://192.168.0.103:3000/api/telemetria";

// Senha e nome para gravação sem cabo (OTA Local)
const char *otaHostname = "esp32-grupo07-mestre";
const char *otaPassword = "senai123";

// ============================================================================
// 2. PINAGEM DO HARDWARE (CONFORME SUA MONTAGEM)
// ============================================================================
#define PINO_DHT 4
#define PINO_LDR 34
#define PINO_BOTAO 0
#define PINO_LED_INTERNO 2
#define PINO_RELE 26
#define PINO_LED_VERDE 25
#define PINO_LED_VERMELHO 27

// Se o seu sensor DHT físico for o AZUL (DHT11), troque DHT22 por DHT11 abaixo:
DHT dht(PINO_DHT, DHT22);

// ============================================================================
// 3. VARIÁVEIS DE CONTROLE
// ============================================================================
volatile bool aulaAtiva = true;
volatile bool cliqueDetectado = false;
volatile unsigned long ultimaInterrupcao = 0;

unsigned long ultimoEnvio = 0;
unsigned long ultimoClique = 0;
unsigned long ultimaTentativaWifi = 0;

void atualizarSaidasFisicas();
void enviarTelemetria(bool comandoManual);
void configurarOTA();

// Interrupção instantânea quando você aperta o botão físico
void IRAM_ATTR aoPressionarBotao()
{
  unsigned long agora = millis();
  if (agora - ultimaInterrupcao > 250)
  {
    aulaAtiva = !aulaAtiva;
    cliqueDetectado = true;
    ultimaInterrupcao = agora;

    digitalWrite(PINO_LED_VERDE, aulaAtiva ? HIGH : LOW);
    digitalWrite(PINO_LED_VERMELHO, aulaAtiva ? LOW : HIGH);
    digitalWrite(PINO_LED_INTERNO, aulaAtiva ? HIGH : LOW);
    digitalWrite(PINO_RELE, aulaAtiva ? HIGH : LOW);
  }
}

void configurarOTA()
{
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("\n[OTA] Iniciando atualizacao via Wi-Fi...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Concluido! Reiniciando ESP32...");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Erro [%u]\n", error);
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] Ativo na rede local (" + String(otaHostname) + ".local)");
}

// ============================================================================
// 4. INICIALIZAÇÃO (SETUP)
// ============================================================================
void setup()
{
  Serial.begin(115200);
  delay(500);
  dht.begin();

  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_LED_INTERNO, OUTPUT);
  pinMode(PINO_RELE, OUTPUT);
  pinMode(PINO_LED_VERDE, OUTPUT);
  pinMode(PINO_LED_VERMELHO, OUTPUT);

  atualizarSaidasFisicas();
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO), aoPressionarBotao, FALLING);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado! IP do ESP32: " + WiFi.localIP().toString());

  configurarOTA();
}

// ============================================================================
// 5. LOOP PRINCIPAL
// ============================================================================
void loop()
{
  ArduinoOTA.handle();

  // Reconecta sozinho se o Wi-Fi cair
  if (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - ultimaTentativaWifi > 5000)
    {
      ultimaTentativaWifi = millis();
      Serial.println("[WIFI] Conexao perdida! Tentando reconectar...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
    return;
  }

  // Envia imediatamente se você apertou o botão físico
  if (cliqueDetectado)
  {
    cliqueDetectado = false;
    ultimoClique = millis();
    Serial.println("[BOTAO] Chave Geral pressionada -> " + String(aulaAtiva ? "AULA ATIVA" : "ENCERRADA"));
    enviarTelemetria(true);
    ultimoEnvio = millis();
  }

  // Lê os sensores e envia para o servidor a cada 2 segundos
  if (millis() - ultimoEnvio > 2000)
  {
    ultimoEnvio = millis();
    enviarTelemetria(false);
  }
}

void atualizarSaidasFisicas()
{
  digitalWrite(PINO_LED_VERDE, aulaAtiva ? HIGH : LOW);
  digitalWrite(PINO_LED_VERMELHO, aulaAtiva ? LOW : HIGH);
  digitalWrite(PINO_LED_INTERNO, aulaAtiva ? HIGH : LOW);
  digitalWrite(PINO_RELE, aulaAtiva ? HIGH : LOW);
}

// ============================================================================
// 6. LEITURA DOS SENSORES E COMUNICAÇÃO COM O BACKEND
// ============================================================================
void enviarTelemetria(bool comandoManual)
{
  float temp = dht.readTemperature();
  float umid = dht.readHumidity();
  int luzBruta = analogRead(PINO_LDR);
  int luzPorcentagem = map(luzBruta, 0, 4095, 0, 100);

  // Proteção contra falha de leitura do DHT (Bug Hunt - Teste 4)
  if (isnan(temp) || isnan(umid))
  {
    Serial.println("[AVISO] Falha ao ler DHT no GPIO 4 (NaN)! Usando valor seguro.");
    temp = 24.0;
    umid = 50.0;
  }

  // Mostra os valores dos sensores no Serial Monitor a cada 2 segundos
  Serial.printf("[SENSORES] Temp: %.1f C | Umid: %.1f %% | LDR: %d%% (ADC: %d) | Rele: %s\n",
                temp, umid, luzPorcentagem, luzBruta, aulaAtiva ? "LIGADO" : "DESLIGADO");

  // Define o nível de alerta
  String statusAlerta = "NORMAL";
  if (temp >= 35.0)
  {
    statusAlerta = "CRITICO";
  }
  else if (temp > 29.0)
  {
    statusAlerta = "ATENCAO";
  }

  HTTPClient http;
  http.setTimeout(800);
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["grupo_id"] = "grupo_07";
  doc["grupo"] = 7;
  doc["nome_subsistema"] = "Console_Mestre_Professor";
  doc["ip_local"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
  doc["firmware_versao"] = "v1.1-OTA";
  doc["temperatura"] = temp;
  doc["umidade"] = umid;
  doc["variavel_principal_nome"] = "Luminosidade_Mesa_Pct";
  doc["variavel_principal_valor"] = luzPorcentagem;
  doc["rele_ativo"] = (bool)aulaAtiva;
  doc["modo_aula"] = aulaAtiva ? "ATIVA" : "ENCERRADA";
  doc["status_alerta"] = statusAlerta;
  doc["comando_fisico"] = comandoManual;

  String jsonString;
  serializeJson(doc, jsonString);

  int httpResponseCode = http.POST(jsonString);
  Serial.println("[HTTP] Resposta do servidor: " + String(httpResponseCode));

  // Recebe comando do Dashboard ("LIGAR" ou "PARAR")
  if (httpResponseCode > 0 && !comandoManual && (millis() - ultimoClique > 3000))
  {
    String resposta = http.getString();
    JsonDocument resDoc;
    deserializeJson(resDoc, resposta);

    String cmd = resDoc["comando"] | "NENHUM";
    if (cmd == "LIGAR" && !aulaAtiva)
    {
      aulaAtiva = true;
      atualizarSaidasFisicas();
      Serial.println("[SERVIDOR] Comando LIGAR recebido -> Rele LIGADO");
    }
    else if (cmd == "PARAR" && aulaAtiva)
    {
      aulaAtiva = false;
      atualizarSaidasFisicas();
      Serial.println("[SERVIDOR] Comando PARAR recebido -> Rele DESLIGADO");
    }
  }

  http.end();
}