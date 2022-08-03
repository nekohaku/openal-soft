/**
 * OpenAL cross platform audio library
 * Copyright (C) 2018 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "sceaudioout.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>

#include "albyte.h"
#include "alc/alconfig.h"
#include "aloptional.h"
#include "almalloc.h"
#include "alnumeric.h"
#include "core/device.h"
#include "core/logging.h"
#include "core/helpers.h"
#include "threads.h"
#include "vector.h"
#include "ringbuffer.h"

#ifdef OOPS4
/* OpenOrbis */
#include <orbis/UserService.h>
#include <orbis/AudioOut.h>
#include <orbis/AudioIn.h>
#define NIKAL_USER_ID_LIST_TYPE OrbisUserServiceLoginUserIdList
#else
/* Sony SDK, not tested, use with caution, do NOT ask. */
#include <user_service.h>
#include <audioout.h>
#include <audioin.h>
#define NIKAL_USER_ID_LIST_TYPE SceUserServiceLoginUserIdList
#endif /* !OOPS4 */

/*
    To stay compatible with both SIE SDK and OpenOrbis,
    no constants are used here, and the only "user id list" type is defined as a macro.
    The only types used are `const void*`, `int`, `uint` and that user id list type.

    some common PS4 api details:
    0 - ORBIS_OK / SCE_OK, successful operation
    (expr < 0) - did an SCE call fail?
    int - a type used for handles and error codes (kinda like HRESULT but signed)
    0xFF - a SYSTEM user id, used for audio ports that are not bound to a specific user
    4 - the amount of maximum local logged on users
*/


namespace {

struct NikalUserServiceLoginUserIdList {
    /* maximum logged on user count is always 4 on PS4 no matter the SDK */
    int userIdsList[4]{-1,-1,-1,-1}; /* initialize with USER_ID_INVALIDs */
};

const std::string DeviceNames[] = {
    /* these ports do not require a specific userid and operate under SYSTEM */
    "MAIN",
    "BGM",
    "AUX",
    /* these ports require a non-SYSTEM valid user id in order to operate */
    "VOICE1", "VOICE2", "VOICE3", "VOICE4",
    "PERSONAL1", "PERSONAL2", "PERSONAL3", "PERSONAL4",
    "PADSPK1", "PADSPK2", "PADSPK3", "PADSPK4"
};

/* device -> port */
int DevicePorts[] = {
    0, /* "MAIN" */
    1, /* "BGM" */
    127, /* "AUX" */
    2, 2, 2, 2, /* "VOICE-" */
    3, 3, 3, 3, /* "PERSONAL-" */
    4, 4, 4, 4 /* "PADSPK-" */
};

/* device -> required user id */
int DeviceUserIds[] = {
    /* SYSTEM user id */
    0xFF, /* "MAIN" */
    0xFF, /* "BGM" */
    0xFF, /* "AUX" */
    /* Look-up from users list */
    1, 2, 3, 4, /* "VOICE" 1,2,3,4 */
    1, 2, 3, 4, /* "PERSONAL" 1,2,3,4 */
    1, 2, 3, 4 /* "PERSONAL" 1,2,3,4 */
};

struct SceAudioOutBackend final : public BackendBase {
    SceAudioOutBackend(DeviceBase *device) noexcept : BackendBase{device} { }
    ~SceAudioOutBackend() override;

    void open(const char *name) override;
    bool reset() override;
    void start() override;
    void stop() override;

    /* SceAudioOut handle, must be closed when not in use */
    int mDeviceID{-1};

    uint mFrameSize{0};

    uint mFrequency{0u};
    DevFmtChannels mFmtChans{};
    DevFmtType     mFmtType{};
    uint mUpdateSize{0u};

    al::vector<al::byte> mBuffer;
    std::mutex mMutex;
    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    /* SceAudioOut mixer thread */
    int mixerProc();

