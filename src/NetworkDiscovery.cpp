#include "NetworkDiscovery.h"
#include <WiFi.h>
#include <ESP32Ping.h>
#include "esp_log.h"

static const char* TAG_ND = "NetworkDiscovery";

// A função que cada tarefa "trabalhadora" irá executar
// src/NetworkDiscovery.cpp

// A função que cada tarefa "trabalhadora" irá executar
// In src/NetworkDiscovery.cpp

void pingTask(void *pvParameters) {
  PingTaskParams* params = (PingTaskParams*)pvParameters;
  NetworkDiscovery* scanner = params->scanner;

  ESP_LOGD(TAG_ND, "Iniciando sub-scan para IPs de %d a %d", params->startIp, params->endIp);

  // --- CORREÇÃO DEFINITIVA PARA SILENCIAR O PING ---
  // 1. Guarda o objeto Serial original
  Stream* originalSerial = &Serial;
  // 2. Redireciona a saída do Serial para o "buraco negro" (null)
  Serial.setDebugOutput(false);

  for (int i = params->startIp; i <= params->endIp; i++) {
    IPAddress hostToPing = WiFi.gatewayIP();
    hostToPing[3] = i;

    // A biblioteca vai tentar imprimir via Serial.printf, mas a saída
    // está desativada, então nada aparecerá no terminal.
    if (Ping.ping(hostToPing, 2)) {
      if (xSemaphoreTake(scanner->listMutex, (TickType_t)100) == pdTRUE) {
        if (scanner->deviceCount < MAX_DEVICES) {
          scanner->devices[scanner->deviceCount].ip = hostToPing;
          scanner->devices[scanner->deviceCount].isOnline = true;
          scanner->deviceCount++;
        }
        xSemaphoreGive(scanner->listMutex);
      }
    }
  }

  // 3. Restaura o Serial para o seu comportamento normal
  Serial.setDebugOutput(true);
  
  ESP_LOGD(TAG_ND, "Sub-scan finalizado para IPs de %d a %d", params->startIp, params->endIp);
  
  scanner->activeScanTasks--;
  delete params;
  vTaskDelete(NULL);
}

NetworkDiscovery::NetworkDiscovery() {
  deviceCount = 0;
  activeScanTasks = 0;
  listMutex = NULL;
}

void NetworkDiscovery::setup() {
  listMutex = xSemaphoreCreateMutex();
  if (listMutex) {
    ESP_LOGI(TAG_ND, "Módulo de Descoberta de Rede (Scanner Nativo) inicializado.");
  } else {
    ESP_LOGE(TAG_ND, "Falha ao criar o Mutex do Scanner!");
  }
}

bool NetworkDiscovery::isScanning() {
  return activeScanTasks > 0;
}

void NetworkDiscovery::beginScan() {
  if (isScanning()) {
    ESP_LOGW(TAG_ND, "Scan já está em andamento.");
    return;
  }

  ESP_LOGI(TAG_ND, "--- Iniciando Scan Paralelo da Rede Local ---");
  deviceCount = 0;

  // Divide a rede em 4 partes para 4 tarefas
  const int numTasks = 4;
  activeScanTasks = numTasks;

  for (int i = 0; i < numTasks; i++) {
    // Cria os parâmetros para cada tarefa
    PingTaskParams* params = new PingTaskParams();
    params->startIp = (i * 64) + 1;
    params->endIp = (i + 1) * 64;
    params->scanner = this;

    char taskName[20];
    sprintf(taskName, "PingTask%d", i + 1);

    // Cria e inicia a tarefa
    xTaskCreate(
      pingTask,
      taskName,
      4096, // Tamanho da pilha
      (void*)params, // Passa os parâmetros
      1, // Prioridade
      NULL
    );
  }
}