#include <Arduino.h>
#include <WiFi.h>
#include "RouterManager.h"
#include "NetworkDiagnostics.h"
#include "NotificationManager.h"
#include "NetworkDiscovery.h"
#include <Adafruit_NeoPixel.h>
#include "esp_log.h"
// No topo do main.cpp, junto com os outros includes
extern "C" {
  #include "lwip/dns.h"
}

static const char *TAG = "MainLogic";

// --- Configurações ... (todo o bloco de constantes continua o mesmo) ...
const char* WIFI_SSID = "XXXX";
const char* WIFI_PASS = "XXXX";
const int   ROUTER_RELAY_PIN = 4;
const char* ROUTER_IP        = "192.168.1.1";
const int   ROUTER_TR064_PORT= 49000;
const char* ROUTER_USER      = "admin";
const char* ROUTER_PASS      = "admin";
#define LED_PIN    48
#define NUM_LEDS   1
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_RGB);
#define COLOR_RED pixels.Color(0, 255, 0)
#define COLOR_GREEN   pixels.Color(255, 0, 0)
const char* TELEGRAM_BOT_TOKEN = "XXXX:XXX";
const int64_t TELEGRAM_CHAT_ID = 00000000;

// Instâncias dos módulos
RouterManager routerManager(ROUTER_RELAY_PIN);
NetworkDiagnostics networkDiagnostics;
NotificationManager notificationManager;
NetworkDiscovery networkDiscovery;

void setLedStatus(bool isOnline) {
  if (isOnline) { pixels.setPixelColor(0, COLOR_GREEN); }
  else { pixels.setPixelColor(0, COLOR_RED); }
  pixels.show();
}

// --- TAREFA ÚNICA E UNIFICADA ---
void mainTask(void *pvParameters) {
  ESP_LOGI(TAG, "Tarefa Principal Unificada iniciada.");
  vTaskDelay(5000 / portTICK_PERIOD_MS); 

  // Variáveis de controle de tempo para as ações
  unsigned long lastInternetCheck = 0;
  unsigned long lastDiscoveryScan = 0;

  // Intervalos em milissegundos
  const long internetCheckInterval = 60 * 1000;     // 1 minuto
  const long discoveryScanInterval = 15 * 60 * 1000; // 15 minutos

  // Envia a notificação inicial
  bool initialCheck = networkDiagnostics.isInternetConnected();
  setLedStatus(initialCheck);
  routerManager.updateInternetStatus(initialCheck);
  if (initialCheck) {
    notificationManager.sendMessage("✅ *Super Monitor* iniciado e online!");
  } else {
    notificationManager.sendMessage("⚠️ *Super Monitor* iniciado, mas sem conexão com a internet!");
  }
  
  // Marca o tempo da primeira checagem para não rodar tudo de novo imediatamente
  lastInternetCheck = millis();
  lastDiscoveryScan = millis();

  for (;;) {
    unsigned long currentTime = millis();

    // --- Bloco de Diagnóstico da Internet ---
    if (currentTime - lastInternetCheck >= internetCheckInterval) {
      bool internetOK = networkDiagnostics.isInternetConnected();
      setLedStatus(internetOK);
      routerManager.updateInternetStatus(internetOK);
      lastInternetCheck = currentTime;
    }

    // O loop do RouterManager roda com mais frequência para ser responsivo
    routerManager.loop();

    // --- Bloco de Descoberta de Rede ---
    if (currentTime - lastDiscoveryScan >= discoveryScanInterval) {
      if (WiFi.status() == WL_CONNECTED) {
        networkDiscovery.beginScan();
        ESP_LOGI(TAG, "Aguardando o scan da rede terminar...");
        while (networkDiscovery.isScanning()) {
          vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Scan finalizado! %d dispositivos encontrados.", networkDiscovery.deviceCount);
        // A impressão detalhada dos dispositivos já está no módulo NetworkDiscovery
      }
      lastDiscoveryScan = currentTime;
    }

    // Pequeno delay no loop principal para ceder tempo de CPU
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  ESP_LOGI(TAG, "Iniciando o Super-Monitor...");
  
  pixels.begin();
  pixels.setBrightness(40);
  pixels.clear();
  pixels.show();
  
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  ESP_LOGI(TAG, "Tentando conectar ao Wi-Fi por 30 segundos...");
  int retryCount = 0;
  const int maxRetries = 60; 
  while (WiFi.status() != WL_CONNECTED && retryCount < maxRetries) {
    delay(500);
    Serial.print(".");
    retryCount++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    ESP_LOGI(TAG, "Wi-Fi Conectado!");
    
    ESP_LOGI(TAG, "Configurando DNS primário (Google) e secundário (Cloudflare)...");
    
    // 1. Criamos os objetos IPAddress do Arduino, que é mais seguro e fácil
    IPAddress primaryDNS(8, 8, 8, 8);
    IPAddress secondaryDNS(1, 1, 1, 1);

    // 2. Convertemos para o formato ip_addr_t que a função dns_setserver espera
    ip_addr_t primaryDnsAddr;
    ip_addr_t secondaryDnsAddr;
    primaryDnsAddr.type = IPADDR_TYPE_V4;
    secondaryDnsAddr.type = IPADDR_TYPE_V4;
    primaryDnsAddr.u_addr.ip4.addr = static_cast<uint32_t>(primaryDNS);
    secondaryDnsAddr.u_addr.ip4.addr = static_cast<uint32_t>(secondaryDNS);
    
    // 3. Agora definimos os servidores com os tipos corretos
    dns_setserver(0, &primaryDnsAddr);
    dns_setserver(1, &secondaryDnsAddr);
  } else {
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
  
  ESP_LOGI(TAG, "Setup completo. Iniciando tarefa principal.");
  xTaskCreate(mainTask, "Main Task", 8192, NULL, 1, NULL);
}

void loop() {
  // O loop principal agora está oficialmente aposentado.
  vTaskDelete(NULL); // Deleta a tarefa do loop do Arduino para economizar recursos.
}