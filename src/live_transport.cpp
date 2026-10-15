#include "live_transport.h"

#if defined(_WIN32) && !defined(ARDUINO)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <mutex>
#include <vector>

namespace {

// O winhttp.h do MinGW usado aqui não declara a API de WebSocket (Windows 8+); declaramos o necessário
// e buscamos as funções na winhttp.dll em tempo de execução.
constexpr DWORD OPTION_UPGRADE_TO_WEB_SOCKET = 114;
enum WebSocketBufferType : DWORD {
    WS_BINARY_MESSAGE = 0,
    WS_BINARY_FRAGMENT = 1,
    WS_UTF8_MESSAGE = 2,
    WS_UTF8_FRAGMENT = 3,
    WS_CLOSE = 4
};
using CompleteUpgradeFn = HINTERNET(WINAPI*)(HINTERNET, DWORD_PTR);
using SendFn = DWORD(WINAPI*)(HINTERNET, DWORD, PVOID, DWORD);
using ReceiveFn = DWORD(WINAPI*)(HINTERNET, PVOID, DWORD, DWORD*, DWORD*);
using CloseFn = DWORD(WINAPI*)(HINTERNET, USHORT, PVOID, DWORD);

struct WebSocketApi {
    CompleteUpgradeFn complete_upgrade = nullptr;
    SendFn send = nullptr;
    ReceiveFn receive = nullptr;
    CloseFn close = nullptr;
    bool ok = false;
};

const WebSocketApi& webSocketApi() {
    static WebSocketApi api;
    static std::once_flag once;
    std::call_once(once, []() {
        HMODULE dll = LoadLibraryA("winhttp.dll");
        if (dll == nullptr) return;
        api.complete_upgrade = reinterpret_cast<CompleteUpgradeFn>(GetProcAddress(dll, "WinHttpWebSocketCompleteUpgrade"));
        api.send = reinterpret_cast<SendFn>(GetProcAddress(dll, "WinHttpWebSocketSend"));
        api.receive = reinterpret_cast<ReceiveFn>(GetProcAddress(dll, "WinHttpWebSocketReceive"));
        api.close = reinterpret_cast<CloseFn>(GetProcAddress(dll, "WinHttpWebSocketClose"));
        api.ok = api.complete_upgrade && api.send && api.receive && api.close;
    });
    return api;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

class WinHttpLiveTransport : public LiveTransport {
public:
    ~WinHttpLiveTransport() override { close(); }

    bool connect(const std::string& host, const std::string& path, const std::string& extra_headers) override {
        const WebSocketApi& api = webSocketApi();
        if (!api.ok) {
            error_ = "WebSocket do WinHTTP indisponivel (requer Windows 8+)";
            return false;
        }
        session_ = WinHttpOpen(L"Bicho-Live/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_ == nullptr) return fail("WinHttpOpen");
        connection_ = WinHttpConnect(session_, widen(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connection_ == nullptr) return fail("WinHttpConnect");
        HINTERNET request = WinHttpOpenRequest(connection_, L"GET", widen(path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request == nullptr) return fail("WinHttpOpenRequest");
        if (!WinHttpSetOption(request, OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
            WinHttpCloseHandle(request);
            return fail("upgrade option");
        }
        std::wstring headers = widen(extra_headers);
        if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                headers.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr)) {
            WinHttpCloseHandle(request);
            return fail("handshake");
        }
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 101) {
            WinHttpCloseHandle(request);
            error_ = "handshake HTTP " + std::to_string(status);
            return false;
        }
        websocket_ = api.complete_upgrade(request, 0);
        WinHttpCloseHandle(request);
        if (websocket_ == nullptr) return fail("CompleteUpgrade");
        return true;
    }

    bool sendText(const std::string& text) override {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (websocket_ == nullptr) return false;
        DWORD rc = webSocketApi().send(websocket_, WS_UTF8_MESSAGE, const_cast<char*>(text.data()),
                                       static_cast<DWORD>(text.size()));
        if (rc != NO_ERROR) {
            error_ = "send " + std::to_string(rc);
            return false;
        }
        return true;
    }

    bool receiveText(std::string& out_text) override {
        out_text.clear();
        std::vector<char> buffer(64 * 1024);
        for (;;) {
            HINTERNET ws = websocket_;
            if (ws == nullptr) return false;
            DWORD read = 0, type = 0;
            DWORD rc = webSocketApi().receive(ws, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type);
            if (rc != NO_ERROR) {
                error_ = "receive " + std::to_string(rc);
                return false;
            }
            if (type == WS_CLOSE) {
                error_ = "conexao fechada pelo servidor";
                return false;
            }
            out_text.append(buffer.data(), read);
            if (type == WS_UTF8_MESSAGE || type == WS_BINARY_MESSAGE) {
                return true; // Fim da mensagem (fragmentos anteriores já acumulados)
            }
        }
    }

    void close() override {
        HINTERNET ws = nullptr;
        {
            std::lock_guard<std::mutex> lock(send_mutex_); // Não fecha no meio de um envio
            ws = websocket_;
            websocket_ = nullptr;
        }
        if (ws != nullptr) {
            webSocketApi().close(ws, 1000, nullptr, 0);
            WinHttpCloseHandle(ws); // Desbloqueia um receive() pendente em outra thread
        }
        if (connection_ != nullptr) { WinHttpCloseHandle(connection_); connection_ = nullptr; }
        if (session_ != nullptr) { WinHttpCloseHandle(session_); session_ = nullptr; }
    }

    std::string lastError() const override { return error_; }

private:
    bool fail(const char* step) {
        error_ = std::string(step) + " falhou (erro " + std::to_string(GetLastError()) + ")";
        return false;
    }

    HINTERNET session_ = nullptr;
    HINTERNET connection_ = nullptr;
    HINTERNET websocket_ = nullptr;
    std::mutex send_mutex_;
    std::string error_;
};

} // namespace

bool isLiveTransportSupported() {
    return webSocketApi().ok;
}

std::unique_ptr<LiveTransport> createLiveTransport() {
    if (!webSocketApi().ok) return nullptr;
    return std::unique_ptr<LiveTransport>(new WinHttpLiveTransport());
}

#else

bool isLiveTransportSupported() {
    return false;
}

std::unique_ptr<LiveTransport> createLiveTransport() {
    return nullptr; // CoreS3: transporte Live ainda não implementado (fase 3); usa o fluxo HTTP
}

#endif
