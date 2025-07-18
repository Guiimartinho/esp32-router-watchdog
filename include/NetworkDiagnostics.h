#ifndef NETWORK_DIAGNOSTICS_H
#define NETWORK_DIAGNOSTICS_H

class NetworkDiagnostics {
public:
  NetworkDiagnostics();
  void setup();

  // A função principal que retornará o status da internet
  bool isInternetConnected();

private:
  // Futuramente, podemos adicionar variáveis aqui para pings mais complexos
};

#endif