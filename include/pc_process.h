#ifndef PC_PROCESS_H
#define PC_PROCESS_H

// =============================================================================
// Processos auxiliares do emulador de PC (ex.: scripts/webcam_tracker.py)
// =============================================================================
#if !defined(ARDUINO)

// Inicia um processo em background vinculado ao emulador: no Windows ele entra num Job Object
// com KILL_ON_JOB_CLOSE, então é encerrado junto com o emulador (mesmo se o emulador for morto à força
// e a câmera não fica presa por um rastreador órfão). Retorna false se não conseguiu iniciar.
bool spawnBackgroundChildProcess(const char* command_line);

#endif

#endif // PC_PROCESS_H
