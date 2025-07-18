#ifndef TRAFFIC_ANALYZER_H
#define TRAFFIC_ANALYZER_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h" // <-- Adicionado para TaskHandle_t

struct CapturedPacketInfo {
  uint8_t mac_source[6];
  int length;
};

// Declaração da tarefa para que o .cpp a conheça
void snifferTask(void* pvParameters);

class TrafficAnalyzer {
public:
  TrafficAnalyzer();
  void setup();
  void start(); // Inicia o modo promiscuo E a tarefa
  void stop();  // Para o modo promiscuo E a tarefa

private:
  QueueHandle_t _packetQueue;
  TaskHandle_t _snifferTaskHandle; // <-- Handle para controlar nossa tarefa

  // O callback continua o mesmo
  static void snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type);

  // Permite que a snifferTask acesse os membros privados desta classe
  friend void snifferTask(void* pvParameters); 
};

#endif