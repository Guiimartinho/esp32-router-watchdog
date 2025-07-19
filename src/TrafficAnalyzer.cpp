#include "TrafficAnalyzer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <map>
#include <WiFi.h>

static const char* TAG_TA = "TrafficAnalyzer";

// Variáveis estáticas, pois o callback não pertence a um objeto
static QueueHandle_t packetQueue_s = NULL;
static uint8_t target_bssid_s[6]; // Cópia do BSSID para o callback

// Estrutura para guardar as estatísticas de cada dispositivo
struct DeviceStats {
  uint64_t packetCount = 0;
  uint64_t totalBytes = 0;
};
static std::map<String, DeviceStats> statsMap;

// --- Função para parsear pacotes DNS ---
// Implementação simplificada para extrair o nome de uma consulta DNS (UDP porta 53)
String parseDnsQuery(uint8_t* data, int len) {
    // O payload UDP começa após o cabeçalho Ethernet (14), IP (20) e UDP (8)
    if (len <= 42) return "";
    uint8_t* dns_payload = data + 42;
    int dns_len = len - 42;
    
    // O nome da consulta começa no 13º byte do payload DNS
    if (dns_len < 13) return "";
    
    char* query = (char*)(dns_payload + 12);
    char* q_end = (char*)(dns_payload + dns_len);
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

// --- CALLBACK "SMART" COM FILTROS ---
void TrafficAnalyzer::snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  // Ignoramos pacotes de beacon, controle, etc. Focamos em pacotes de dados.
  if (type != WIFI_PKT_DATA) return;

  wifi_promiscuous_pkt_t* packet = (wifi_promiscuous_pkt_t*)buf;
  
  // FILTRO POR BSSID: Ignora pacotes que não são da nossa rede
  if (memcmp(packet->payload + 10, target_bssid_s, 6) != 0) {
      return;
  }

  wifi_pkt_rx_ctrl_t& ctrl = (wifi_pkt_rx_ctrl_t&)packet->rx_ctrl;
  CapturedPacketInfo info;
  memcpy(info.mac_source, packet->payload + 4, 6); // MAC de origem
  info.length = ctrl.sig_len;
  info.dns_query = "";

  // SNIFFER UDP DNS: Verifica se é um pacote IP -> UDP -> DNS
  // Identificador de pacote IP = 0x0800
  uint16_t ether_type = (packet->payload[12] << 8) | packet->payload[13];
  if (ether_type == 0x0800) {
    // Identificador de protocolo UDP = 17
    uint8_t ip_protocol = packet->payload[23];
    if (ip_protocol == 17) { // 17 = UDP
      uint16_t dest_port = (packet->payload[36] << 8) | packet->payload[37];
      if (dest_port == 53) {
          info.dns_query = parseDnsQuery(packet->payload, info.length);
      }
    }
  }

  xQueueSendToBack(packetQueue_s, &info, (TickType_t)0);
}

// Tarefa que processa a fila de pacotes
void snifferTask(void* pvParameters) {
  ESP_LOGI(TAG_TA, "Tarefa de Análise de Tráfego iniciada.");
  TrafficAnalyzer* analyzer = (TrafficAnalyzer*)pvParameters;
  unsigned long lastStatsPrint = 0;

  for (;;) {
    CapturedPacketInfo receivedPacket;
    if (xQueueReceive(analyzer->_packetQueue, &receivedPacket, pdMS_TO_TICKS(1000))) {
      char macStr[18];
      sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
              receivedPacket.mac_source[0], receivedPacket.mac_source[1], receivedPacket.mac_source[2],
              receivedPacket.mac_source[3], receivedPacket.mac_source[4], receivedPacket.mac_source[5]);

      statsMap[String(macStr)].packetCount++;
      statsMap[String(macStr)].totalBytes += receivedPacket.length;

      // Se uma consulta DNS foi capturada, imprime!
      if (receivedPacket.dns_query.length() > 0) {
        ESP_LOGW(TAG_TA, "DNS Query from MAC %s -> %s", macStr, receivedPacket.dns_query.c_str());
      }
    }

    if (millis() - lastStatsPrint > 30000) {
      lastStatsPrint = millis();
      ESP_LOGI(TAG_TA, "--- Estatísticas de Tráfego (últimos 30s) ---");
      for(auto const& [mac, stats] : statsMap) {
        ESP_LOGI(TAG_TA, "MAC: %s - Pacotes: %llu, Bytes: %llu", mac.c_str(), stats.packetCount, stats.totalBytes);
      }
      statsMap.clear(); // Limpa para a próxima janela de 30s
    }
  }
}

TrafficAnalyzer::TrafficAnalyzer() {
  _packetQueue = NULL;
  _snifferTaskHandle = NULL;
}

void TrafficAnalyzer::setup() {
  _packetQueue = xQueueCreate(100, sizeof(CapturedPacketInfo));
  packetQueue_s = _packetQueue;
  if (_packetQueue) { ESP_LOGI(TAG_TA, "Módulo de Análise de Tráfego inicializado."); } 
  else { ESP_LOGE(TAG_TA, "Falha ao criar a fila de pacotes!"); }
}

void TrafficAnalyzer::start() {
  if (_snifferTaskHandle != NULL) return;

  ESP_LOGI(TAG_TA, "Preparando para modo promíscuo...");
  // 1. Salva o canal e o BSSID da rede atual ANTES de desconectar
  _target_channel = WiFi.channel();
  memcpy(_target_bssid, WiFi.BSSID(), 6);
  memcpy(target_bssid_s, _target_bssid, 6);
  
  char bssidStr[18];
  sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X", _target_bssid[0], _target_bssid[1], _target_bssid[2], _target_bssid[3], _target_bssid[4], _target_bssid[5]);
  ESP_LOGI(TAG_TA, "Alvo -> Canal: %d, BSSID: %s", _target_channel, bssidStr);
  
  // 2. Desconecta do Wi-Fi para poder mudar o modo
  WiFi.disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));

  // 3. Liga o modo promíscuo e registra o callback
  ESP_LOGI(TAG_TA, "Iniciando modo promíscuo...");
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);

  // 4. Sintoniza o rádio no canal correto
  esp_wifi_set_channel(_target_channel, WIFI_SECOND_CHAN_NONE);
  
  // 5. Inicia a tarefa de análise
  xTaskCreatePinnedToCore(snifferTask, "Sniffer Task", 8192, this, 2, &_snifferTaskHandle, 0);
}

void TrafficAnalyzer::stop() {
  if (_snifferTaskHandle != NULL) {
    vTaskDelete(_snifferTaskHandle);
    _snifferTaskHandle = NULL;
  }
  esp_wifi_set_promiscuous(false);
  statsMap.clear();
  ESP_LOGI(TAG_TA, "Modo promíscuo parado.");
}