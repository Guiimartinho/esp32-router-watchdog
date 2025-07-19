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

// Sub-estados do modo operacional, que serão controlados pela tarefa principal
enum OperationalMode
{
  MODE_MONITOR,
  MODE_SNIFFER
};

// --- Configurações e Instâncias de Módulos ---
const int ROUTER_RELAY_PIN = 4;
#define LED_PIN 48
#define NUM_LEDS 1
#define BOOT_BUTTON_PIN 0
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ800);
#define COLOR_GREEN pixels.Color(0, 255, 0)
#define COLOR_RED pixels.Color(255, 0, 0)
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

void setLedStatus(bool isOnline)
{
  if (systemState == STATE_OPERATIONAL)
  {
    if (isOnline)
    {
      pixels.setPixelColor(0, COLOR_GREEN);
    }
    else
    {
      pixels.setPixelColor(0, COLOR_RED);
    }
  }
  else
  {
    pixels.setPixelColor(0, COLOR_PURPLE);
  }
  pixels.show();
}

// --- TAREFA ÚNICA E UNIFICADA COM MÁQUINA DE ESTADOS DE MODO ---
void operationalTask(void *pvParameters)
{
  ESP_LOGI(TAG, "Tarefa de Operação iniciada.");
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  OperationalMode currentMode = MODE_MONITOR;
  unsigned long lastModeChange = millis();

  const long monitorDuration = 14 * 60 * 1000; // 14 minutos
  const long snifferDuration = 1 * 60 * 1000;  // 1 minuto

  unsigned long lastInternetCheck = 0;
  unsigned long lastDiscoveryScan = 0;
  const long internetCheckInterval = 60 * 1000;
  const long discoveryScanInterval = 5 * 60 * 1000;

  notificationManager.sendMessage("✅ *Super Monitor* iniciou operação normal.");

  for (;;)
  {
    unsigned long currentTime = millis();

    // --- LÓGICA DE ALTERNÂNCIA DE MODO ---
    if (currentMode == MODE_MONITOR && (currentTime - lastModeChange >= monitorDuration))
    {
      ESP_LOGI(TAG, "MUDANDO PARA MODO SNIFFER.");
      notificationManager.sendMessage("🔬 Entrando em modo de análise de tráfego por 1 minuto...");
      trafficAnalyzer.start(); // Esta função liga o modo promíscuo e cria a snifferTask
      currentMode = MODE_SNIFFER;
      lastModeChange = currentTime;
    }
    else if (currentMode == MODE_SNIFFER && (currentTime - lastModeChange >= snifferDuration))
    {
      ESP_LOGI(TAG, "MUDANDO PARA MODO MONITOR.");
      trafficAnalyzer.stop(); // Esta função desliga o modo promíscuo e deleta a snifferTask
      notificationManager.sendMessage("📡 Voltando ao modo de monitoramento...");
      ESP_LOGI(TAG, "Reconectando ao Wi-Fi...");

      WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
      currentMode = MODE_MONITOR;
      lastModeChange = currentTime;
    }

    // --- LÓGICA DE EXECUÇÃO BASEADA NO MODO ATUAL ---
    if (currentMode == MODE_MONITOR)
    {
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
        ESP_LOGW(TAG, "Wi-Fi desconectado em modo Monitor.");
        setLedStatus(false);
        routerManager.updateInternetStatus(false);
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

  // --- LÓGICA DE DECISÃO DE MODO (CORRIGIDA) ---
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
  // --- FIM DA LÓGICA DE DECISÃO ---

  // --- MÁQUINA DE ESTADOS DA INICIALIZAÇÃO ---
  if (systemState == STATE_PROVISIONING)
  {
    ESP_LOGI(TAG, "Sistema em MODO DE PROVISIONAMENTO.");
    pixels.setPixelColor(0, COLOR_PURPLE);
    pixels.show();
    webServerManager.setup();
    webServerManager.startProvisioningServer();
  }
  else
  { // STATE_OPERATIONAL
    ESP_LOGI(TAG, "Sistema em MODO DE OPERAÇÃO.");

    preferences.begin("s-monitor-cfg", true);
    String ssid = preferences.getString("wifi_ssid");
    String pass = preferences.getString("wifi_pass");
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

    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());

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
      ESP_LOGE(TAG, "Falha ao conectar com as credenciais salvas.");
    }

    ESP_LOGI(TAG, "Setup completo. Iniciando tarefa de operação.");
    xTaskCreate(operationalTask, "Operational Task", 10000, NULL, 1, NULL);
  }
}

void loop()
{
  if (systemState == STATE_PROVISIONING) {
    // No modo de configuração, o loop apenas garante que o servidor DNS
    // do captive portal continue respondendo às requisições.
    webServerManager.loop();
    // O reboot agora é acionado por um temporizador interno do WebServerManager,
    // então não precisamos mais checar a flag aqui.
  } else {
    // Em modo operacional, a tarefa principal (operationalTask) cuida de tudo.
    // Deletamos a tarefa do loop do Arduino para economizar recursos.
    vTaskDelete(NULL); 
  }
}