    /*
        Provide our own prototypes for sce functions so that we do not rely on a specific SDK.
        As long as the symbol itself is provided and resolved, we can re_cast it into a proper function declaration.
    */
    using sceAudioOutOpen_t = int(*)(int, int, int, uint, uint, uint);
    using sceAudioOutOutput_t = int(*)(int /* handle */, const void* /* sound data, GRAIN * CHANNELS * SIZEOF_int_OR_float */);
    using sceAudioOutClose_t = int(*)(int /* handle */);

    sceAudioOutOpen_t msceAudioOutOpen{reinterpret_cast<sceAudioOutOpen_t>(sceAudioOutOpen)};
    sceAudioOutOutput_t msceAudioOutOutput{reinterpret_cast<sceAudioOutOutput_t>(sceAudioOutOutput)};
    sceAudioOutClose_t msceAudioOutClose{reinterpret_cast<sceAudioOutClose_t>(sceAudioOutClose)};

    DEF_NEWDEL(SceAudioOutBackend)
};

SceAudioOutBackend::~SceAudioOutBackend() {
    int ok{-1};

    /* be sure we're not trying to kill ourselves twice a row */
    if (mDeviceID >= 0) {
        /* will wait for the thread to quit gracefully */
        stop();
        /* kill it with fire */
        ok = msceAudioOutClose(mDeviceID);
        if (ok < 0) {
            /* uh oh.... we did wait and we're unable to close the port? */
            ERR("SceAudioOut Port closure failure 0x%X", ok);
        }

        mDeviceID = -1;
    }
}

int SceAudioOutBackend::mixerProc() {
    /*
        PS4-specific way to rename a thread and set the highest prio
    */
    scePthreadSetprio(pthread_self(), 256 /* HIGHEST */);
    scePthreadRename(pthread_self(), MIXER_THREAD_NAME);

    /* i have no idea what I'm doing */
    const size_t frame_step{mDevice->channelsFromFmt()};
    const size_t frame_size{mDevice->frameSizeFromFmt()};
    int ok{-1}; /* sce error code, 0 is success */

    while (!mKillNow.load(std::memory_order_acquire)) {
        al::span<al::byte> buffer{mBuffer};
        auto bufferptr{buffer.data()};
        auto buffersiz{buffer.size()};

        mDevice->renderSamples(bufferptr, static_cast<uint>(buffersiz / frame_size), frame_step);
        
        /* should output GRAIN(256) * CHANNCOUNT() */
        ok = msceAudioOutOutput(mDeviceID, buffer.data());
        if (ok < 0) {
            /* failed to output sound! */
            mDevice->handleDisconnect("SceAudioOutError .data() 0x%X", ok);
            break;
        }

        ok = msceAudioOutOutput(mDeviceID, nullptr);
        if (ok < 0) {
            /* failed to output sound! */
            mDevice->handleDisconnect("SceAudioOutError nullptr 0x%X", ok);
            break;
        }
    }

    if (ok >= 0) {
        /* last error code was OK then we must wait for data to output and return gracefully */
        ok = msceAudioOutOutput(mDeviceID, nullptr);
        if (ok < 0) {
            mDevice->handleDisconnect("SceAudioOutError nullptr outer 0x%X", ok);
        }
    }

    /* no sound should be playing when we reach this line, can return */
    return 0;
}

void SceAudioOutBackend::open(const char *name) {
    int indexInTable{-1},
        i{0},
        userId{-1}, /* target user id */
        ok{-1}, /* SCE error code */
        freq{48000}, /* SceAudioOut only supports 48000hz, nothing more, nothing less */
        sonyDataFmt{-1}, /* SceAudioOut channformat constant */
        alChanFmt{-1}, /* AL channel format constant */
        alDataFmt{-1}, /* AL data format constant */
        scehandle{-1}, /* SceAudioOut port handle */
        porttype{-1};
    
    NikalUserServiceLoginUserIdList usersList;

    TRACE("SceAudioOutBackend::open %s", name);

    if (!name || 0 == strlen(name)) {
        /* assume "MAIN" device as default */
        indexInTable = 0; /* first device index is "MAIN" */
        name = DeviceNames[indexInTable].c_str();
    }
    else {
        for (const auto& sname : DeviceNames) {
            if (sname == name) {
                indexInTable = i;
                break;
            }

            ++i;
        }
    }

    if (indexInTable < 0) {
        throw al::backend_exception{al::backend_error::NoDevice, "Invalid device name '%s'", name};
    }

    /* cast into SDK-appropriate type */
    if ((ok = sceUserServiceGetLoginUserIdList(reinterpret_cast<NIKAL_USER_ID_LIST_TYPE*>(&usersList))) < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Unable to enumerate users 0x%X", ok};
    }

    userId = DeviceUserIds[indexInTable];
    if (userId != 0xFF) {
        /* 1 becomes [0], the first user's index */
        userId = usersList.userIdsList[userId - 1];
    }

    if (userId < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid user id 0x%X", userId};
    }

    /*
        SceAudioOut only supports either Short16LE or Float32LE as data format.
        Mono, Stereo, or 7.1(STD?)
    */

    switch (mDevice->FmtType) {
        case DevFmtUByte:
        case DevFmtByte:
        case DevFmtUShort:
        case DevFmtShort: {
            /* use s16 if possible for s16 and smaller types */
            alDataFmt = DevFmtShort;

            switch (mDevice->FmtChans) {
                case DevFmtMono: {
                    sonyDataFmt = 0; /* S16 Mono */
                    alChanFmt = DevFmtMono;
                    break;
                }

                case DevFmtStereo: {
                    sonyDataFmt = 1; /* S16 Stereo */
                    alChanFmt = DevFmtStereo;
                    break;
                }

                default: {
                    sonyDataFmt = 6; /* S16 8CH 'STD layout' */
                    alChanFmt = DevFmtX71;
                    break;
                }
            }

            break;
        }

        default: {
            /* use float32 for int32 and higher */
            alDataFmt = DevFmtFloat;

            switch (mDevice->FmtChans) {
                case DevFmtMono: {
                    sonyDataFmt = 3; /* Float Mono */
                    alChanFmt = DevFmtMono;
                    break;
                }

                case DevFmtStereo: {
                    sonyDataFmt = 4; /* Float Stereo */
                    alChanFmt = DevFmtStereo;
                    break;
                }

                default: {
                    sonyDataFmt = 7; /* Float 8CH 'STD layout' */
                    alChanFmt = DevFmtX71;
                    break;
                }
            }

            break;
        }
    }

    porttype = DevicePorts[indexInTable];

    mFrequency = static_cast<uint>(freq);
    mFmtChans = static_cast<DevFmtChannels>(alChanFmt);
    mFmtType = static_cast<DevFmtType>(alDataFmt);
    mUpdateSize = clampu(
        mDevice->UpdateSize,
        64 * 4, /* minimum allowed SceAudioOut port 'granularity' */
        64 * 32 /* maximum allowed SceAudioOut port 'granularity' */
    );
    mFrameSize = BytesFromDevFmt(mFmtType) * ChannelsFromDevFmt(mFmtChans, 0 /* we never use ambisonic in SceAudio */);
    mBuffer.resize(mUpdateSize * ChannelsFromDevFmt(mFmtChans, 0 /* we never use ambisonic in SceAudio */));

    /* fill the buffer with zeroes */
    std::fill(mBuffer.begin(), mBuffer.end(), al::byte{});
    /* sooo for a stereo s16 this buffer should be 256*2 in size */

    scehandle = msceAudioOutOpen(userId, porttype, 0 /* device index:unused */, mUpdateSize, mFrequency, static_cast<uint>(sonyDataFmt));
    if (scehandle < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Unable to open audio handle 0x%X", scehandle};
    }

    mDeviceID = scehandle;
    
    /* ????????????????????? */
    reset();
}

bool SceAudioOutBackend::reset() {
    mDevice->Frequency = mFrequency;
    mDevice->FmtChans = mFmtChans;
    mDevice->FmtType = mFmtType;
    mDevice->UpdateSize = mUpdateSize;
    mDevice->BufferSize = static_cast<uint>(mBuffer.size());
    /* thanks kcat! */
    setDefaultWFXChannelOrder();
    return true;
}

void SceAudioOutBackend::start() {
    try {
        mKillNow.store(false, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&SceAudioOutBackend::mixerProc), this};
    }
    catch (std::exception& e) {
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start mixing thread: %s", e.what()};
    }
}

