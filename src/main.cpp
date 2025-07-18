#include <Arduino.h>
#include "secrets.h"
#include <WiFi.h>
#include "RouterManager.h"
#include "NetworkDiagnostics.h"
#include "NotificationManager.h"
#include "NetworkDiscovery.h"
#include "TrafficAnalyzer.h"
#include <Adafruit_NeoPixel.h>
#include "esp_log.h"
extern "C"
{
#include "lwip/dns.h"
}

static const char *TAG = "MainLogic";

// Define os modos de operação do sistema
enum SystemMode
{
  MODE_MONITOR,
  MODE_SNIFFER
};

// --- Configurações do Projeto ---
const int ROUTER_RELAY_PIN = 4;
#define LED_PIN 48
#define NUM_LEDS 1
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ800);
#define COLOR_GREEN pixels.Color(0, 255, 0)
#define COLOR_RED pixels.Color(255, 0, 0)

// Instâncias dos módulos
RouterManager routerManager(ROUTER_RELAY_PIN);
NetworkDiagnostics networkDiagnostics;
NotificationManager notificationManager;
NetworkDiscovery networkDiscovery;
TrafficAnalyzer trafficAnalyzer;

void setLedStatus(bool isOnline)
{
  if (isOnline)
  {
    pixels.setPixelColor(0, COLOR_GREEN);
  }
  else
  {
    pixels.setPixelColor(0, COLOR_RED);
  }
  pixels.show();
}

// --- TAREFA ÚNICA E UNIFICADA COM MÁQUINA DE ESTADOS DE MODO ---
void mainTask(void *pvParameters)
{
  ESP_LOGI(TAG, "Tarefa Principal Unificada iniciada.");
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  SystemMode currentMode = MODE_MONITOR;
  unsigned long lastModeChange = millis();

  // Intervalos de tempo para os modos
  const long monitorDuration = 1 * 60 * 2000; // 14 minutos
  const long snifferDuration = 1 * 60 * 1000;  // 1 minuto

  // Variáveis de controle para as ações DENTRO do modo monitor
  unsigned long lastInternetCheck = 0;
  unsigned long lastDiscoveryScan = 0;
  const long internetCheckInterval = 60 * 1000;
  const long discoveryScanInterval = 5 * 60 * 1000;

  for (;;)
  {
    unsigned long currentTime = millis();

    switch (currentMode)
    {
    case MODE_MONITOR:
      // Lógica de monitoramento (internet, router, discovery)
      if (WiFi.status() == WL_CONNECTED)
      {
        // Bloco de Diagnóstico da Internet
        if (currentTime - lastInternetCheck >= internetCheckInterval)
        {
          bool internetOK = networkDiagnostics.isInternetConnected();
          setLedStatus(internetOK);
          routerManager.updateInternetStatus(internetOK);
          lastInternetCheck = currentTime;
        }
        routerManager.loop();

        // Bloco de Descoberta de Rede
        if (currentTime - lastDiscoveryScan >= discoveryScanInterval)
        {
          networkDiscovery.beginScan();
          while (networkDiscovery.isScanning())
          {
            vTaskDelay(pdMS_TO_TICKS(500));
          }
          ESP_LOGI(TAG, "Scan de rede finalizado, %d dispositivos encontrados.", networkDiscovery.deviceCount);
          lastDiscoveryScan = currentTime;
        }
      }
      else
      {
        ESP_LOGW(TAG, "Wi-Fi desconectado em modo Monitor. Tentando reconectar...");
        setLedStatus(false);                       // LED vermelho se Wi-Fi cair
        routerManager.updateInternetStatus(false); // Avisa o manager que tudo caiu
        WiFi.reconnect();
        vTaskDelay(pdMS_TO_TICKS(5000));
      }

      // Verifica se é hora de mudar para o modo Sniffer
      if (currentTime - lastModeChange >= monitorDuration)
      {
        ESP_LOGI(TAG, "Tempo de monitoramento esgotado. Mudando para MODO SNIFFER.");
        notificationManager.sendMessage("🔬 Entrando em modo de análise de tráfego por 1 minuto...");
        WiFi.disconnect();               // Desconecta do Wi-Fi para o modo promiscuo funcionar
        vTaskDelay(pdMS_TO_TICKS(1000)); // Pequena pausa para a desconexão
        trafficAnalyzer.start();
        lastModeChange = currentTime;
        currentMode = MODE_SNIFFER;
      }
      break;

    case MODE_SNIFFER:
      // Neste modo, a tarefa de sniffer está ativa. A mainTask apenas espera.
      if (currentTime - lastModeChange >= snifferDuration)
      {
        ESP_LOGI(TAG, "Tempo de sniffer esgotado. Mudando para MODO MONITOR.");
        trafficAnalyzer.stop();
        notificationManager.sendMessage("📡 Voltando ao modo de monitoramento de internet...");
        ESP_LOGI(TAG, "Reconectando ao Wi-Fi...");
        WiFi.begin(WIFI_SSID, WIFI_PASS); // Usa begin() para uma reconexão limpa
        lastModeChange = currentTime;
        currentMode = MODE_MONITOR;
      }
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Loop principal roda a cada 1 segundo
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

  // O AutoReconnect agora é gerenciado pela nossa lógica
  WiFi.setAutoReconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  ESP_LOGI(TAG, "Tentando conectar ao Wi-Fi por 30 segundos...");
  int retryCount = 0;
  const int maxRetries = 60;
  while (WiFi.status() != WL_CONNECTED && retryCount < maxRetries)
  {
    delay(500);
    Serial.print(".");
    retryCount++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println();
    ESP_LOGI(TAG, "Wi-Fi Conectado!");

    ESP_LOGI(TAG, "Configurando DNS...");
    IPAddress primaryDNS(8, 8, 8, 8);
    IPAddress secondaryDNS(1, 1, 1, 1);
    
    // 2. Convertemos para o formato ip_addr_t que a função dns_setserver espera
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
    ESP_LOGE(TAG, "Falha ao conectar ao Wi-Fi após 30 segundos.");
  }

  ESP_LOGI(TAG, "Inicializando módulos...");
  routerManager.setup();
  routerManager.setRouterCredentials(ROUTER_IP, ROUTER_TR064_PORT, ROUTER_USER, ROUTER_PASS);
  networkDiagnostics.setup();
  notificationManager.setup(TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID);
  routerManager.setNotificationManager(&notificationManager);
  networkDiscovery.setup();
  trafficAnalyzer.setup();

  ESP_LOGI(TAG, "Setup completo. Iniciando tarefa principal.");
  xTaskCreate(mainTask, "Main Task", 8192, NULL, 1, NULL);
}

void loop()
{
  vTaskDelete(NULL);
}