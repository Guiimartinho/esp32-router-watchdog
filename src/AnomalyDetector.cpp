#include "AnomalyDetector.h"
#include "esp_log.h"

// --- INÍCIO DAS CORREÇÕES ---
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
// Adiciona o header para o reportador de erros
#include "tensorflow/lite/micro/micro_error_reporter.h"
// --- FIM DAS CORREÇÕES ---

// Inclui o modelo que foi gerado pelo Python
#include "AnomalyModel.h"

static const char* TAG = "AnomalyDetector";

// --- CORREÇÃO: Adicionada a palavra-chave 'static' para evitar erros de redefinição ---
static const float data_min[] = { -0.00239096, -0.00067029 };
static const float data_scale[] = { 9.83932384e-06, 8.47170853e-09 };
static const float ANOMALY_THRESHOLD = 0.006253;
// ------------------------------------------------------------------------------------

// --- Configuração do TensorFlow Lite ---
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;
constexpr int kTensorArenaSize = 5 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

// --- CORREÇÃO: Cria um 'ErrorReporter' para o TensorFlow Lite ---
tflite::ErrorReporter* error_reporter = nullptr;
tflite::MicroErrorReporter micro_error_reporter;
// ------------------------------------------------------------

AnomalyDetector::AnomalyDetector() {}

void AnomalyDetector::setup() {
  // --- CORREÇÃO: Inicializa o error_reporter ---
  error_reporter = &micro_error_reporter;
  // -------------------------------------------

  model = tflite::GetModel(anomaly_model_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG, "Erro: Versão do modelo incompatível!");
    return;
  }

  static tflite::MicroMutableOpResolver<4> op_resolver;
  op_resolver.AddFullyConnected();
  op_resolver.AddRelu();
  op_resolver.AddLogistic(); // --- CORREÇÃO: AddSigmoid foi substituído por AddLogistic ---
  op_resolver.AddReshape();

  // --- CORREÇÃO: Adicionado o error_reporter como último parâmetro ---
  static tflite::MicroInterpreter static_interpreter(model, op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;
  // --------------------------------------------------------------------

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(TAG, "Falha ao alocar tensores!");
    return;
  }

  input = interpreter->input(0);
  output = interpreter->output(0);
  
  _initialized = true;
  ESP_LOGI(TAG, "Módulo de Detecção de Anomalias com TinyML inicializado.");
}

bool AnomalyDetector::detect(uint32_t packet_count, uint64_t total_bytes) {
  if (!_initialized) return false;

  float norm_packet_count = ( (float)packet_count - data_min[0]) * data_scale[0];
  float norm_total_bytes = ( (float)total_bytes - data_min[1]) * data_scale[1];

  input->data.f[0] = norm_packet_count;
  input->data.f[1] = norm_total_bytes;

  if (interpreter->Invoke() != kTfLiteOk) {
    ESP_LOGE(TAG, "Falha na invocação do interpretador");
    return false;
  }

  float recon_packet_count = output->data.f[0];
  float recon_total_bytes = output->data.f[1];
  
  float error = (abs(norm_packet_count - recon_packet_count) + abs(norm_total_bytes - recon_total_bytes)) / 2.0;

  ESP_LOGI(TAG, "Análise TinyML - Erro de reconstrução: %.6f (Limite: %.6f)", error, ANOMALY_THRESHOLD);
  if (error > ANOMALY_THRESHOLD) {
    ESP_LOGW(TAG, "*** ANOMALIA DE TRÁFEGO DETECTADA! Erro: %.6f ***", error);
    return true;
  }

  return false;
}