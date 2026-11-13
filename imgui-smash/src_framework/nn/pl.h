#pragma once

#include "types.h"

#include <cstddef>

// System shared fonts. Exported by the SDK module of Smash 13.0.5 (sdk dynamic symbols
// _ZN2nn2pl21RequestSharedFontLoadENS0_14SharedFontTypeE, ...22GetSharedFontLoadState..., ...20GetSharedFontAddress...,
// ...17GetSharedFontSize...). Each aborts on an IPC failure; the game's NPDM grants pl:u. The address points into
// read-only system shared memory that stays mapped for the life of the process.
namespace nn::pl
{

    enum SharedFontType {
        STANDARD = 0x00, //	Japan, US and Europe (Standard)
        CHINESE_SIMPLIFIED = 0x01, //	Chinese Simplified
        EXT_CHINESE_SIMPLIFIED = 0x02, //	Extended Chinese Simplified
        CHINESE_TRAD = 0x03, //	Chinese Traditional
        KOREAN = 0x04, //	Korean (Hangul)
        NN_EXT = 0x05 //	Nintendo Extended
    };

    enum SharedFontLoadState { SharedFontLoadState_Loading = 0, SharedFontLoadState_Loaded = 1 };

    void RequestSharedFontLoad(SharedFontType);
    SharedFontLoadState GetSharedFontLoadState(SharedFontType);
    const void *GetSharedFontAddress(SharedFontType);
    size_t GetSharedFontSize(SharedFontType);
} // namespace nn::pl
