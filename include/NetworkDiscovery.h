#ifndef NETWORK_DISCOVERY_H
#define NETWORK_DISCOVERY_H

#include <Arduino.h>
#include "freertos/semphr.h"

#define MAX_DEVICES 50 // Aumentando o limite

struct DiscoveredDevice {
  IPAddress ip;
  String macAddress; // Por enquanto, não conseguimos o MAC com ping, ficará vazio
  bool isOnline;
};

// Estrutura para passar os parâmetros para cada tarefa de ping
struct PingTaskParams {
  int startIp;
  int endIp;
  class NetworkDiscovery* scanner;
};

class NetworkDiscovery {
public:
  NetworkDiscovery();
  void setup();
  void beginScan();
  bool isScanning();

  DiscoveredDevice devices[MAX_DEVICES];
  int deviceCount;

  // Mutex para proteger o acesso à lista de dispositivos por múltiplas tarefas
  SemaphoreHandle_t listMutex; 
  // Contador para saber quantas tarefas de scan ainda estão ativas
  volatile int activeScanTasks; 
};

#endif