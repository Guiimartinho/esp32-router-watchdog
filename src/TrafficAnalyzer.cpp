#include "TrafficAnalyzer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <map>
#include <WiFi.h>

static const char* TAG_TA = "TrafficAnalyzer";
static QueueHandle_t packetQueue_s = NULL;

// ===================================================================
// --- NOVA FUNÇÃO DE DEPURAÇÃO ---
// Esta função imprime o conteúdo de um pacote em formato hexadecimal.
// ===================================================================
void print_packet_hex_dump(uint8_t* data, int len) {
    // Limita a quantidade de bytes impressos para não inundar o log
    int len_to_print = (len < 96) ? len : 96; 
    
    for (int i = 0; i < len_to_print; i++) {
        // Imprime o byte em formato hexadecimal com 2 dígitos (ex: 0F, A4)
        printf("%02X ", data[i]);
        // Quebra a linha a cada 16 bytes para melhor visualização
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n"); // Linha extra no final
}

// O callback permanece o mesmo, apenas captura e envia para a fila
void TrafficAnalyzer::snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  // Ainda filtramos por pacotes de dados, pois é o que nos interessa
  if (type != WIFI_PKT_DATA) return;
  
  wifi_promiscuous_pkt_t* packet = (wifi_promiscuous_pkt_t*)buf;
  wifi_pkt_rx_ctrl_t& ctrl = (wifi_pkt_rx_ctrl_t&)packet->rx_ctrl;
  CapturedPacketInfo info;
  info.length = ctrl.sig_len;
  int len_to_copy = (info.length < sizeof(info.payload)) ? info.length : sizeof(info.payload);
  memcpy(info.payload, packet->payload, len_to_copy);
  xQueueSendToBack(packetQueue_s, &info, (TickType_t)0);
}

// ===================================================================
// --- TAREFA MODIFICADA PARA DEPURAÇÃO ---
// Agora ela apenas imprime o "dump" hexadecimal de cada pacote recebido.
// ===================================================================
void snifferTask(void* pvParameters) {
  ESP_LOGI(TAG_TA, "Tarefa de Análise de Tráfego iniciada. (MODO DE DEPURAÇÃO HEX DUMP)");
  TrafficAnalyzer* analyzer = (TrafficAnalyzer*)pvParameters;

  while (!analyzer->_stopSniffer) {
    CapturedPacketInfo receivedPacket;
    if (xQueueReceive(analyzer->_packetQueue, &receivedPacket, pdMS_TO_TICKS(1000))) {
      // Imprime um cabeçalho para cada pacote
      ESP_LOGI(TAG_TA, "--- Pacote Recebido (len: %d) ---", receivedPacket.length);
      
      // Chama a função para imprimir o conteúdo hexadecimal
      print_packet_hex_dump(receivedPacket.payload, receivedPacket.length);
    }
    // A lógica de estatísticas e decodificação DNS foi removida temporariamente para limpar o log.
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  ESP_LOGI(TAG_TA, "Tarefa de Análise de Tráfego encerrando graciosamente.");
  analyzer->_snifferTaskHandle = NULL;
  vTaskDelete(NULL);
}

// O construtor, setup, start e stop permanecem os mesmos da versão correta anterior
TrafficAnalyzer::TrafficAnalyzer() {
  _packetQueue = NULL;
  _snifferTaskHandle = NULL;
  _stopSniffer = false;
}

void TrafficAnalyzer::setup() {
  _packetQueue = xQueueCreate(100, sizeof(CapturedPacketInfo));
  packetQueue_s = _packetQueue;
  ESP_LOGI(TAG_TA, "Módulo de Análise de Tráfego inicializado.");
}

void TrafficAnalyzer::start() {
  if (WiFi.status() != WL_CONNECTED) {
    ESP_LOGE(TAG_TA, "Nao e possivel iniciar o modo promiscuo. Wi-Fi desconectado.");
    return;
  }
  if (_snifferTaskHandle != NULL) return;
  _stopSniffer = false;

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

void TrafficAnalyzer::stop() {
  if (_snifferTaskHandle != NULL) {
    _stopSniffer = true;
    vTaskDelay(pdMS_TO_TICKS(1100)); 
  }
  esp_wifi_set_promiscuous(false);
  // statsMap foi removido temporariamente da lógica de depuração
  ESP_LOGI(TAG_TA, "Modo promíscuo parado.");
}