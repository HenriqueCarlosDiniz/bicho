Import("env")
import os
import shutil

# =============================================================================
# Script de Automação do Ambiente Nativo PC (MinGW + SDL2)
# Garante que o compilador C++ e as DLLs de runtime estejam presentes no build
# =============================================================================

pio_packages_dir = os.path.expanduser("~/.platformio/packages/toolchain-gccmingw32")
mingw_bin_dir = os.path.join(pio_packages_dir, "bin")
sdl2_base_dir = "C:/tools/SDL2-mingw/SDL2-2.30.10/i686-w64-mingw32"
sdl2_bin_dir = os.path.join(sdl2_base_dir, "bin")

# Adiciona o compilador MinGW e o diretório bin do SDL2 ao PATH da sessão de build/exec
path_additions = []
if os.path.isdir(mingw_bin_dir):
    path_additions.append(mingw_bin_dir)
if os.path.isdir(sdl2_bin_dir):
    path_additions.append(sdl2_bin_dir)

for p in reversed(path_additions):
    env.PrependENVPath("PATH", p)

# Copia automaticamente a SDL2.dll e as DLLs de runtime do MinGW para a pasta do executável
def copy_runtime_dlls(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    os.makedirs(build_dir, exist_ok=True)

    dll_sources = [
        os.path.join(sdl2_bin_dir, "SDL2.dll"),
        os.path.join(mingw_bin_dir, "libgcc_s_dw2-1.dll"),
        os.path.join(mingw_bin_dir, "libstdc++-6.dll"),
        os.path.join(mingw_bin_dir, "libwinpthread-1.dll"),
    ]

    for dll_src in dll_sources:
        if os.path.isfile(dll_src):
            dll_dst = os.path.join(build_dir, os.path.basename(dll_src))
            if not os.path.isfile(dll_dst):
                shutil.copy2(dll_src, dll_dst)

# Protege SDL_MAIN_HANDLED no header local do M5GFX para evitar warning de redefinição
m5gfx_sdl_common = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"),
    env.subst("$PIOENV"),
    "M5GFX", "src", "lgfx", "v1", "platforms", "sdl", "common.hpp"
)
if os.path.isfile(m5gfx_sdl_common):
    with open(m5gfx_sdl_common, "r", encoding="utf-8") as f:
        header_txt = f.read()
    if "#ifndef SDL_MAIN_HANDLED" not in header_txt and "#define SDL_MAIN_HANDLED" in header_txt:
        header_txt = header_txt.replace(
            "#define SDL_MAIN_HANDLED",
            "#ifndef SDL_MAIN_HANDLED\n#define SDL_MAIN_HANDLED\n#endif",
            1
        )
        with open(m5gfx_sdl_common, "w", encoding="utf-8") as f:
            f.write(header_txt)

env.AddPostAction("$BUILD_DIR/${PROGNAME}${PROGSUFFIX}", copy_runtime_dlls)
