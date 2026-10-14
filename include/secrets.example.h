#ifndef SECRETS_EXAMPLE_H
#define SECRETS_EXAMPLE_H

// =============================================================================
// Modelo de Configuração de Credenciais e Rede (Padrão UPPER_CASE)
// Copie este arquivo para "include/secrets.h" e preencha com suas chaves reais.
// O arquivo "include/secrets.h" está listado no .gitignore e não será versionado.
// =============================================================================

constexpr const char* WIFI_SSID = "SUA_REDE_WIFI_AQUI";
constexpr const char* WIFI_PASSWORD = "SUA_SENHA_WIFI_AQUI";
constexpr const char* AI_API_KEY = "SUA_CHAVE_DE_API_MULTIMODAL_AQUI";
constexpr const char* AI_API_ENDPOINT = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent";

#endif // SECRETS_EXAMPLE_H
