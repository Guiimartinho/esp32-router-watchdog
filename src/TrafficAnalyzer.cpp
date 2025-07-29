#include "TrafficAnalyzer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <map>
#include <WiFi.h>

static const char* TAG_TA = "TrafficAnalyzer";
static QueueHandle_t packetQueue_s = NULL;

// Estrutura para estatísticas por dispositivo
struct DeviceStats {
  uint64_t packetCount = 0;
  uint64_t totalBytes = 0;
};
static std::map<String, DeviceStats> statsMap;

// Função para extrair a query de um pacote DNS
String parseDnsQuery(uint8_t* data, int len) {
    if (len < 13) return "";
    char* query = (char*)(data + 12);
    char* q_end = (char*)(data + len);
    String qname = "";
    while (query < q_end && *query != 0) {
        uint8_t label_len = *query++;
        if (label_len == 0 || query + label_len > q_end) break;
        for (int i = 0; i < label_len; i++) {
            qname += (char)query[i];
        }
        query += label_len;
        if (*query != 0) qname += ".";
    }
    return qname;
}

// Callback do sniffer: apenas captura o pacote e envia para a fila
void TrafficAnalyzer::snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA) return;
  
  wifi_promiscuous_pkt_t* packet = (wifi_promiscuous_pkt_t*)buf;
  wifi_pkt_rx_ctrl_t& ctrl = (wifi_pkt_rx_ctrl_t&)packet->rx_ctrl;
  CapturedPacketInfo info;
  info.length = ctrl.sig_len;
  int len_to_copy = (info.length < sizeof(info.payload)) ? info.length : sizeof(info.payload);
  memcpy(info.payload, packet->payload, len_to_copy);
  xQueueSendToBack(packetQueue_s, &info, (TickType_t)0);
}

// Tarefa principal do sniffer: processa a fila, coleta estatísticas e procura por DNS
void snifferTask(void* pvParameters) {
  ESP_LOGI(TAG_TA, "Tarefa de Análise de Tráfego (Produção) iniciada.");
  TrafficAnalyzer* analyzer = (TrafficAnalyzer*)pvParameters;
  unsigned long lastStatsPrint = 0;

  while (!analyzer->_stopSniffer) {
    CapturedPacketInfo receivedPacket;
    if (xQueueReceive(analyzer->_packetQueue, &receivedPacket, pdMS_TO_TICKS(1000))) {
      // Coleta estatísticas de todos os pacotes recebidos
      char macStr[18];
      uint8_t* mac_source_ptr = receivedPacket.payload + 10;
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
              mac_source_ptr[0], mac_source_ptr[1], mac_source_ptr[2],
              mac_source_ptr[3], mac_source_ptr[4], mac_source_ptr[5]);
      statsMap[String(macStr)].packetCount++;
      statsMap[String(macStr)].totalBytes += receivedPacket.length;

      // Análise focada em pacotes DNS
      const int IP_HEADER_OFFSET = 32;
      if (receivedPacket.length > IP_HEADER_OFFSET && receivedPacket.payload[IP_HEADER_OFFSET - 2] == 0x08 && receivedPacket.payload[IP_HEADER_OFFSET - 1] == 0x00) {
          uint8_t ip_protocol = receivedPacket.payload[IP_HEADER_OFFSET + 9];
          if (ip_protocol == 17) { // UDP
              const int UDP_HEADER_OFFSET = IP_HEADER_OFFSET + 20;
              if (receivedPacket.length > UDP_HEADER_OFFSET + 2) {
                  uint16_t dest_port = (receivedPacket.payload[UDP_HEADER_OFFSET] << 8) | receivedPacket.payload[UDP_HEADER_OFFSET + 1];
                  if (dest_port == 53) { // DNS
                      const int DNS_PAYLOAD_OFFSET = UDP_HEADER_OFFSET + 8;
                      String dns_query = parseDnsQuery(receivedPacket.payload + DNS_PAYLOAD_OFFSET, receivedPacket.length - DNS_PAYLOAD_OFFSET);
                      if (dns_query.length() > 0) {
                          ESP_LOGW(TAG_TA, "DNS Query from MAC %s -> %s", macStr, dns_query.c_str());
                      }
                  }
              }
          }
      }
    }

    // A cada 30 segundos, imprime as estatísticas e calcula os totais para a IA
    if (millis() - lastStatsPrint > 30000) {
      lastStatsPrint = millis();
      ESP_LOGI(TAG_TA, "--- Estatísticas de Tráfego (últimos 30s) ---");
      
      uint32_t current_total_packets = 0;
      uint64_t current_total_bytes = 0;

      for(auto const& [mac, stats] : statsMap) {
        ESP_LOGD(TAG_TA, "MAC: %s - Pacotes: %llu, Bytes: %llu", mac.c_str(), stats.packetCount, stats.totalBytes);
        current_total_packets += stats.packetCount;
        current_total_bytes += stats.totalBytes;
      }

      // Guarda os totais para serem usados pelo AnomalyDetector
      analyzer->_total_packets_in_window += current_total_packets;
      analyzer->_total_bytes_in_window += current_total_bytes;
      
      statsMap.clear();
    }
  }

  // Encerramento seguro da tarefa
  ESP_LOGI(TAG_TA, "Tarefa de Análise de Tráfego encerrando graciosamente.");
  analyzer->_snifferTaskHandle = NULL;
  vTaskDelete(NULL);
}

