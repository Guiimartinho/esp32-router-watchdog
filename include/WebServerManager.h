#ifndef WEBSERVER_MANAGER_H
#define WEBSERVER_MANAGER_H

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "freertos/timers.h"

class WebServerManager {
public:
  WebServerManager();
  void setup();
  void loop();
  
  // Nova função para checar se o reboot é necessário
  // bool isRebootNeeded();

  void startProvisioningServer();
  void startDashboardServer();
  void stopServer();

private:
  AsyncWebServer _server;
  DNSServer _dnsServer;
  TimerHandle_t _rebootTimer;
  
  // Nova flag para sinalizar o reboot
  // volatile bool _reboot_needed;

  void _handleRoot(AsyncWebServerRequest *request);
  void _handleSaveConfig(AsyncWebServerRequest *request);
  // void _handleGetStatus(AsyncWebServerRequest *request);
  // void _handleReboot(AsyncWebServerRequest *request);
  // void _handleNotFound(AsyncWebServerRequest *request);
};

#endif