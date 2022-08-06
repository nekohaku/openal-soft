#!/usr/bin/env python3

# OpenAL-Soft OpenOrbis PS4 Toolchain build script by Nikita Krapivin

import os
import shutil
from pathlib import Path

print("> Building OpenAL-Soft for PS4 via OO")

LLVM_BIN_PATH = os.environ.get("OPENALPS4_LLVM_BIN_PATH")

if LLVM_BIN_PATH is None:
    LLVM_BIN_PATH = "D:\\SDK\\LLVM10\\bin"

# only works with the latest nightly release of the OpenOrbis PS4 Toolchain
OO_PS4_TOOLCHAIN = os.environ.get("OO_PS4_TOOLCHAIN")

if OO_PS4_TOOLCHAIN is None:
    raise RuntimeError("This script requires the OpenOrbis PS4 toolchain to be installed for this user.")

# ALSoft repo directory, usually the script's dir...
ROOT_DIR = os.path.dirname(os.path.realpath(__file__))

# a folder where all build files will be written to...
BUILD_FOLDER_NAME = "ps4"
BUILD_FOLDER = os.path.join(ROOT_DIR, "build", BUILD_FOLDER_NAME)

# Dependencies: libunwind is auto-merged into libc++ in nightly OO builds for simplicity
LINK_WITH = "-lc -lc++ -lkernel -lSceAudioOut -lSceAudioIn -lSceUserService -lSceSysmodule"

AL_ROOT = ROOT_DIR
AL_COMMON = os.path.join(ROOT_DIR, "common")
AL_INCLUDE = os.path.join(ROOT_DIR, "include")
AL_HRTF = os.path.join(ROOT_DIR, "hrtf")
AL_ALC = os.path.join(ROOT_DIR, "alc")
# do we really need these dirs?
#AL_CORE = os.path.join(ROOT_DIR, "core")
#AL_CORE_FILTERS = os.path.join(ROOT_DIR, "core", "filters")
#AL_CORE_MIXER = os.path.join(ROOT_DIR, "core", "mixer")

# name of the final output, must start with lib
FINAL_NAME = "libOpenALSoftOO"

ELF_PATH = os.path.join(BUILD_FOLDER, FINAL_NAME + ".elf")
OELF_PATH = os.path.join(BUILD_FOLDER, FINAL_NAME + ".oelf")
PRX_PATH = os.path.join(BUILD_FOLDER, FINAL_NAME + ".prx")
ALIB_PATH = os.path.join(BUILD_FOLDER, FINAL_NAME + ".a")
STUB_PATH = os.path.join(BUILD_FOLDER, FINAL_NAME + ".so")

COMPILER_DEFINES =  " -DORBIS=1 -D__ORBIS__=1 -DPS4=1 -DOO=1 -D__PS4__=1 -DOOPS4=1 -DRESTRICT=__restrict " + \
                    " -D__BSD_VISIBLE=1 -D_BSD_SOURCE=1 -D_DEBUG=1 -DAL_ALEXT_PROTOTYPES=1 "

COMPILER_WFLAGS  =  " -Winline -Wunused -Wall -Wextra -Wshadow -Wconversion -Wcast-align " + \
                    " -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual -Wpedantic -Werror "

COMPILER_FFLAGS  =  " -fPIC -fexceptions -fcxx-exceptions -fvisibility=hidden -std=c++14 -c "

COMPILER_IFLAGS  = f" -isysroot \"{OO_PS4_TOOLCHAIN}\" -isystem \"{OO_PS4_TOOLCHAIN}/include/c++/v1\" -isystem \"{OO_PS4_TOOLCHAIN}/include\" " + \
                   f" -I\"{AL_ROOT}\" -I\"{AL_COMMON}\" -I\"{AL_INCLUDE}\" -I\"{AL_HRTF}\" -I\"{AL_ALC}\" "

# use freebsd12 target, define some generic ps4 defines, force exceptions to ON since we have to do that for now
COMPILER_FLAGS   = f" --target=x86_64-pc-freebsd12-elf -funwind-tables -fuse-init-array " + \
                   f" {COMPILER_WFLAGS} {COMPILER_FFLAGS} {COMPILER_IFLAGS} {COMPILER_DEFINES} "

# link with PRX crtlib
LINKER_FLAGS = f" -m elf_x86_64 -pie --script \"{OO_PS4_TOOLCHAIN}/link.x\" " + \
               f" --eh-frame-hdr --verbose -L\"{OO_PS4_TOOLCHAIN}/lib\" {LINK_WITH} -o \"{ELF_PATH}\" "