// Construtor
TrafficAnalyzer::TrafficAnalyzer() {
  _packetQueue = NULL;
  _snifferTaskHandle = NULL;
  _stopSniffer = false;
  _total_packets_in_window = 0;
  _total_bytes_in_window = 0;
}

// Setup
void TrafficAnalyzer::setup() {
  _packetQueue = xQueueCreate(100, sizeof(CapturedPacketInfo));
  packetQueue_s = _packetQueue;
  ESP_LOGI(TAG_TA, "Módulo de Análise de Tráfego inicializado.");
}

// Inicia o modo Sniffer
void TrafficAnalyzer::start() {
  if (WiFi.status() != WL_CONNECTED) {
    ESP_LOGE(TAG_TA, "Nao e possivel iniciar o modo promiscuo. Wi-Fi desconectado.");
    return;
  }
  if (_snifferTaskHandle != NULL) return;
  
  _stopSniffer = false;
  // Zera os contadores no início de cada ciclo
  _total_packets_in_window = 0;
  _total_bytes_in_window = 0;

  ESP_LOGI(TAG_TA, "Preparando para modo promíscuo...");
  _target_channel = WiFi.channel();
  memcpy(_target_bssid, WiFi.BSSID(), 6);
  char bssidStr[18];
  sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X", _target_bssid[0], _target_bssid[1], _target_bssid[2], _target_bssid[3], _target_bssid[4], _target_bssid[5]);
  ESP_LOGI(TAG_TA, "Alvo -> Canal: %d, BSSID: %s", _target_channel, bssidStr);
  
  WiFi.disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));

  ESP_LOGI(TAG_TA, "Iniciando modo promíscuo...");
  esp_wifi_set_promiscuous(true);
  
  wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA};
  esp_wifi_set_promiscuous_filter(&filter);
  
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
  esp_wifi_set_channel(_target_channel, WIFI_SECOND_CHAN_NONE);
  
  xTaskCreatePinnedToCore(snifferTask, "Sniffer Task", 8192, this, 2, &_snifferTaskHandle, 0);
}

// Para o modo Sniffer
void TrafficAnalyzer::stop() {
  if (_snifferTaskHandle != NULL) {
    _stopSniffer = true;
    // Aguarda um pouco para a tarefa terminar e processar os últimos pacotes
    vTaskDelay(pdMS_TO_TICKS(1100)); 
  }
  esp_wifi_set_promiscuous(false);
  statsMap.clear(); // Garante que o mapa seja limpo
  ESP_LOGI(TAG_TA, "Modo promíscuo parado.");
}