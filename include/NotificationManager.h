#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H

#include <WiFiClientSecure.h>
#include "AsyncTelegram2.h"

class NotificationManager {
public:
  NotificationManager();
  void setup(const char* token, const int64_t mainChatId);
  void sendMessage(const char* message);

private:
  WiFiClientSecure _secure_client;
  // O objeto _bot agora é um ponteiro para podermos controlar sua criação
  AsyncTelegram2* _bot; 
  int64_t _mainChatId;
};

#endif