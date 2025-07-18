#ifndef ROUTER_MANAGER_H
#define ROUTER_MANAGER_H

#include <Arduino.h>
#include "NotificationManager.h" 
#include "freertos/semphr.h"

// Define os estados possíveis do nosso sistema
enum SystemState {
  NORMAL,
  AWAITING_FIRST_REBOOT,
  AWAITING_SECOND_REBOOT,
  AWAITING_30MIN_REBOOT_1,
  AWAITING_30MIN_REBOOT_2,
  AWAITING_2HOUR_REBOOT
};

class RouterManager {
public:
  RouterManager(const int relayPin);
  void setup();
  void setRouterCredentials(const char* ip, int port, const char* user, const char* pass);
  
  // A função loop() agora conterá a lógica da máquina de estados
  void loop();

  // Função para o Módulo 2 nos informar o status da internet
  void updateInternetStatus(bool isUp);

  // NOVA FUNÇÃO para ligar o notificador ao manager
  void setNotificationManager(NotificationManager* nm);

private:
  // --- Configurações ---
  int _relayPin;
  const char* _routerIp;
  int _routerPort;
  const char* _routerUser;
  const char* _routerPass;

  // --- Variáveis de Estado ---
  SystemState _currentState;
  bool _isInternetUp;
  unsigned long _lastStateChangeTime;

  // --- Handle para o nosso Mutex ---
  SemaphoreHandle_t _stateMutex;

  // --- Métodos Internos ---
  void performIntelligentReboot();
  bool _rebootViaTR064();
  void _rebootViaRelay();

  // NOVO PONTEIRO para guardar a referência ao notificador
  NotificationManager* _notificationManager;
};

#endif