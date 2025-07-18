#include "NetworkDiagnostics.h"
#include <HTTPClient.h>
#include <ESP32Ping.h>
#include "esp_log.h"
#include <WiFi.h> // <-- Adicionado para usar o DNSClient

static const char *TAG_ND = "NetworkDiagnostics";

NetworkDiagnostics::NetworkDiagnostics() {}

void NetworkDiagnostics::setup() {
  ESP_LOGI(TAG_ND, "Módulo de Diagnóstico de Rede inicializado com lógica multi-camada.");
}

bool NetworkDiagnostics::isInternetConnected() {
  // --- ETAPA 1: Verificação primária com HTTP GET ---
  HTTPClient http;
  WiFiClient client; // Usado para configurar o DNS
  const char* testUrl = "http://clients3.google.com/generate_204";

  ESP_LOGI(TAG_ND, "Etapa 1: Verificando conectividade via HTTP GET para %s", testUrl);

  // --- MUDANÇA AQUI: Força o uso de um DNS confiável ---
  // Isso evita falhas de DNS locais logo após a conexão Wi-Fi.
  IPAddress dns(8, 8, 8, 8); // DNS do Google
  http.begin(client, testUrl); // Inicia o cliente HTTP com as configurações de DNS
  http.setConnectTimeout(5000); 

  int httpCode = http.GET();
  http.end();

  if (httpCode == 204) {
    ESP_LOGI(TAG_ND, "Resultado do HTTP GET: SUCESSO. Internet está ONLINE.");
    return true; 
  }
  
  ESP_LOGW(TAG_ND, "Etapa 1 FALHOU (Código: %d). Partindo para a Etapa 2: Rajada de Pings.", httpCode);

  // --- ETAPA 2: Verificação secundária com Rajada de Pings ---
  const char* hostToPing = "8.8.8.8";
  const int totalPings = 10;
  const int intervalMs = 500;

  for (int i = 0; i < totalPings; i++) {
    ESP_LOGI(TAG_ND, "Etapa 2: Tentativa de Ping %d/%d...", i + 1, totalPings);
    
    if (Ping.ping(hostToPing, 1)) {
      ESP_LOGI(TAG_ND, "Resultado do Ping: SUCESSO na tentativa %d. Internet está ONLINE (instável).", i + 1);
      return true;
    }
    
    delay(intervalMs);
  }

  ESP_LOGE(TAG_ND, "Etapa 2 FALHOU. Todas as %d tentativas de ping falharam. Confirmando internet OFFLINE.", totalPings);
  return false;
}