#include "NetworkDiscovery.h"
#include <WiFi.h>
#include <ESP32Ping.h>
#include "esp_log.h"

static const char* TAG_ND = "NetworkDiscovery";

// A função pingTask não será mais usada, mas a deixamos aqui para não dar erro de compilação.
void pingTask(void *pvParameters) {
  vTaskDelete(NULL);
}

NetworkDiscovery::NetworkDiscovery() {
  deviceCount = 0;
  activeScanTasks = 0;
  listMutex = NULL;
  pingMutex = NULL;
}

void NetworkDiscovery::setup() {
  listMutex = xSemaphoreCreateMutex();
  pingMutex = xSemaphoreCreateMutex(); 
  if (listMutex && pingMutex) {
    ESP_LOGI(TAG_ND, "Módulo de Descoberta de Rede (MODO SEQUENCIAL).");
  } else {
    ESP_LOGE(TAG_ND, "Falha ao criar o Mutex do Scanner!");
  }
}

// Em modo sequencial, isScanning será controlado de outra forma ou pode ser removido
// mas por enquanto, para evitar quebras, deixamos assim.
bool NetworkDiscovery::isScanning() {
  return activeScanTasks > 0;
}

// ===================================================================
// VERSÃO FINAL DA FUNÇÃO beginScan (SEQUENCIAL E BLOQUEANTE)
// ===================================================================
void NetworkDiscovery::beginScan() {
  if (isScanning()) {
    ESP_LOGW(TAG_ND, "Scan já em andamento.");
    return;
  }
  
  activeScanTasks = 1; // Sinaliza que o scan está ativo

  ESP_LOGI(TAG_ND, "--- Iniciando Scan Sequencial da Rede Local ---");
  deviceCount = 0;

  // O loop agora acontece diretamente aqui, sem criar tarefas
  for (int i = 1; i < 255; i++) {
    IPAddress hostToPing = WiFi.gatewayIP();
    hostToPing[3] = i;

    // A chamada ao Ping é protegida pelo mutex, como boa prática
    // para o caso do NetworkDiagnostics tentar usar ao mesmo tempo.
    bool success = false;
    if (xSemaphoreTake(pingMutex, portMAX_DELAY) == pdTRUE) {
        success = Ping.ping(hostToPing, 1);
        xSemaphoreGive(pingMutex);
    }
    
    if (success) {
      ESP_LOGI(TAG_ND, "Dispositivo encontrado em: %s", hostToPing.toString().c_str());
      if (xSemaphoreTake(listMutex, (TickType_t)100) == pdTRUE) {
        if (deviceCount < MAX_DEVICES) {
          devices[deviceCount].ip = hostToPing;
          devices[deviceCount].isOnline = true;
          deviceCount++;
        }
        xSemaphoreGive(listMutex);
      }
    }
    // Pequeno delay para a tarefa principal poder respirar e não bloquear o watchdog
    vTaskDelay(pdMS_TO_TICKS(20)); 
  }

  ESP_LOGI(TAG_ND, "--- Scan Sequencial Concluído ---");
  activeScanTasks = 0; // Sinaliza que o scan terminou
}