COMPILER_EXE = os.path.join(LLVM_BIN_PATH, "clang++")
C_COMPILER_EXE = os.path.join(LLVM_BIN_PATH, "clang")
LINKER_EXE = os.path.join(LLVM_BIN_PATH, "ld.lld")
AR_EXE = os.path.join(LLVM_BIN_PATH, "llvm-ar")
TOOL_EXE = os.path.join(OO_PS4_TOOLCHAIN, "bin", "windows", "create-fself")

# Taken from CMakeLists.txt and added sony stuff at the end
SOURCE_FILES = """
common/alcomplex.cpp
common/alfstream.cpp
common/almalloc.cpp
common/alstring.cpp
common/dynload.cpp
common/polyphase_resampler.cpp
common/ringbuffer.cpp
common/strutils.cpp
common/threads.cpp
core/ambdec.cpp
core/ambidefs.cpp
core/bformatdec.cpp
core/bs2b.cpp
core/bsinc_tables.cpp
core/buffer_storage.cpp
core/context.cpp
core/converter.cpp
core/cpu_caps.cpp
core/devformat.cpp
core/device.cpp
core/effectslot.cpp
core/except.cpp
core/filters/biquad.cpp
core/filters/nfc.cpp
core/filters/splitter.cpp
core/fmt_traits.cpp
core/fpu_ctrl.cpp
core/helpers.cpp
core/hrtf.cpp
core/logging.cpp
core/mastering.cpp
core/mixer.cpp
core/uhjfilter.cpp
core/uiddefs.cpp
core/voice.cpp
core/mixer/mixer_c.cpp
al/auxeffectslot.cpp
al/buffer.cpp
al/effect.cpp
al/effects/autowah.cpp
al/effects/chorus.cpp
al/effects/compressor.cpp
al/effects/convolution.cpp
al/effects/dedicated.cpp
al/effects/distortion.cpp
al/effects/echo.cpp
al/effects/effects.cpp
al/effects/equalizer.cpp
al/effects/fshifter.cpp
al/effects/modulator.cpp
al/effects/null.cpp
al/effects/pshifter.cpp
al/effects/reverb.cpp
al/effects/vmorpher.cpp
al/error.cpp
al/event.cpp
al/extension.cpp
al/filter.cpp
al/listener.cpp
al/source.cpp
al/state.cpp
alc/alc.cpp
alc/alu.cpp
alc/alconfig.cpp
alc/context.cpp
alc/device.cpp
alc/effects/autowah.cpp
alc/effects/chorus.cpp
alc/effects/compressor.cpp
alc/effects/convolution.cpp
alc/effects/dedicated.cpp
alc/effects/distortion.cpp
alc/effects/echo.cpp
alc/effects/equalizer.cpp
alc/effects/fshifter.cpp
alc/effects/modulator.cpp
alc/effects/null.cpp
alc/effects/pshifter.cpp
alc/effects/reverb.cpp
alc/effects/vmorpher.cpp
alc/panning.cpp
core/mixer/mixer_sse.cpp
core/mixer/mixer_sse2.cpp
core/mixer/mixer_sse3.cpp
core/mixer/mixer_sse41.cpp
alc/backends/base.cpp
alc/backends/loopback.cpp
alc/backends/null.cpp
alc/backends/wave.cpp
alc/backends/sceaudioout.cpp
"""

# quoted .o paths
OBJECTS = ""

# the success exit code, usually 0 in most apps
EXIT_SUCCESS = 0

# Does the linking of all .o files in OBJECTS
def do_link() -> int:
    # here OBJECTS is already quoted
    runargs = f"{LINKER_FLAGS} {OBJECTS}"
    fullline = f"{LINKER_EXE} {runargs}"

    print(f"Invoking {fullline}")
    ec = os.system(fullline)

    return ec

# Creates a prx out of an elf
def do_prx() -> int:
    runargs = f"--paid 0x3800000000000011 --in \"{ELF_PATH}\" --out \"{OELF_PATH}\" --lib \"{PRX_PATH}\""
    fullline = f"{TOOL_EXE} {runargs}"

    print(f"Invoking {fullline}")
    ec = os.system(fullline)

    return ec

# Creates an .a static library
def do_static() -> int:
    runargs = f"rc \"{ALIB_PATH}\" {OBJECTS}"
    fullline = f"{AR_EXE} {runargs}"

    print(f"Invoking {fullline}")
    ec = os.system(fullline)

    return ec

