#ifndef NETWORK_DIAGNOSTICS_H
#define NETWORK_DIAGNOSTICS_H

// --- MUDANÇA 1 de 3: Adicionar esta declaração ---
// "Avisa" ao compilador que a classe NetworkDiscovery existe, sem precisar incluir o .h aqui.
class NetworkDiscovery;
// ----------------------------------------------------

class NetworkDiagnostics {
public:
  NetworkDiagnostics();
  void setup();
  bool isInternetConnected();

  // --- MUDANÇA 2 de 3: Adicionar esta função ---
  // Função para que o main.cpp possa nos dar acesso ao outro módulo
  void setDiscoveryModule(NetworkDiscovery* discovery);
  // ----------------------------------------------------

private:
  // --- MUDANÇA 3 de 3: Adicionar este ponteiro ---
  // Para guardar o endereço do módulo que tem o mutex
  NetworkDiscovery* _discoveryModule;
  // ----------------------------------------------------
};

#endif