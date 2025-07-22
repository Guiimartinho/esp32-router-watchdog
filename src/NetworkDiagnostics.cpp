#include "NetworkDiagnostics.h"
#include <HTTPClient.h>
#include <ESP32Ping.h>
#include "esp_log.h"
#include <WiFi.h>

// Inclui o header do NetworkDiscovery para que possamos usar o mutex compartilhado
#include "NetworkDiscovery.h"

static const char *TAG_ND = "NetworkDiagnostics";

// Inicializa o ponteiro do módulo como nulo
NetworkDiagnostics::NetworkDiagnostics() {
  _discoveryModule = nullptr;
}

void NetworkDiagnostics::setup() {
  ESP_LOGI(TAG_ND, "Módulo de Diagnóstico de Rede inicializado com lógica multi-camada.");
}

// Esta função permite que o main.cpp nos "entregue" o mutex
void NetworkDiagnostics::setDiscoveryModule(NetworkDiscovery* discovery) {
  _discoveryModule = discovery;
}

bool NetworkDiagnostics::isInternetConnected() {
  // --- ETAPA 1: Verificação primária com HTTP GET ---
  HTTPClient http;
  WiFiClient client;
  const char* testUrl = "http://clients3.google.com/generate_204";
  ESP_LOGI(TAG_ND, "Etapa 1: Verificando conectividade via HTTP GET para %s", testUrl);
  http.begin(client, testUrl);
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
  
  // Primeiro, verificamos se a conexão com o outro módulo foi feita.
  if (!_discoveryModule || !_discoveryModule->pingMutex) {
      ESP_LOGE(TAG_ND, "Mutex de Ping não está disponível! Abortando verificação de ping.");
      return false;
  }

  for (int i = 0; i < totalPings; i++) {
    ESP_LOGI(TAG_ND, "Etapa 2: Tentativa de Ping %d/%d...", i + 1, totalPings);
    
    bool success = false;
    // Pega a "trava" do mutex compartilhado antes de fazer o ping.
    if (xSemaphoreTake(_discoveryModule->pingMutex, portMAX_DELAY) == pdTRUE) {
        success = Ping.ping(hostToPing, 1);
        // Libera a "trava" para que outras tarefas possam usar o ping.
        xSemaphoreGive(_discoveryModule->pingMutex);
    }
    
    if (success) {
      ESP_LOGI(TAG_ND, "Resultado do Ping: SUCESSO na tentativa %d. Internet está ONLINE (instável).", i + 1);
      return true;
    }
    
    delay(500);
  }

  ESP_LOGE(TAG_ND, "Etapa 2 FALHOU. Todas as %d tentativas de ping falharam. Confirmando internet OFFLINE.", totalPings);
  return false;
}