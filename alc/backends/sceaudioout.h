#ifndef BACKENDS_SCEAUDIOOUT_H
#define BACKENDS_SCEAUDIOOUT_H

#include "base.h"

struct SceAudioOutBackendFactory final : public BackendFactory {
public:
    bool init() override;

    bool querySupport(BackendType type) override;

    std::string probe(BackendType type) override;

    BackendPtr createBackend(DeviceBase *device, BackendType type) override;

    static BackendFactory &getFactory();
};

#endif /* BACKENDS_SCEAUDIOOUT_H */
