#include <Arduino.h>
#include "secrets.h"
#include <WiFi.h>
#include "RouterManager.h"
#include "NetworkDiagnostics.h"
#include "NotificationManager.h"
#include "NetworkDiscovery.h"
#include "TrafficAnalyzer.h"
#include "WebServerManager.h"
#include <Adafruit_NeoPixel.h>
#include "esp_log.h"
#include <Preferences.h>
#include "TinyUPnP.h"
#include "AnomalyDetector.h" 

extern "C"
{
#include "lwip/dns.h"
}

static const char *TAG = "MainLogic";

// Estados de alto nível do sistema
enum SystemOverallState
{
  STATE_PROVISIONING,
  STATE_OPERATIONAL
};
SystemOverallState systemState;

// Sub-estados do modo operacional
enum OperationalMode
{
  MODE_MONITOR,
  MODE_SNIFFER
};

// --- Configurações e Instâncias de Módulos ---
const int ROUTER_RELAY_PIN = 18;
#define LED_PIN 48
#define NUM_LEDS 1
#define BOOT_BUTTON_PIN 0
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#define COLOR_RED pixels.Color(255, 0, 0)
#define COLOR_GREEN pixels.Color(0, 255, 0)
#define COLOR_PURPLE pixels.Color(128, 0, 128)

String saved_ssid;
String saved_pass;

RouterManager routerManager(ROUTER_RELAY_PIN);
NetworkDiagnostics networkDiagnostics;
NotificationManager notificationManager;
NetworkDiscovery networkDiscovery;
TrafficAnalyzer trafficAnalyzer;
WebServerManager webServerManager;
Preferences preferences;
TinyUPnP upnp(5000); 
AnomalyDetector anomalyDetector;

// ===================================================================
// --- MUDANÇA 1: NOVA LÓGICA DE CONTROLE DO LED ---
// ===================================================================
void updateLedColor(OperationalMode opMode = MODE_MONITOR, bool isOnline = false)
{
  if (systemState == STATE_PROVISIONING)
  {
    pixels.setPixelColor(0, COLOR_PURPLE); // Roxo somente no provisionamento
  }
  else // STATE_OPERATIONAL
  {
    if (opMode == MODE_SNIFFER)
    {
      pixels.setPixelColor(0, COLOR_RED); // Vermelho somente no modo sniffer
    }
    else // opMode == MODE_MONITOR
    {
      if (isOnline)
      {
        pixels.setPixelColor(0, COLOR_GREEN); // Verde se estiver online no modo monitor
      }
      else
      {
        pixels.setPixelColor(0, 0); // Apagado se estiver offline no modo monitor
      }
    }
  }
  pixels.show();
}

