#ifndef LIVE_TRANSPORT_H
#define LIVE_TRANSPORT_H

#include <memory>
#include <string>

// =============================================================================
// Transporte WebSocket (TLS) da sessão Live.
// PC (Windows): WinHTTP, com as funções de WebSocket carregadas da winhttp.dll do sistema.
// CoreS3: ainda não implementado (createLiveTransport() devolve nullptr e o Bicho usa o fluxo HTTP).
// Uma thread pode receber enquanto outra envia; close() desbloqueia o receive().
// =============================================================================
class LiveTransport {
public:
    virtual ~LiveTransport() {}
    virtual bool connect(const std::string& host, const std::string& path, const std::string& extra_headers) = 0;
    virtual bool sendText(const std::string& text) = 0;
    virtual bool receiveText(std::string& out_text) = 0; // Bloqueia até uma mensagem completa; false se fechou
    virtual void close() = 0;
    virtual std::string lastError() const = 0;
};

std::unique_ptr<LiveTransport> createLiveTransport();
bool isLiveTransportSupported();

#endif // LIVE_TRANSPORT_H
