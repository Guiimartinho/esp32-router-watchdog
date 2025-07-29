import os
import pandas as pd
import numpy as np
import tensorflow as tf
from tensorflow import keras
from sklearn.preprocessing import MinMaxScaler
from sklearn.model_selection import train_test_split
import joblib

# --- CONFIGURAÇÕES ---
CSV_FILE = 'network_metrics_dataset.csv'
SCALER_FILE = 'data_scaler.gz'
MODEL_H5_FILE = 'anomaly_detector.h5'
MODEL_TFLITE_FILE = 'anomaly_model.tflite'
MODEL_H_FILE = 'anomaly_model.h'

# --- MUDANÇA 1: Aumentar o número máximo de épocas ---
EPOCHS = 500 # Um número bem alto para dar espaço para o EarlyStopping funcionar
# --------------------------------------------------
BATCH_SIZE = 32

# --- FASE 1: PREPARAÇÃO DOS DADOS ---
print(f"--- Fase 1: Preparando dados do arquivo '{CSV_FILE}' ---")

# (O código de preparação de dados permanece o mesmo)
df = pd.read_csv(CSV_FILE)
data_for_training = df[['packet_count', 'total_bytes']]
scaler = MinMaxScaler()
data_scaled = scaler.fit_transform(data_for_training)
joblib.dump(scaler, SCALER_FILE)
print(f"Normalizador salvo em '{SCALER_FILE}'")
print("\n!!! GUARDE ESTES VALORES PARA USAR NO CÓDIGO DO ESP32 !!!")
print(f"Valores Mínimos (data_min): {scaler.min_}")
print(f"Valores de Escala (data_scale): {scaler.scale_}")
print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n")


# --- FASE 2: CONSTRUÇÃO E TREINAMENTO DO MODELO ---
print("--- Fase 2: Construindo e treinando o modelo Autoencoder ---")

model = keras.Sequential([
    keras.layers.Dense(16, activation='relu', input_shape=(data_scaled.shape[1],)),
    keras.layers.Dense(8, activation='relu'),
    keras.layers.Dense(4, activation='relu'),
    keras.layers.Dense(8, activation='relu'),
    keras.layers.Dense(16, activation='relu'),
    keras.layers.Dense(data_scaled.shape[1], activation='sigmoid')
])

model.compile(optimizer='adam', loss='mae')
model.summary()

# --- MUDANÇA 2: Definir a "paciência" para 20 ---
early_stopping_callback = keras.callbacks.EarlyStopping(monitor='val_loss', patience=20, restore_best_weights=True)
# 'restore_best_weights=True' é uma ótima prática: ele garante que o modelo final tenha os melhores pesos encontrados durante o treino.
# --------------------------------------------------

print("\nIniciando o treinamento do modelo...")
history = model.fit(data_scaled, data_scaled,
                    epochs=EPOCHS, # Usa o novo valor alto
                    batch_size=BATCH_SIZE,
                    validation_split=0.1,
                    shuffle=True,
                    callbacks=[early_stopping_callback]) # Usa o nosso novo callback

print("\nTreinamento concluído.")
model.save(MODEL_H5_FILE)
print(f"Modelo Keras salvo em '{MODEL_H5_FILE}'")


# --- FASE 3: CALCULAR O LIMITE DE ANOMALIA (THRESHOLD) ---
# (O código aqui permanece o mesmo)
print("\n--- Fase 3: Calculando o limite de anomalia ---")
reconstructions = model.predict(data_scaled)
train_loss = tf.keras.losses.mae(reconstructions, data_scaled)
threshold = np.mean(train_loss) + 2 * np.std(train_loss)
print("\n!!! GUARDE ESTE VALOR PARA USAR NO CÓDIGO DO ESP32 !!!")
print(f"Limite de Anomalia (ANOMALY_THRESHOLD): {threshold:.6f}")
print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n")


# --- FASE 4: CONVERTER PARA TENSORFLOW LITE E GERAR ARQUIVO .H ---
# (O código aqui permanece o mesmo)
print(f"--- Fase 4: Convertendo o modelo para C++ ---")
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()
with open(MODEL_TFLITE_FILE, 'wb') as f:
    f.write(tflite_model)
print(f"Modelo TFLite salvo em '{MODEL_TFLITE_FILE}'")

try:
    with open(MODEL_H_FILE, 'w') as h_file:
        h_file.write('#ifndef ANOMALY_MODEL_H\n')
        h_file.write('#define ANOMALY_MODEL_H\n\n')
        h_file.write('// Modelo treinado para detecção de anomalias na rede\n')
        h_file.write(f'const unsigned int {os.path.splitext(MODEL_TFLITE_FILE)[0]}_tflite_len = {len(tflite_model)};\n')
        h_file.write(f'const unsigned char {os.path.splitext(MODEL_TFLITE_FILE)[0]}_tflite[] = {{\n  ')
        
        for i, byte in enumerate(tflite_model):
            h_file.write(f'0x{byte:02x}, ')
            if (i + 1) % 12 == 0:
                h_file.write('\n  ')
        
        h_file.write('\n};\n\n')
        h_file.write('#endif // ANOMALY_MODEL_H\n')
        
    print(f"Modelo C++ salvo com sucesso em '{MODEL_H_FILE}'")
    print("\nPROCESSO CONCLUÍDO!")

except Exception as e:
    print(f"\nErro ao gerar o arquivo .h: {e}")
    print("Se o erro persistir, use o comando 'xxd -i anomaly_model.tflite > anomaly_model.h' no seu terminal (Git Bash).")