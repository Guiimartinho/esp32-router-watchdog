#include "WebServerManager.h"
#include "secrets.h"
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "esp_log.h"
#include <ArduinoJson.h>
#include "RouterManager.h"
#include "NetworkDiagnostics.h"
#include "NetworkDiscovery.h"
#include "TrafficAnalyzer.h"

extern RouterManager routerManager;
extern NetworkDiagnostics networkDiagnostics;
extern NetworkDiscovery networkDiscovery;
extern TrafficAnalyzer trafficAnalyzer;

static const char *TAG_WS = "WebServer";
const byte DNS_PORT = 53;
const char *ap_ssid = "Super-Monitor-Setup";

void rebootCallback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG_WS, "Temporizador de reboot acionado. Reiniciando agora...");
    ESP.restart();
}

WebServerManager::WebServerManager() : _server(80)
{
    _rebootTimer = NULL;
}

void WebServerManager::setup()
{
    ESP_LOGI(TAG_WS, "Módulo WebServer inicializado.");
    _rebootTimer = xTimerCreate("rebootTimer", pdMS_TO_TICKS(3000), pdFALSE, (void *)0, rebootCallback);
}

void WebServerManager::loop()
{
    _dnsServer.processNextRequest();
}

// bool WebServerManager::isRebootNeeded()
// {
//     return _reboot_needed;
// }

void WebServerManager::startProvisioningServer()
{
    ESP_LOGI(TAG_WS, "Iniciando servidor em Modo de Provisionamento (AP)...");
    WiFi.softAP(ap_ssid);
    ESP_LOGI(TAG_WS, "Rede Wi-Fi '%s' criada. IP: %s", ap_ssid, WiFi.softAPIP().toString().c_str());
    _dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    _server.on("/", HTTP_GET, std::bind(&WebServerManager::_handleRoot, this, std::placeholders::_1));
    _server.on("/save", HTTP_POST, std::bind(&WebServerManager::_handleSaveConfig, this, std::placeholders::_1));
    _server.onNotFound(std::bind(&WebServerManager::_handleRoot, this, std::placeholders::_1));
    _server.begin();
    ESP_LOGI(TAG_WS, "Servidor de configuração online.");
}

void WebServerManager::_handleRoot(AsyncWebServerRequest *request)
{
    String html = R"rawliteral(
  <!DOCTYPE HTML><html><head><title>Super Monitor Setup</title><meta name="viewport" content="width=device-width, initial-scale=1">
  <style>body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#f4f4f4;margin:0;padding:20px;}.container{max-width:500px;margin:auto;background:#fff;padding:20px;box-shadow:0 0 10px rgba(0,0,0,0.1);border-radius:8px;}h2,h3{color:#333;}input[type=text],input[type=password]{width:100%;padding:12px;margin:8px 0;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;}input[type=submit]{background-color:#4CAF50;color:white;padding:14px 20px;margin:8px 0;border:none;cursor:pointer;width:100%;border-radius:4px;font-size:16px;}input[type=submit]:hover{background-color:#45a049;}</style>
  </head><body><div class="container"><h2>Configuracao do Super Monitor</h2>
  <form action="/save" method="POST"><h3>Rede Wi-Fi (Obrigatorio)</h3>
  <input type="text" name="wifi_ssid" placeholder="Nome da Rede (SSID)" required>
  <input type="password" name="wifi_pass" placeholder="Senha da Rede" required>
  <h3>TR-064 (Opcional)</h3>
  <input type="text" name="router_ip" placeholder="IP do Roteador (ex: 192.168.1.1)">
  <input type="text" name="router_user" placeholder="Usuario do Roteador">
  <input type="password" name="router_pass" placeholder="Senha do Roteador">
  <h3>Telegram (Opcional)</h3>
  <input type="text" name="tg_token" placeholder="Token do Bot">
  <input type="text" name="tg_chat_id" placeholder="Seu Chat ID numerico">
  <input type="submit" value="Salvar e Reiniciar"></form></div></body></html>
  )rawliteral";
    request->send(200, "text/html", html);
}

