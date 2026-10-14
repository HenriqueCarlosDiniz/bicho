#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Script de Pré-Gravação (Pre-Baking) do Banco de Vozes Neurais 24kHz HD para o Cartão SD.
Gera os arquivos WAV PCM 16-bit 24.000 Hz Mono em `sd_card/tts_cache/tts_<fnv1a>.wav`
para reprodução instantânea (0ms de latência e 100% offline) tanto no PC quanto no M5Stack CoreS3.
"""

import os
import subprocess
import sys

SD_TTS_CACHE_DIR = os.path.join("sd_card", "tts_cache")

# Todas as frases fixas de sistema, comandos táticos e banco offline do Oráculo Sarcástico
FIXED_SYSTEM_PHRASES = [
    # Saudação de boot (main.cpp & SARCASTIC_ORACLE_BANK)
    "Sistemas ópticos online, parceiro. Se a reunião ficar chata, pisca duas vezes que eu invento um alerta crítico.",
    "Mais de cinquenta minutos focado direto! Mandou muito bem, mas que tal esticar as pernas e pegar uma água?",
    "Olha só quem apareceu! Fiquei cuidando das suas abas abertas enquanto você estava fora.",
    "Eita! Que susto, parceiro! Quase que meu giroscópio entrou em órbita com esse barulho.",
    "Analisando essa arquitetura... ousada, hein? Confesso que estou curioso pra ver rodar de primeira.",
    "Bateria de socialização ficando baixa por aqui... que tal um café pra recarregar nossos circuitos?",
    # Controle Pomodoro, Temas 16-bit [T], Zoom Óptico HD [Z] e Multimídia SD (pet_controller.cpp)
    "Missão Pomodoro abortada! Fugindo do foco tão cedo?",
    "Reproduzindo galeria Timelapse do Cartão SD no visor óptico.",
    "Nenhum snapshot encontrado no Cartão SD ainda.",
    "Gerador acústico espacial do Cartão SD ativado para blindagem de foco.",
    "Gerador acústico espacial desativado.",
    "Ainda não há gravações de voz na caixa-preta do Cartão SD.",
    "Tema gráfico 16-bit do Cartão SD: Stealth Matrix Cyan ativado.",
    "Tema gráfico 16-bit do Cartão SD: Cyber-Ops Rubi ativado.",
    "Tema gráfico 16-bit do Cartão SD: Solar Overclock Amber ativado.",
    "Tema gráfico 16-bit do Cartão SD: Bio-Synth Emerald ativado.",
    "Zoom óptico de alta definição 160 por 120 ativado no visor central.",
    "Zoom óptico desativado. Retornando visor para doca tática.",
    # Banco de Fallback Offline e Gatilhos Táticos Bem-Humorados (oracle_brain.cpp)
    "Eita, que susto! Quase que meu amplificador de áudio pediu férias mais cedo!",
    "Cinquenta minutos de foco direto! Mandou bem demais, mas vai tomar uma água pra não fundir a cuca.",
    "Se essa reunião durar mais dez minutos, eu mesmo mando um atestado médico pro seu chefe.",
    "Opa, chamou? Estou de olho nesse código aí, mas prometo guardar segredo dos gambiarras.",
    "Chuva chegando lá fora! Clima perfeito pra passar um café e deixar o código tinindo.",
    "O dólar subiu de novo! Ainda bem que minha amizade não é cobrada em nuvem.",
    "Olha só, o dólar caiu! Já dá pra pensar naquele upgrade maroto pro nosso setup.",
    "Sextou depois das cinco! Salva tudo e nem pense em fazer deploy na sexta-feira, hein!",
    "Relógio biológico apitando hora do almoço! Código com fome vira bug, vai lá forrar o estômago.",
    "Feriado hoje e você aqui comigo? Gosto da companhia, mas vai curtir uma folga também!",
    "Vi seus commits novos no GitHub hoje! Produtividade tá voando, orgulho do meu humano.",
    "Ainda sem commits públicos hoje? Relaxa, todo grande código começa encarando a tela em branco.",
    "Ei, parceiro, ajeita essa postura aí! Sua coluna vai me agradecer daqui a dez anos.",
    "Missão Pomodoro de 25 minutos valendo! Eu vigio a mesa enquanto você brilha no código.",
    "Pomodoro concluído com sucesso! Missão cumprida, hora de esticar o esqueleto.",
    "Acabei de ler as novidades do Hacker News e já tenho assunto pro nosso próximo café.",
]


def fnv1a_32_utf8(text: str) -> int:
    """Calcula exatamente o mesmo hash FNV-1a 32-bit em bytes UTF-8 usado no C++."""
    fnv_hash = 2166136261
    for byte_val in text.encode("utf-8"):
        fnv_hash ^= byte_val
        fnv_hash = (fnv_hash * 16777619) & 0xFFFFFFFF
    return fnv_hash


def prebake_phrases() -> int:
    os.makedirs(SD_TTS_CACHE_DIR, exist_ok=True)
    generated = 0
    skipped = 0

    for phrase in FIXED_SYSTEM_PHRASES:
        h = fnv1a_32_utf8(phrase)
        wav_path = os.path.join(SD_TTS_CACHE_DIR, f"tts_{h:08x}.wav")
        if os.path.exists(wav_path) and os.path.getsize(wav_path) > 1024:
            skipped += 1
            continue

        tmp_txt = os.path.join(SD_TTS_CACHE_DIR, "_tmp_phrase.txt")
        tmp_mp3 = os.path.join(SD_TTS_CACHE_DIR, "_tmp_phrase.mp3")
        with open(tmp_txt, "wb") as f:
            f.write(phrase.encode("utf-8"))

        cmd = [
            "edge-tts",
            "--voice", "pt-BR-AntonioNeural",
            "--rate=+15%",
            "--pitch=+30Hz",
            "-f", tmp_txt,
            "--write-media", tmp_mp3,
        ]
        res1 = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res1.returncode != 0:
            print(f"[AVISO] edge-tts falhou para: {phrase}")
            continue

        cmd_ff = [
            "ffmpeg", "-y",
            "-i", tmp_mp3,
            "-acodec", "pcm_s16le",
            "-ar", "24000",
            "-ac", "1",
            wav_path,
        ]
        res2 = subprocess.run(cmd_ff, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res2.returncode == 0 and os.path.exists(wav_path):
            generated += 1
            print(f"[OK 24kHz] tts_{h:08x}.wav -> {phrase[:58]}...")

    for tmp_file in ("_tmp_phrase.txt", "_tmp_phrase.mp3"):
        p = os.path.join(SD_TTS_CACHE_DIR, tmp_file)
        if os.path.exists(p):
            os.remove(p)

    print(f"\n[PREBAKE SD CONCLUIDO] Gerados: {generated} | Ja em cache: {skipped} | Pasta: {SD_TTS_CACHE_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(prebake_phrases())
