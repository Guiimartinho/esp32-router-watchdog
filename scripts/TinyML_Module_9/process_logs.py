import os
import pyshark
import pandas as pd
import datetime
from tqdm import tqdm

# --- CONFIGURAÇÕES ---
# Coloque o caminho para a pasta onde estão seus logs do Wireshark
LOGS_FOLDER = 'Wireshark_Logs'  # '.' significa 'a pasta atual'

# Nome do arquivo CSV que será gerado
CSV_OUTPUT_FILE = 'network_metrics_dataset.csv'

# Janela de tempo para agrupar as métricas, em segundos (60s = 1 minuto)
TIME_WINDOW_SECONDS = 60
# ---------------------

def process_pcap_files(folder_path):
    """
    Lê todos os arquivos .pcapng em uma pasta, extrai as métricas
    e retorna um dicionário com os dados agregados por janela de tempo.
    """
    metrics = {}
    
    # Encontra todos os arquivos .pcapng na pasta
    pcap_files = [os.path.join(root, file) 
                  for root, _, files in os.walk(folder_path) 
                  for file in files if file.endswith(('.pcapng', '.pcap'))]

    if not pcap_files:
        print(f"Nenhum arquivo .pcapng ou .pcap encontrado na pasta '{folder_path}'")
        return None

    print(f"Encontrados {len(pcap_files)} arquivos de log para processar.")

    # Processa cada arquivo de log
    for file_path in pcap_files:
        print(f"\nProcessando arquivo: {os.path.basename(file_path)}")
        try:
            cap = pyshark.FileCapture(file_path)
            
            # A barra de progresso (tqdm) nos mostrará o andamento
            for packet in tqdm(cap, desc=f"Lendo {os.path.basename(file_path)}"):
                try:
                    # Pega o timestamp do pacote e arredonda para a nossa janela de tempo
                    ts = float(packet.sniff_timestamp)
                    window_ts = int(ts / TIME_WINDOW_SECONDS) * TIME_WINDOW_SECONDS
                    
                    # Inicializa a janela de tempo no nosso dicionário se for a primeira vez
                    if window_ts not in metrics:
                        metrics[window_ts] = {'packet_count': 0, 'total_bytes': 0}
                        
                    # Acumula as métricas: incrementa a contagem e soma o tamanho do pacote
                    metrics[window_ts]['packet_count'] += 1
                    metrics[window_ts]['total_bytes'] += int(packet.length)
                    
                except (AttributeError, KeyError):
                    # Ignora pacotes que possam estar malformados ou não ter os campos necessários
                    continue
            cap.close()
        except Exception as e:
            print(f"Erro ao processar o arquivo {file_path}: {e}")
            continue
            
    return metrics

def save_metrics_to_csv(metrics, output_file):
    """
    Converte o dicionário de métricas para um DataFrame do Pandas e salva em CSV.
    """
    if not metrics:
        print("Nenhuma métrica foi extraída. O arquivo CSV não será gerado.")
        return

    print("\nProcessamento de pacotes concluído. Preparando para salvar o CSV...")
    
    # Converte o dicionário para um DataFrame do Pandas
    df = pd.DataFrame.from_dict(metrics, orient='index')
    
    # Converte o timestamp (que é o índice do DataFrame) para um formato legível
    df.index = pd.to_datetime(df.index, unit='s')
    
    # Ordena os dados por data/hora
    df.sort_index(inplace=True)
    
    # Salva o DataFrame em um arquivo CSV
    df.to_csv(output_file, index_label='timestamp')
    print(f"Dataset salvo com sucesso em: {output_file}")


# --- Execução Principal ---
if __name__ == "__main__":
    collected_metrics = process_pcap_files(LOGS_FOLDER)
    save_metrics_to_csv(collected_metrics, CSV_OUTPUT_FILE)