void SceAudioOutBackend::stop() {
    if (mKillNow.exchange(true, std::memory_order_acq_rel) || !mThread.joinable()) {
        return;
    }

    /* the thread will wait for SceAudio to complete and only then return */
    mThread.join();
}

struct SceAudioInCapture final : public BackendBase {
    SceAudioInCapture(DeviceBase *device) noexcept : BackendBase{device} { }
    ~SceAudioInCapture() override;

    int recordProc();

    void open(const char *name) override;
    void start() override;
    void stop() override;
    void captureSamples(al::byte *buffer, uint samples) override;
    uint availableSamples() override;

    RingBufferPtr mRing{nullptr};

    std::atomic<bool> mKillNow{true};
    std::thread mThread;

    int mDeviceID{-1};
    DevFmtType mFmtType{};
    DevFmtChannels mFmtChannels{};
    uint mFrequency{};

    /*
        OpenOrbis doesn't have correct headers for SceAudioIn,
        but it does provide the basic symbols as void(*)() functions and they are resolved at boot
        so we can re_cast them into proper functions...
    */
    using sceAudioInOpen_t = int(*)(int, uint, uint, uint, uint, uint);
    using sceAudioInClose_t = int(*)(int /* handle */);
    using sceAudioInInput_t = int(*)(int /* handle */, void* /* data buffer */);
    using sceAudioInGetSilentState_t = int(*)(int /* handle */);

