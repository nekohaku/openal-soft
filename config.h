/* Define if deprecated EAX extensions are enabled */
//#define ALSOFT_EAX

#ifdef OO
/* if doing an OpenOrbis PS4 Toolchain build: */

#ifndef OO_EXPORT
/* clang gcc a-la __declspec(dllexport) thingy */
#define OO_EXPORT __attribute__((visibility("default")))
#endif /* OO_EXPORT */

#ifndef AL_API
#define AL_API    OO_EXPORT
#endif /* AL_API */

#ifndef ALC_API
#define ALC_API   OO_EXPORT
#endif /* ALC_API */

#endif /* OO */

/* Define if HRTF data is embedded in the library */
#define ALSOFT_EMBED_HRTF_DATA

/* Define if we have the posix_memalign function */
#define HAVE_POSIX_MEMALIGN

/* Define if we have the _aligned_malloc function */
//#define HAVE__ALIGNED_MALLOC

/* Define if we have the proc_pidpath function */
//#define HAVE_PROC_PIDPATH

/* Define if we have the getopt function */
//#define HAVE_GETOPT

/* Define if we have DBus/RTKit */
//#define HAVE_RTKIT

/* Define if we have SSE CPU extensions */
#define HAVE_SSE
#define HAVE_SSE2
#define HAVE_SSE3
#define HAVE_SSE4_1

/* Define if we have ARM Neon CPU extensions */
//#define HAVE_NEON

/* Define if we have the ALSA backend */
//#define HAVE_ALSA

/* Define if we have the OSS backend */
//#define HAVE_OSS

/* Define if we have the PipeWire backend */
//#define HAVE_PIPEWIRE

/* Define if we have the Solaris backend */
//#define HAVE_SOLARIS

/* Define if we have the SndIO backend */
//#define HAVE_SNDIO

/* Define if we have the WASAPI backend */
//#define HAVE_WASAPI

/* Define if we have the DSound backend */
//#define HAVE_DSOUND

/* Define if we have the Windows Multimedia backend */
//#define HAVE_WINMM

/* Define if we have the PortAudio backend */
//#define HAVE_PORTAUDIO

/* Define if we have the PulseAudio backend */
//#define HAVE_PULSEAUDIO

/* Define if we have the JACK backend */
//#define HAVE_JACK

/* Define if we have the CoreAudio backend */
//#define HAVE_COREAUDIO

/* Define if we have the OpenSL backend */
//#define HAVE_OPENSL

/* Define if we have the Oboe backend */
//#define HAVE_OBOE

/* Define if we have the Wave Writer backend */
#define HAVE_WAVE

/* Define if we have the SDL2 backend */
//#define HAVE_SDL2

/* Define if we have the SceAudioOut backend */
#define HAVE_SCEAUDIOOUT

/* Define if we have dlfcn.h */
//#define HAVE_DLFCN_H

/* Define if we have pthread_np.h */
//#define HAVE_PTHREAD_NP_H

/* Define if we have malloc.h */
#define HAVE_MALLOC_H

/* Define if we have cpuid.h */
#define HAVE_CPUID_H

/* Define if we have intrin.h */
//#define HAVE_INTRIN_H

/* Define if we have guiddef.h */
//#define HAVE_GUIDDEF_H

/* Define if we have initguid.h */
//#define HAVE_INITGUID_H

/* Define if we have GCC's __get_cpuid() */
#define HAVE_GCC_GET_CPUID

/* Define if we have the __cpuid() intrinsic */
//#define HAVE_CPUID_INTRINSIC

/* Define if we have SSE intrinsics */
//#define HAVE_SSE_INTRINSICS

/* Define if we have pthread_setschedparam() */
//#define HAVE_PTHREAD_SETSCHEDPARAM

/* Define if we have pthread_setname_np() */
//#define HAVE_PTHREAD_SETNAME_NP

/* Define if we have pthread_set_name_np() */
//#define HAVE_PTHREAD_SET_NAME_NP

/* Define the installation data directory */
//#define ALSOFT_INSTALL_DATADIR "/app0/aldata"