// ===================================================================
// --- TAREFA OPERACIONAL REESTRUTURADA ---
// ===================================================================
void operationalTask(void *pvParameters)
{
  ESP_LOGI(TAG, "Tarefa de Operação iniciada.");
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  OperationalMode currentMode = MODE_MONITOR;
  unsigned long lastModeChange = millis();

  const long monitorDuration = 3 * 60 * 1000; // 3 minutos
  const long snifferDuration = 1 * 60 * 1000;   // 1 minuto

  unsigned long lastInternetCheck = 0;
  unsigned long lastDiscoveryScan = millis();

  const long internetCheckInterval = 60 * 1000;     // 1 minuto
  const long discoveryScanInterval = 4 * 60 * 1000; // 5 minutos

  // --- diciona um timer para a descoberta UPnP ---
  unsigned long lastUpnpDiscovery = 0;
  const long upnpDiscoveryInterval = 10 * 60 * 1000; // A cada 10 minutos

  notificationManager.sendMessage("✅ *Super Monitor* iniciou operação normal.");

  for (;;)
  {
    unsigned long currentTime = millis();
    bool isConnected = (WiFi.status() == WL_CONNECTED);

    // --- LÓGICA DE EXECUÇÃO E TRANSIÇÃO DE MODO ---
    if (currentMode == MODE_MONITOR)
    {
        // --- Bloco de ações do Modo Monitor ---
        if (isConnected)
        {
            // 1. Verifica a internet periodicamente
            if (currentTime - lastInternetCheck >= internetCheckInterval)
            {
                bool internetOK = networkDiagnostics.isInternetConnected();
                updateLedColor(MODE_MONITOR, internetOK); // --- MUDANÇA AQUI ---
                routerManager.updateInternetStatus(internetOK);
                lastInternetCheck = currentTime;
            }

            // 2. Controla o roteador
            routerManager.loop();

            // 3. Roda o scan de descoberta periodicamente
            if (!networkDiscovery.isScanning() && (currentTime - lastDiscoveryScan >= discoveryScanInterval))
            {
                networkDiscovery.beginScan();
                lastDiscoveryScan = currentTime;
            }

            // --- Adiciona a lógica de descoberta UPnP periódica ---
            if (currentTime - lastUpnpDiscovery >= upnpDiscoveryInterval) {
                ESP_LOGI(TAG, "Iniciando descoberta de dispositivos UPnP...");
                
                // Esta é a chamada correta: ela bloqueia e retorna a lista
                ssdpDeviceNode* deviceList = upnp.listSsdpDevices();

                ESP_LOGI(TAG, "------ Dispositivos UPnP Encontrados ------");
                // A biblioteca oferece uma função para imprimir a lista
                upnp.printSsdpDevices(deviceList);
                ESP_LOGI(TAG, "------------------------------------------");
                
                // IMPORTANTE: A biblioteca não libera a memória, temos que fazer isso manualmente
                // para evitar vazamento de memória (memory leak).
                ssdpDeviceNode *curr = deviceList;
                while (curr != NULL) {
                    ssdpDeviceNode *next = curr->next;
                    delete curr->ssdpDevice;
                    delete curr;
                    curr = next;
                }

                lastUpnpDiscovery = currentTime;
            }
        }
        else
        {
            ESP_LOGW(TAG, "Wi-Fi desconectado em modo Monitor. Tentando reconectar...");
            updateLedColor(MODE_MONITOR, false); // --- MUDANÇA AQUI ---
            routerManager.updateInternetStatus(false);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }

        // 4. Verifica se é hora de mudar para o modo Sniffer
        if (currentTime - lastModeChange >= monitorDuration)
        {
            ESP_LOGI(TAG, "MUDANDO PARA MODO SNIFFER.");
            notificationManager.sendMessage("🔬 Entrando em modo de análise de tráfego por 1 minuto...");
            trafficAnalyzer.start();
            currentMode = MODE_SNIFFER;
            lastModeChange = currentTime;
            updateLedColor(MODE_SNIFFER, false); // --- MUDANÇA AQUI ---
        }
    }
    else // currentMode == MODE_SNIFFER
    {
        // --- Bloco de ações do Modo Sniffer ---
        // A tarefa snifferTask está rodando em background.
        
        // Verifica se é hora de voltar ao modo Monitor
        if (currentTime - lastModeChange >= snifferDuration)
        {
            ESP_LOGI(TAG, "MUDANDO PARA MODO MONITOR.");
            trafficAnalyzer.stop();
            // --- ADICIONADO: Lógica de Detecção de Anomalia ---
            ESP_LOGI(TAG, "Executando análise de tráfego com TinyML...");
            bool isAnomaly = anomalyDetector.detect(
                trafficAnalyzer._total_packets_in_window, 
                trafficAnalyzer._total_bytes_in_window
            );
            if (isAnomaly) {
                notificationManager.sendMessage("🚨 *ALERTA:* Anomalia de tráfego de rede detectada!");
            }
            // -----------------------------------------------------------

            notificationManager.sendMessage("📡 Voltando ao modo de monitoramento...");
            
            ESP_LOGI(TAG, "Reconectando ao Wi-Fi...");
            WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
            
            vTaskDelay(pdMS_TO_TICKS(10000)); // Espera para estabilizar
            
            currentMode = MODE_MONITOR;
            lastModeChange = currentTime;
            lastInternetCheck = 0; // Força uma checagem de internet imediata
            updateLedColor(MODE_MONITOR, (WiFi.status() == WL_CONNECTED)); // --- MUDANÇA AQUI ---
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  ESP_LOGI(TAG, "Iniciando o Super-Monitor...");

  pixels.begin();
  pixels.setBrightness(40);
  pixels.clear();
  pixels.show();

  // --- LÓGICA DE DECISÃO DE MODO ---
  bool forceProvisioning = false;
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  delay(50);
  if (digitalRead(BOOT_BUTTON_PIN) == LOW)
  {
    long pressStartTime = millis();
    ESP_LOGI(TAG, "Botão BOOT pressionado, aguardando 10s para reset de fábrica...");
    while (digitalRead(BOOT_BUTTON_PIN) == LOW)
    {
      if (millis() - pressStartTime > 10000)
      {
        ESP_LOGW(TAG, "Reset de fábrica acionado!");
        forceProvisioning = true;
        preferences.begin("s-monitor-cfg", false);
        preferences.clear();
        preferences.end();
        break;
      }
      delay(100);
    }
  }

  preferences.begin("s-monitor-cfg", true);
  String storedSsid = preferences.getString("wifi_ssid", "");
  preferences.end();

  if (forceProvisioning || storedSsid == "")
  {
    systemState = STATE_PROVISIONING;
  }
  else
  {
    systemState = STATE_OPERATIONAL;
  }

  // --- MÁQUINA DE ESTADOS DA INICIALIZAÇÃO ---
  if (systemState == STATE_PROVISIONING)
  {
    ESP_LOGI(TAG, "Sistema em MODO DE PROVISIONAMENTO.");
    updateLedColor(); // --- MUDANÇA AQUI ---
    webServerManager.setup();
    webServerManager.startProvisioningServer();
  }
  else
  { // STATE_OPERATIONAL
    ESP_LOGI(TAG, "Sistema em MODO DE OPERAÇÃO.");

    preferences.begin("s-monitor-cfg", true);
    saved_ssid = preferences.getString("wifi_ssid");
    saved_pass = preferences.getString("wifi_pass");
    String router_ip = preferences.getString("router_ip", ROUTER_IP);
    String router_user = preferences.getString("router_user", ROUTER_USER);
    String router_pass = preferences.getString("router_pass", ROUTER_PASS);
    String tg_token = preferences.getString("tg_token", TELEGRAM_BOT_TOKEN);
    long long tg_chat_id_ll = atoll(preferences.getString("tg_chat_id", "0").c_str());
    if (tg_chat_id_ll == 0)
      tg_chat_id_ll = TELEGRAM_CHAT_ID;
    preferences.end();

    // Inicializa todos os módulos
    routerManager.setup();
    routerManager.setRouterCredentials(router_ip.c_str(), ROUTER_TR064_PORT, router_user.c_str(), router_pass.c_str());
    networkDiagnostics.setup();
    notificationManager.setup(tg_token.c_str(), tg_chat_id_ll);
    routerManager.setNotificationManager(&notificationManager);
    networkDiscovery.setup();
    trafficAnalyzer.setup();
    webServerManager.setup();
    networkDiagnostics.setDiscoveryModule(&networkDiscovery);
    anomalyDetector.setup();

    WiFi.setAutoReconnect(true);
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str()); 

    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED && retryCount < 60)
    {
      delay(500);
      Serial.print(".");
      retryCount++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println();
      ESP_LOGI(TAG, "Wi-Fi Conectado!");

      // Atualiza o LED para verde, pois estamos online no modo monitor
      updateLedColor(MODE_MONITOR, true); //

      IPAddress primaryDNS(8, 8, 8, 8);
      IPAddress secondaryDNS(1, 1, 1, 1);
      
      ip_addr_t primaryDnsAddr;
      ip_addr_t secondaryDnsAddr;

      primaryDnsAddr.type = IPADDR_TYPE_V4;
      secondaryDnsAddr.type = IPADDR_TYPE_V4;

      primaryDnsAddr.u_addr.ip4.addr = static_cast<uint32_t>(primaryDNS);
      secondaryDnsAddr.u_addr.ip4.addr = static_cast<uint32_t>(secondaryDNS);
    }
    else
    {
      Serial.println();
      ESP_LOGE(TAG, "Falha ao conectar com as credenciais salvas.");
      updateLedColor(MODE_MONITOR, false); // --- MUDANÇA AQUI ---
    }

    ESP_LOGI(TAG, "Setup completo. Iniciando tarefa de operação.");
    xTaskCreate(operationalTask, "Operational Task", 16384, NULL, 1, NULL);
  }
}

void loop()
{
  if (systemState == STATE_PROVISIONING)
  {
    webServerManager.loop();
  }
  else
  {
    vTaskDelete(NULL);
  }
}