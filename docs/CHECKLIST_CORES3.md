# Checklist do primeiro teste no M5Stack CoreS3

## Preparar o cartão microSD (FAT32)

Tudo do Bicho fica numa pasta `bicho_sd/` na raiz do cartão. É o mesmo layout da pasta `sd_card/` do emulador:

```
bicho_sd/
  config.json          <- Wi-Fi, gemini_api_key, latitude/longitude, opções do Diário
  memory/              <- perfil, falas, episódios, hábitos, diário (events/, diary/)
  tts_cache/  sfx/  ambient/  gfx/  snapshots/  voice_notes/
```

- Para levar a memória do emulador, copie o conteúdo de `sd_card/` para `bicho_sd/` no cartão.
- Sem `config.json`, o Bicho cria um padrão a partir do `secrets.h` no primeiro boot.
- `sfx/`, `ambient/` e `gfx/` são gerados automaticamente se estiverem vazios.

### Privacidade e retenção (`config.json`)

| Chave | Padrão | Efeito |
|---|---|---|
| `privacy_mode` | `false` | Liga o modo privado ao ligar (no PC, alterna com a tecla **[L]**): não grava fotos, notas de voz, transcrições nem memórias, e não envia fotos ao Gemini |
| `snapshots_max_files` | `60` | Fotos mantidas no álbum (o mesmo limite vale para os quadros do timelapse) |
| `tts_cache_max_mb` | `15` | Teto do cache de voz; só frases fixas são gravadas |
| `log_max_lines` | `1000` | Linhas mantidas nos logs de fala e memória |
| `diary_keep_days` | `30` | Dias de eventos e capítulos do Diário |

A limpeza roda a cada boot, e o álbum também é podado a cada foto nova.

### Conversa ao vivo (`config.json`)

| Chave | Padrão | Efeito |
|---|---|---|
| `live_enabled` | `true` | Ao detectar uma frase dirigida ao Bicho, abre uma sessão da API Live do Gemini: conversa falada contínua, com interrupção, até ~15 s de silêncio. **Por enquanto só no emulador do PC**; no CoreS3 o Bicho segue usando o fluxo HTTP |
| `live_model` | `gemini-3.8-live` | Modelo da sessão Live |
| `live_voice` | `Orus` | Voz da sessão Live |
| `live_idle_timeout_s` | `15` | Segundos sem conversa até encerrar a sessão |

Se a sessão falhar (cota da API, rede), o Bicho volta sozinho ao fluxo HTTP por 1 minuto.

## Gravar e abrir o monitor serial

```
python -m platformio run -e m5stack-cores3 -t upload
python -m platformio device monitor -e m5stack-cores3
```

## O que conferir no log serial

| Item | Linha esperada |
|---|---|
| Cartão montado | `[SD-MEM] Armazenamento pronto em '/sd/bicho_sd'.` |
| config.json lido do cartão | `[SD-CFG] Configuracao carregada de '/sd/bicho_sd/config.json'` |
| Wi-Fi | `[HAL-CORES3] Conectando ao Wi-Fi '<sua rede>'...` |
| Relógio (NTP) | O Diário mostra `hoje=AAAA-MM-DD` (e não `relogio pendente`) |
| Gráficos | `[SD-GFX] Ativos graficos ... carregados do Cartao SD` |
| Mundo externo (TLS) | `[MUNDO-EXTERNO] Sincronizado | Clima: ...` |
| Gemini (TLS) | `[OracleBrain] Resposta Gemini ... recebida` no briefing |
| Voz | `[SD-TTS] Voz pronta: ...` e áudio no alto-falante |
| Ouvido | Fale "Ei Bicho, ..." de frente: `[OUVIDO-VAD] Frase detectada` |
| Notas de voz no cartão | `bicho_sd/voice_notes/voice_*.wav` aparece após uma frase |
| Câmera e álbum | `[SD-ALBUM] Snapshot salvo no Cartao SD` após um gatilho de voz |

## Riscos a observar

- **Memória:** reinícios com `abort()` ou `mbedtls ... alloc failed` durante uma chamada ao Gemini indicam falta de RAM interna para TLS. Isso acontece quando Oráculo, voz e mundo externo conectam ao mesmo tempo.
- **Travadas curtas da animação** ao começar a falar: a leitura e gravação do cache de voz (~300–450 KB) agora roda no loop principal, por causa do SPI compartilhado com o display.
- **Microfone e alto-falante:** depois de cada fala ou efeito sonoro, o VU meter lateral deve voltar a reagir à sua voz em até ~1 s.
