#ifndef TRAFFIC_ANALYZER_H
#define TRAFFIC_ANALYZER_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// MUDANÇA: A struct agora carrega o pacote bruto para análise na tarefa
struct CapturedPacketInfo {
  uint8_t payload[1500]; // Buffer para guardar o pacote
  int length;
};

void snifferTask(void *pvParameters);

class TrafficAnalyzer {
public:
  TrafficAnalyzer();
  void setup();
  void start();
  void stop();

  uint32_t _total_packets_in_window;
  uint64_t _total_bytes_in_window;

private:
  QueueHandle_t _packetQueue;
  TaskHandle_t _snifferTaskHandle;
  volatile bool _stopSniffer;

  uint8_t _target_bssid[6];
  uint8_t _target_channel;

  static void snifferCallback(void *buf, wifi_promiscuous_pkt_type_t type);
  friend void snifferTask(void *pvParameters);
};

#endif