    sceAudioInOpen_t msceAudioInOpen{reinterpret_cast<sceAudioInOpen_t>(sceAudioInOpen)};
    sceAudioInClose_t msceAudioInClose{reinterpret_cast<sceAudioInClose_t>(sceAudioInClose)};
    sceAudioInInput_t msceAudioInInput{reinterpret_cast<sceAudioInInput_t>(sceAudioInInput)};
    sceAudioInGetSilentState_t msceAudioInGetSilentState{reinterpret_cast<sceAudioInGetSilentState_t>(sceAudioInGetSilentState)};

    DEF_NEWDEL(SceAudioInCapture)
};

const std::string CaptureDeviceNames[] = {
    /* all names require a user id */
    "GENERAL1", "GENERAL2", "GENERAL3", "GENERAL4",
    "VOICE_CHAT1", "VOICE_CHAT2", "VOICE_CHAT3", "VOICE_CHAT4",
    "VOICE_RECOGNITION1", "VOICE_RECOGNITION2", "VOICE_RECOGNITION3", "VOICE_RECOGNITION4"
};

const int CaptureDevicePorts[] = {
    1, 1, 1, 1,
    0, 0, 0, 0,
    5, 5, 5, 5
};

const int CaptureDeviceUserIds[] = {
    1, 2, 3, 4,
    1, 2, 3, 4,
    1, 2, 3, 4
};

SceAudioInCapture::~SceAudioInCapture() {
    if (mDeviceID >= 0) {
        stop();
        msceAudioInClose(mDeviceID);
        mDeviceID = -1;
    }
}

