# ESP32 Router Watchdog & Network Monitor

Este projeto transforma um ESP32-S3 em um hub inteligente para monitoramento, gerenciamento e garantia da conectividade da rede doméstica. Ele utiliza o sistema operacional de tempo real **FreeRTOS** para lidar com múltiplas tarefas de forma confiável e concorrente, incluindo análise de tráfego Wi-Fi e um dashboard web para controle.

---

### Core Architectural Features

* **Multi-Tasking RTOS:** Construído sobre FreeRTOS para lidar com diagnósticos de rede, descoberta de dispositivos, análise de tráfego e o servidor web em paralelo sem bloquear operações críticas.
* **Visual Status Indicator:** Utiliza o LED RGB onboard para feedback visual imediato sobre o status do sistema (Verde para Online, Vermelho para Offline/Falha, Roxo para modo de configuração).
* **Modular C++ Design:** Cada funcionalidade principal é encapsulada em sua própria classe para melhor organização, manutenibilidade e escalabilidade.

---

### Project Modules

#### Módulo 1: Resiliência Autônoma
* `Status:` ✅ **Implementado**
* **Recuperação Inteligente:** Tenta reiniciar o roteador através de um comando de software TR-064 para uma recuperação rápida e elegante.
* **Fallback Físico:** Usa um relé para cortar e restaurar a energia do roteador como Plano B, garantindo a recuperação mesmo que o software do roteador não responda.
* **Implementação Atual:** Uma máquina de estados robusta gerencia o status do sistema. Quando uma queda de internet é confirmada, ela segue um protocolo de reinicializações com tempo progressivo (intervalos de 2 min, 30 min e 2 horas).

#### Módulo 2: Diagnóstico em Múltiplas Camadas
* `Status:` ✅ **Implementado**
* **Análise Precisa de Falhas:** Verifica a conectividade através de múltiplos pontos críticos.
* **Tomada de Decisão Informada:** Evita reinicializações desnecessárias, identificando de forma confiável uma verdadeira queda de internet.
* **Implementação Atual:** Uma verificação resiliente em duas etapas é realizada. Primeiro, tenta uma requisição leve via HTTP GET. Apenas se isso falhar, inicia uma rajada de pings ICMP para um servidor confiável (`8.8.8.8`). Todas as chamadas de Ping no projeto são protegidas por um mutex para garantir a estabilidade.

#### Módulo 3: Visibilidade e Descoberta de Rede
* `Status:` ✅ **Implementado**
* **Mapeamento Ativo da Rede:** Varre a sub-rede para criar um inventário em tempo real de todos os dispositivos conectados.
* **Implementação Atual:** Para garantir a máxima estabilidade do sistema, a varredura de rede foi refatorada de um modelo paralelo para um **scan sequencial**. A tarefa principal executa um ping ICMP para cada IP na sub-rede de forma bloqueante, mas com pequenos delays para não travar o sistema. Isso elimina a complexidade e os riscos de instabilidade associados à criação de múltiplas tarefas de rede.

#### Módulo 4: Análise de Tráfego e Segurança (Modo Promíscuo)
* `Status:` ✅ **Implementado**
* **Monitoramento de Baixo Nível:** Captura pacotes Wi-Fi para monitorar a saúde da rede.
* **Captura de DNS:** Filtra e decodifica especificamente as consultas DNS (porta UDP 53), registrando qual dispositivo (por endereço MAC) está tentando acessar qual domínio.
* **Implementação Atual:** O sistema entra periodicamente em modo Sniffer (promíscuo). A tarefa de captura foi otimizada para ser extremamente leve, evitando a sobrecarga do sistema. Ela usa um filtro de hardware (`WIFI_PROMIS_FILTER_MASK_DATA`) para receber apenas pacotes de dados e possui um mecanismo de **encerramento gracioso** para evitar corrupção de memória ao alternar os modos do Wi-Fi.

#### Módulo 5: Interface de Controle Remoto (Web Server + API)
* `Status:` ✅ **Implementado**
* **Dashboard Centralizado:** Hospeda uma interface web para visualização de status em tempo real e controle sob demanda.
* **Portal de Provisionamento:** Ao ser ligado pela primeira vez, cria um Ponto de Acesso (AP) com um portal cativo para configurar as credenciais do Wi-Fi, roteador e Telegram de forma fácil.
* **Implementação Atual:** Um servidor web assíncrono fornece um dashboard que exibe o status da internet e a lista de dispositivos na rede (via API JSON `/status_json`). Também permite forçar uma reinicialização do roteador através de um botão na interface (via endpoint `/reboot`).