void WebServerManager::_handleSaveConfig(AsyncWebServerRequest *request)
{
    Preferences preferences;
    preferences.begin("s-monitor-cfg", false);

    if (request->hasParam("wifi_ssid", true))
        preferences.putString("wifi_ssid", request->getParam("wifi_ssid", true)->value());
    if (request->hasParam("wifi_pass", true))
        preferences.putString("wifi_pass", request->getParam("wifi_pass", true)->value());
    if (request->hasParam("router_ip", true))
        preferences.putString("router_ip", request->getParam("router_ip", true)->value());
    if (request->hasParam("router_user", true))
        preferences.putString("router_user", request->getParam("router_user", true)->value());
    if (request->hasParam("router_pass", true))
        preferences.putString("router_pass", request->getParam("router_pass", true)->value());
    if (request->hasParam("tg_token", true))
        preferences.putString("tg_token", request->getParam("tg_token", true)->value());
    if (request->hasParam("tg_chat_id", true))
        preferences.putString("tg_chat_id", request->getParam("tg_chat_id", true)->value());
    preferences.end();

    String html = "<html><body><h1>Configuracoes salvas!</h1><h2>O dispositivo ira reiniciar...</h2></body></html>";
    request->send(200, "text/html", html);

    ESP_LOGI(TAG_WS, "Configuracoes salvas. Sinalizando para o loop principal reiniciar.");
    if (_rebootTimer != NULL)
    {
        xTimerStart(_rebootTimer, 0);
    }
    else
    {
        ESP_LOGE(TAG_WS, "ERRO FATAL: Temporizador de reboot não foi criado no setup!");
    }
}

void WebServerManager::startDashboardServer()
{
    ESP_LOGI(TAG_WS, "Iniciando servidor de Dashboard...");

    _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    String html = R"rawliteral(
    <!DOCTYPE HTML><html><head><title>Super Monitor Dashboard</title><meta name="viewport" content="width=device-width, initial-scale=1">
    <style>body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#121212;color:#e0e0e0;margin:0;padding:20px;}.grid-container{display:grid;grid-template-columns:repeat(auto-fit, minmax(300px, 1fr));gap:20px;}.card{background:#1e1e1e;padding:20px;border-radius:8px;box-shadow:0 4px 8px rgba(0,0,0,0.3);}h1,h2{color:#fff;border-bottom:1px solid #444;padding-bottom:10px;}#status.online{color:#4CAF50;font-weight:bold;}#status.offline{color:#f44336;font-weight:bold;}pre{background:#282828;padding:10px;border-radius:4px;white-space:pre-wrap;word-wrap:break-word;max-height:300px;overflow-y:auto;}button{background-color:#f44336;color:white;padding:14px 20px;border:none;cursor:pointer;width:100%;border-radius:4px;font-size:16px;margin-top:10px;}button:hover{background-color:#da190b;}</style>
    </head><body><div class="grid-container"><div class="card"><h1>Super Monitor</h1><h2>Status da Internet</h2>
    <p id="status">Carregando...</p><button onclick="forceReboot()">Forcar Reboot do Roteador</button></div>
    <div class="card"><h2>Dispositivos na Rede</h2><pre id="devices">Carregando...</pre></div></div>
    <script>
      function updateData(){fetch('/status_json').then(response=>response.json()).then(data=>{const statusEl=document.getElementById('status');statusEl.innerText=data.isOnline?'ONLINE':'OFFLINE';statusEl.className=data.isOnline?'online':'offline';let deviceText='Total: '+data.deviceCount+'\\n\\n';data.devices.forEach(device=>{deviceText+=device.ip+'\\n';});document.getElementById('devices').innerText=deviceText;});}
      function forceReboot(){if(confirm('Tem certeza que deseja forcar o reboot do roteador?')){fetch('/reboot').then(response=>response.text()).then(text=>alert(text));}}
      setInterval(updateData,5000);window.onload=updateData;
    </script></body></html>
    )rawliteral";
    request->send(200, "text/html", html); });

    _server.on("/status_json", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    JsonDocument json;
    json["isOnline"] = networkDiagnostics.isInternetConnected();
    json["deviceCount"] = networkDiscovery.deviceCount;
    JsonArray devices = json["devices"].to<JsonArray>();
    for (int i = 0; i < networkDiscovery.deviceCount; i++) {
      JsonObject device = devices.add<JsonObject>();
      device["ip"] = networkDiscovery.devices[i].ip.toString();
    }
    String response;
    serializeJson(json, response);
    request->send(200, "application/json", response); });

    _server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    routerManager.performIntelligentReboot();
    request->send(200, "text/plain", "Comando de reboot enviado."); });

    _server.begin();
    ESP_LOGI(TAG_WS, "Servidor de Dashboard online. Acesse pelo IP: http://%s", WiFi.localIP().toString().c_str());
}

void WebServerManager::stopServer()
{
    _server.end();
    _dnsServer.stop();
    ESP_LOGI(TAG_WS, "Servidor web parado.");
}