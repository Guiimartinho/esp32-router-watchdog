#include "TrafficAnalyzer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <map>

// --- CORREÇÃO PRINCIPAL: A DEFINIÇÃO DA STRUCT VEM PRIMEIRO ---
// Estrutura para guardar as estatísticas de cada dispositivo
struct DeviceStats {
  uint64_t packetCount = 0;
  uint64_t totalBytes = 0;
};
// -----------------------------------------------------------

static const char* TAG_TA = "TrafficAnalyzer";

// Objeto estático para a fila, acessível pelo callback
static QueueHandle_t packetQueue_s = NULL;

// Mapa para associar um endereço MAC às suas estatísticas
// Agora o compilador já sabe o que é "DeviceStats" e não dará erro.
static std::map<String, DeviceStats> statsMap;

// O callback deve ser muito rápido! Apenas joga na fila.
void TrafficAnalyzer::snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA) {
    return;
  }

  wifi_promiscuous_pkt_t* packet = (wifi_promiscuous_pkt_t*)buf;
  wifi_pkt_rx_ctrl_t& ctrl = (wifi_pkt_rx_ctrl_t&)packet->rx_ctrl;

  CapturedPacketInfo info;
  memcpy(info.mac_source, packet->payload + 6, 6);
  info.length = ctrl.sig_len;

  xQueueSendToBack(packetQueue_s, &info, (TickType_t)0);
}


// A tarefa de análise, que roda em segundo plano e processa a fila
void snifferTask(void* pvParameters) {
  ESP_LOGI(TAG_TA, "Tarefa de Análise de Tráfego iniciada.");
  unsigned long lastStatsPrint = 0;

  for (;;) {
    CapturedPacketInfo receivedPacket;
    if (xQueueReceive(packetQueue_s, &receivedPacket, pdMS_TO_TICKS(1000))) {
      char macStr[18];
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
              receivedPacket.mac_source[0], receivedPacket.mac_source[1], receivedPacket.mac_source[2],
              receivedPacket.mac_source[3], receivedPacket.mac_source[4], receivedPacket.mac_source[5]);

      statsMap[String(macStr)].packetCount++;
      statsMap[String(macStr)].totalBytes += receivedPacket.length;
    }

    if (millis() - lastStatsPrint > 10000) {
      lastStatsPrint = millis();
      ESP_LOGI(TAG_TA, "--- Estatísticas de Tráfego da Rede (Modo Sniffer) ---");
      
      for(auto const& [mac, stats] : statsMap) {
        ESP_LOGI(TAG_TA, "MAC: %s - Pacotes: %llu, Bytes: %llu", mac.c_str(), stats.packetCount, stats.totalBytes);
      }
    }
  }
}

TrafficAnalyzer::TrafficAnalyzer() {
  _packetQueue = NULL;
  _snifferTaskHandle = NULL;
}

void TrafficAnalyzer::setup() {
  _packetQueue = xQueueCreate(50, sizeof(CapturedPacketInfo));
  packetQueue_s = _packetQueue;

  if (_packetQueue) {
    ESP_LOGI(TAG_TA, "Módulo de Análise de Tráfego inicializado.");
  } else {
    ESP_LOGE(TAG_TA, "Falha ao criar a fila de pacotes!");
  }
}

void TrafficAnalyzer::start() {
  if (_snifferTaskHandle != NULL) {
    ESP_LOGW(TAG_TA, "Tarefa de Sniffer já está rodando.");
    return;
  }
  ESP_LOGI(TAG_TA, "Iniciando modo promíscuo e tarefa de análise...");
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
  xTaskCreatePinnedToCore(snifferTask, "Sniffer Task", 8192, this, 2, &_snifferTaskHandle, 0);
}

void TrafficAnalyzer::stop() {
  if (_snifferTaskHandle != NULL) {
    ESP_LOGI(TAG_TA, "Parando a tarefa de análise e modo promíscuo...");
    vTaskDelete(_snifferTaskHandle);
    _snifferTaskHandle = NULL;
  }
  esp_wifi_set_promiscuous(false);
  statsMap.clear();
}