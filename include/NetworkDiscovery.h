#ifndef NETWORK_DISCOVERY_H
#define NETWORK_DISCOVERY_H

#include <Arduino.h>
#include "freertos/semphr.h"

#define MAX_DEVICES 50

struct DiscoveredDevice {
  IPAddress ip;
  String macAddress;
  bool isOnline;
};

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
  
  SemaphoreHandle_t listMutex;
  SemaphoreHandle_t pingMutex; // <-- ADICIONADO: Mutex para a biblioteca de Ping
  
  volatile int activeScanTasks;
};

#endif