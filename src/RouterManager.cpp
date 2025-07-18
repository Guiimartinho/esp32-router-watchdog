#include "RouterManager.h"
#include "tr064.h"
#include "esp_log.h"
#include "NotificationManager.h"

// TAG para os logs deste módulo
static const char *TAG_RM = "RouterManager";

// Definição dos intervalos de tempo em milissegundos
const unsigned long TWO_MINUTES = 2 * 60 * 1000;
const unsigned long THIRTY_MINUTES = 30 * 60 * 1000;
const unsigned long TWO_HOURS = 2 * 60 * 60 * 1000;

// Construtor
RouterManager::RouterManager(const int relayPin) {
    _currentState = NORMAL;
    _isInternetUp = true;
    _lastStateChangeTime = 0;
    _relayPin = relayPin;
    _stateMutex = NULL;
    _notificationManager = nullptr;
}

// Função de Setup
void RouterManager::setup() {
    pinMode(_relayPin, OUTPUT);
    digitalWrite(_relayPin, HIGH);
    _stateMutex = xSemaphoreCreateMutex();
    if (_stateMutex != NULL) {
        ESP_LOGI(TAG_RM, "Módulo inicializado com sucesso (Mutex criado).");
    } else {
        ESP_LOGE(TAG_RM, "Falha ao criar o Mutex!");
    }
}

void RouterManager::setRouterCredentials(const char *ip, int port, const char *user, const char *pass) {
    _routerIp = ip; _routerPort = port; _routerUser = user; _routerPass = pass;
}

// Recebe a referência do NotificationManager vinda do main.cpp
void RouterManager::setNotificationManager(NotificationManager* nm) {
  _notificationManager = nm;
}

// Atualiza o status da internet (chamada pela logicTask)
void RouterManager::updateInternetStatus(bool isUp) {
    if (xSemaphoreTake(_stateMutex, (TickType_t)10) == pdTRUE) {
        // Lógica de transição de estado: só acontece quando o status MUDA
        if (isUp != _isInternetUp) {
            _isInternetUp = isUp;
            if (_isInternetUp) {
                // Se a internet voltou
                ESP_LOGI(TAG_RM, "[State Machine] Internet recuperada! Voltando ao estado NORMAL.");
                if (_notificationManager) {
                    _notificationManager->sendMessage("✅ *Internet Recuperada!* Sistema voltando ao estado normal.");
                }
                _currentState = NORMAL;
            } else {
                // Se a internet caiu
                ESP_LOGW(TAG_RM, "[State Machine] Internet caiu! Iniciando contagem para o primeiro reboot.");
                if (_notificationManager) {
                    _notificationManager->sendMessage("‼️ *ALERTA: Internet Caiu!*\nIniciando protocolo de recuperação...");
                }
                _currentState = AWAITING_FIRST_REBOOT;
                _lastStateChangeTime = millis();
            }
        }
        xSemaphoreGive(_stateMutex);
    }
}

// Loop principal da máquina de estados
void RouterManager::loop() {
    // Pega a trava do Mutex para trabalhar com segurança
    if (xSemaphoreTake(_stateMutex, (TickType_t)10) == pdTRUE) {
        
        // Se o estado é NORMAL, não há nada a fazer. Libera a trava e sai.
        if (_currentState == NORMAL) {
            xSemaphoreGive(_stateMutex);
            return;
        }

        unsigned long currentTime = millis();
        bool shouldReboot = false;
        char notificationMessage[150]; // Buffer para a mensagem do Telegram

        // A lógica do switch agora apenas verifica se é hora de agir
        switch (_currentState) {
            case AWAITING_FIRST_REBOOT:
                if (currentTime - _lastStateChangeTime >= TWO_MINUTES) {
                    sprintf(notificationMessage, "⚠️ *Internet Offline* por 2 min.\nIniciando tentativa de reboot #1...");
                    shouldReboot = true;
                }
                break;

            case AWAITING_SECOND_REBOOT:
                if (currentTime - _lastStateChangeTime >= TWO_MINUTES) {
                    sprintf(notificationMessage, "🚨 *Falha Persiste* (+2 min).\nIniciando tentativa de reboot #2...");
                    shouldReboot = true;
                }
                break;

            case AWAITING_30MIN_REBOOT_1:
                if (currentTime - _lastStateChangeTime >= THIRTY_MINUTES) {
                    sprintf(notificationMessage, "🚨 *Falha Persiste* (+30 min).\nIniciando tentativa de reboot #3...");
                    shouldReboot = true;
                }
                break;

            case AWAITING_30MIN_REBOOT_2:
                if (currentTime - _lastStateChangeTime >= THIRTY_MINUTES) {
                    sprintf(notificationMessage, "‼️ *FALHA CRÍTICA* (+30 min).\nIniciando tentativa de reboot #4...");
                    shouldReboot = true;
                }
                break;

            case AWAITING_2HOUR_REBOOT:
                if (currentTime - _lastStateChangeTime >= TWO_HOURS) {
                    sprintf(notificationMessage, "‼️ *FALHA CRÍTICA* - Sistema em modo de recuperação de longo prazo (reboot a cada 2h).");
                    shouldReboot = true;
                }
                break;
            
            case NORMAL:
                break;
        }

        // Se qualquer um dos 'cases' acima decidiu que é hora de rebootar...
        if (shouldReboot) {
            ESP_LOGI(TAG_RM, "[State Machine] %s", notificationMessage);
            if (_notificationManager) {
                _notificationManager->sendMessage(notificationMessage);
            }
            
            performIntelligentReboot();
            
            // Avança para o próximo estado
            if(_currentState == AWAITING_FIRST_REBOOT) _currentState = AWAITING_SECOND_REBOOT;
            else if(_currentState == AWAITING_SECOND_REBOOT) _currentState = AWAITING_30MIN_REBOOT_1;
            else if(_currentState == AWAITING_30MIN_REBOOT_1) _currentState = AWAITING_30MIN_REBOOT_2;
            else if(_currentState == AWAITING_30MIN_REBOOT_2) _currentState = AWAITING_2HOUR_REBOOT;
            
            _lastStateChangeTime = currentTime;
        }
        
        // Libera a trava do Mutex em todos os casos antes de sair da função
        xSemaphoreGive(_stateMutex);
    }
}

// Função que executa a tentativa de reboot
void RouterManager::performIntelligentReboot() {
    if (!_rebootViaTR064()) {
        _rebootViaRelay();
    }
}

// Função para tentar o reboot via TR-064
bool RouterManager::_rebootViaTR064() {
    const char *service = "urn:dslforum-org:service:DeviceConfig:1";
    const char *actionName = "ForceTermination";
    TR064 tr064_client(_routerPort, String(_routerIp), String(_routerUser), String(_routerPass));
    bool success = tr064_client.action(String(service), String(actionName));
    if (success) {
        ESP_LOGI(TAG_RM, "SUCESSO: Comando de reboot enviado via TR-064.");
        return true;
    } else {
        ESP_LOGW(TAG_RM, "FALHA: Roteador não respondeu ou comando TR-064 não suportado.");
        return false;
    }
}

// Função para fazer o reboot físico via relé
void RouterManager::_rebootViaRelay() {
    ESP_LOGI(TAG_RM, "Acionando reboot físico via relé.");
    digitalWrite(_relayPin, LOW);
    delay(15000);
    digitalWrite(_relayPin, HIGH);
    ESP_LOGI(TAG_RM, "Reboot físico via relé concluído.");
}