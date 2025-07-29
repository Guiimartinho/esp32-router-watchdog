#ifndef ANOMALY_DETECTOR_H
#define ANOMALY_DETECTOR_H

#include <cstdint>

class AnomalyDetector {
public:
  AnomalyDetector();
  void setup();
  // A função principal: recebe as métricas e retorna true se for uma anomalia
  bool detect(uint32_t packet_count, uint64_t total_bytes);

private:
  bool _initialized = false;
};

#endif