#### Módulo 6: Gateway de Notificação Inteligente
* `Status:` ✅ **Implementado**
* **Comunicação Proativa:** Envia alertas críticos e informativos para serviços de mensagens instantâneas.
* **Alertas Contextuais:** Notifica sobre o início do sistema, quedas de internet e recuperação do serviço.
* **Implementação Atual:** O sistema é totalmente integrado com o Telegram. Ele envia mensagens formatadas em Markdown para eventos chave, como "Super Monitor started", "Internet Outage Detected!", e "Internet Recovered!".

---

### Melhorias de Lógica e Estabilidade Implementadas

Durante o desenvolvimento e depuração, várias melhorias arquiteturais foram feitas para garantir a robustez do sistema:

* **✅ Encerramento Gracioso de Tarefas:** A `snifferTask` agora é encerrada através de uma flag de controle (`volatile bool`), permitindo que ela finalize suas operações e se auto-delete de forma segura. Isso eliminou uma causa crítica de **corrupção de memória (Heap Corruption)**.

* **✅ Proteção de Recursos Compartilhados (Mutex):** Foi implementado um mutex para a biblioteca `ESP32Ping`. Isso garante que diferentes tarefas (`NetworkDiscovery` e `NetworkDiagnostics`) possam usar a função de ping sem causar **condições de corrida (race conditions)**, que levavam a travamentos.

* **✅ Transições de Modo Wi-Fi Robustas:** A tarefa principal (`operationalTask`) foi reestruturada. Notavelmente, uma pausa estratégica foi adicionada após a reconexão do Wi-Fi (ao sair do modo Sniffer), dando tempo para a pilha de rede do ESP32 se estabilizar completamente.

* **✅ Aumento de Memória de Pilha (Stack):** O stack da `operationalTask` foi aumentado para 16KB para garantir uma operação robusta e prevenir qualquer `Stack Overflow`.

---

### O que Falta e Ideias com TinyML (Trabalho Futuro)

#### Módulos Planejados:
* **Módulo 7: Gerenciamento Ativo de Qualidade de Serviço (QoS):** Monitorar o consumo de banda e, via comandos TR-064, priorizar ou limitar dinamicamente o tráfego de dispositivos específicos.
* **Módulo 8: Log Persistente e Análise Histórica:** Armazenar logs de eventos importantes (quedas, reinicializações, picos de latência) em um cartão MicroSD para análise de estabilidade a longo prazo.

#### Módulos Futuros com Inteligência Artificial (TinyML):
* **🤖 Módulo 9: Detecção de Anomalias na Rede:**
    * **Lógica:** Coletar métricas da rede (pacotes/seg, latência, número de dispositivos) para treinar um modelo de "normalidade" (ex: Autoencoder). O modelo rodaria no ESP32 e, se o tráfego atual desviasse muito do padrão normal aprendido, um alerta de "comportamento anômalo" seria gerado.
    * **Benefício:** Detectar problemas antes que se tornem críticos, como um dispositivo IoT infectado gerando tráfego excessivo.

* **🔮 Módulo 10: Análise Preditiva de Falhas:**
    * **Lógica:** Usar um modelo de série temporal (ex: RNN, LSTM) treinado com dados de latência, jitter e perda de pacotes. O modelo aprenderia a reconhecer os padrões que normalmente precedem uma queda de internet.
    * **Benefício:** Enviar uma notificação de **alerta preditivo**, como "Atenção: Conexão instável detectada. Possível queda nos próximos minutos", permitindo uma ação proativa.

* **🛡️ Módulo 11: Impressão Digital e Segurança de Dispositivos (Device Fingerprinting):**
    * **Lógica:** Analisar as características do tráfego de um novo dispositivo que se conecta à rede (protocolos, tamanhos de pacote, servidores de destino). Um modelo de classificação (ex: Decision Tree, SVM) poderia identificar o tipo de dispositivo (celular, laptop, câmera de segurança).
    * **Benefício:** Aumentar a segurança, enviando um alerta como "Alerta de Segurança: Um novo dispositivo não identificado (possivelmente uma câmera) conectou-se à rede".