void SceAudioInCapture::open(const char *name) {
    int indexInTable{-1},
        ok{-1},
        i{0},
        sonyDataFmt{-1},
        alChannFmt{-1},
        alDataFmt{-1},
        freq{16000}, /* SceAudioIn only supports this frequency */
        scehandle{-1},
        userId{-1},
        granularity{256};
    
    NikalUserServiceLoginUserIdList usersList;

    if (!name || 0 == strlen(name)) {
        // assume "GENERAL1" port by default
        indexInTable = 0;
        name = CaptureDeviceNames[indexInTable].c_str();
    }
    else {
        for (const auto& sname : CaptureDeviceNames) {
            if (sname == name) {
                indexInTable = i;
                break;
            }

            ++i;
        }
    }

    if (indexInTable < 0) {
        throw al::backend_exception{al::backend_error::NoDevice, "Invalid device name '%s'", name};
    }

    if ((ok = sceUserServiceGetLoginUserIdList(reinterpret_cast<NIKAL_USER_ID_LIST_TYPE*>(&usersList))) < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Unable to enumerate users 0x%X", ok};
    }

    userId = CaptureDeviceUserIds[indexInTable];
    userId = usersList.userIdsList[userId - 1];
    if (userId < 0) {
        throw al::backend_exception{al::backend_error::DeviceError, "Invalid user id 0x%X", ok};
    }

    alDataFmt = DevFmtShort;
    switch (mDevice->FmtChans) {
        case DevFmtMono: {
            sonyDataFmt = 0; // S16LE Mono
            alChannFmt = DevFmtMono;
            break;
        }

        default: {
            sonyDataFmt = 2; // S16LE Stereo
            alChannFmt = DevFmtStereo;
            break;
        }
    }

    mFmtType = static_cast<DevFmtType>(alDataFmt);
    mFmtChannels = static_cast<DevFmtChannels>(alChannFmt);
    mFrequency = static_cast<uint>(freq);
    mDevice->FmtType = mFmtType;
    mDevice->FmtChans = mFmtChannels;
    mDevice->Frequency = mFrequency;
    mDevice->DeviceName = name;


    // OpenOrbis does not have the correct handles for SceAudioIn as of August 2nd 2022
    // do a recast :D
    

    
    // SceAudioIn only supports Signed 16 LE format
}

void SceAudioInCapture::start() {
    /* TODO !*/
}

void SceAudioInCapture::stop() {
    /* TODO !*/
}

uint SceAudioInCapture::availableSamples() {
    return static_cast<uint>(mRing->readSpace());
}

void SceAudioInCapture::captureSamples(al::byte *buffer, uint samples) {
    mRing->read(buffer, samples);
}

} // namespace

BackendFactory &SceAudioOutBackendFactory::getFactory() {
    static SceAudioOutBackendFactory factory{};
    return factory;
}

bool SceAudioOutBackendFactory::init() {
    int ok{-1};
    /* allow double-initialization just in case some code already did that for us... */

    TRACE("Initializing SceAudioOutBackendFactory...");

    ok = sceUserServiceInitialize(nullptr /* int* optPI32UserServiceThreadPriority */);
    if (ok < 0 && ok != static_cast<int>(0x80960003) /* already initialized */) {
        ERR("SceUserService init fail 0x%X", ok);
        return false;
    }

    ok = sceAudioOutInit();
    if (ok < 0 && ok != static_cast<int>(0x8026000e) /* already initialized */) {
        ERR("SceAudioOut init fail 0x%X", ok);
        return false;
    }

    TRACE("SceAudioOutBackendFactory OK");
    return true;
}

bool SceAudioOutBackendFactory::querySupport(BackendType type) {
    return
        type == BackendType::Playback ||
        type == BackendType::Capture;
}

std::string SceAudioOutBackendFactory::probe(BackendType type) {
    std::string outnames;

    if (type == BackendType::Playback) {
        for (const auto& sname : DeviceNames) {
            /* should include the nullbyte */
            outnames.append(sname.c_str(), sname.length() + 1);
        }
    }
    else if (type == BackendType::Capture) {
        for (const auto& sname : CaptureDeviceNames) {
            /* should include the nullbyte */
            outnames.append(sname.c_str(), sname.length() + 1);
        }
    }

    /* ??? */
    outnames.append("\0");

    return outnames;
}

BackendPtr SceAudioOutBackendFactory::createBackend(DeviceBase *device, BackendType type) {
    if (type == BackendType::Playback) {
        return BackendPtr{new SceAudioOutBackend{device}};
    }
    else if (type == BackendType::Capture) {
        return BackendPtr{new SceAudioInCapture{device}};
    }

    return BackendPtr{};
}

