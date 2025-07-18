#include "NotificationManager.h"
#include "esp_log.h"

static const char* TAG_NM = "NotificationManager";

NotificationManager::NotificationManager() {
  _bot = nullptr;
  _mainChatId = 0;
}

void NotificationManager::setup(const char* token, const int64_t mainChatId) {
  _mainChatId = mainChatId;
  ESP_LOGI(TAG_NM, "Inicializando o bot do Telegram para o chat ID: %lld", _mainChatId);

  _secure_client.setInsecure();
  _bot = new AsyncTelegram2(_secure_client);
  
  _bot->setTelegramToken(token);

  if (_bot->begin()) {
    ESP_LOGI(TAG_NM, "Bot do Telegram inicializado com sucesso.");
  } else {
    ESP_LOGE(TAG_NM, "Falha ao inicializar o bot do Telegram.");
  }
}

void NotificationManager::sendMessage(const char* message) {
  if (_bot == nullptr || _mainChatId == 0) {
    ESP_LOGE(TAG_NM, "Bot não inicializado ou Chat ID não definido.");
    return;
  }

  ESP_LOGI(TAG_NM, "Enviando mensagem para o chat %lld: '%s'", _mainChatId, message);
  
  // --- VERSÃO FINAL E CORRETA DA LÓGICA ---
  
  // 1. Criamos a struct da mensagem.
  TBMessage msg;

  // 2. Preenchemos a struct com os nomes de campo corretos ('chatId' e 'text').
  msg.chatId = _mainChatId;
  msg.text = message;
  
  // 3. Chamamos a versão de sendMessage() que aceita a struct e a string do parse_mode.
  // Esta é a assinatura correta da função que o compilador aceitará.
  _bot->sendMessage(msg, "Markdown");
}