# Creates a dummy .c file for a stub library
def do_so_stub() -> int:
    include_dir = os.path.join(ROOT_DIR, "include", "AL")
    files = [
        os.path.join(include_dir, "al.h"),
        os.path.join(include_dir, "alc.h"),
        os.path.join(include_dir, "alext.h"),
        os.path.join(include_dir, "efx.h")
    ]

    c_src =  "/* stub autogen start, DO NOT INCLUDE OR USE THIS FILE! */\n"
    c_src += "#ifdef __cplusplus\nextern \"C\" {\n#endif /* __cplusplus */"
    c_src += "\n\n"


    for filepath in files:
        with open(filepath, "r") as file:
            for line in file.readlines():
                if not line.startswith("ALC_API ") and not line.startswith("AL_API "):
                    continue
                namestart = line.index("_APIENTRY") + len("_APIENTRY")
                nameend = line.index("(")
                funcname = line[namestart:nameend].strip()
                c_src += "void " + funcname + "() { for(;;); }\n"
    

    c_src += "/* stub autogen end */\n"
    c_src += "#ifdef __cplusplus\n} /* extern \"C\" { */\n#endif /* __cplusplus */"
    c_src += "\n\n"

    outpath = os.path.join(BUILD_FOLDER, "alsoft_c_stub_source.c")
    outobjpath = os.path.join(BUILD_FOLDER, "alsoft_c_stub_source.o")
    with open(outpath, 'w') as file:
        file.write(c_src)

    runargs =   " -target x86_64-pc-linux-gnu -ffreestanding -nostdlib -fno-builtin -fPIC "
    runargs += f" -c \"{outpath}\" -o \"{outobjpath}\""
    fullline = f"{C_COMPILER_EXE} {runargs}"
    print(f"> Invoking {fullline}")
    ec = os.system(fullline)
    if ec != EXIT_SUCCESS:
        return ec

    runargs =   " -target x86_64-pc-linux-gnu -shared -fuse-ld=lld -ffreestanding -nostdlib -fno-builtin -fPIC "
    runargs += f" -L\"{OO_PS4_TOOLCHAIN}/lib\" {LINK_WITH} "
    runargs += f" \"{outobjpath}\" -o \"{STUB_PATH}\""
    fullline = f"{C_COMPILER_EXE} {runargs}"
    print(f"> Invoking {fullline}")
    ec = os.system(fullline)

    return ec

# Returns 0 on success, any other number otherwise
def run_compiler_at(srcfile: str, objfile: str, params: str) -> int:
    # wtf python?
    global OBJECTS

    runargs = f"{params} -o \"{objfile}\" \"{srcfile}\""
    fullline = f"{COMPILER_EXE} {runargs}"

    print(f"Invoking {fullline}")
    ec = os.system(fullline)

    if ec == EXIT_SUCCESS:
        OBJECTS += f"\"{objfile}\" "
    
    return ec

# Builds a single file
def build_file(file: str) -> int:
    srcpath = os.path.join(ROOT_DIR, file)
    objpath = os.path.join(BUILD_FOLDER, file + ".o")
    # make the directory structure...
    Path(os.path.dirname(objpath)).mkdir(parents=True, exist_ok=True)
    return run_compiler_at(srcpath, objpath, COMPILER_FLAGS)

# Does the actual build, must be defined the last and called the first
def do_build():
    # clean on every build for now
    shutil.rmtree(BUILD_FOLDER, ignore_errors=True)

    # ensure the build dir is present...
    Path(BUILD_FOLDER).mkdir(parents=True, exist_ok=True)

    print(f"> OO_PS4_TOOLCHAIN = {OO_PS4_TOOLCHAIN}")
    print(f"> ROOT_DIR         = {ROOT_DIR}")
    print(f"> BUILD_FOLDER     = {BUILD_FOLDER}")

    for file in SOURCE_FILES.splitlines():
        # ignore empty lines
        if not file:
            continue

        ec = build_file(file)
        if ec != EXIT_SUCCESS:
            return ec
    
    # all .o objects have been built now...
    ec = do_link()
    if ec != EXIT_SUCCESS:
        return ec
    
    ec = do_prx()
    if ec != EXIT_SUCCESS:
        return ec
    
    ec = do_static()
    if ec != EXIT_SUCCESS:
        return ec

    ec = do_so_stub()
    if ec != EXIT_SUCCESS:
        return ec

    return ec

# yay
print(f"> Build result = {do_build()}")

# remove later
#os.system("pause")
