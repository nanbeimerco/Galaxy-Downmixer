// Reader for Equalizer APO configuration text (the subset relevant to a
// stereo headphone PEQ).
//
// Supported:
//   Preamp: <gain> dB
//   Filter[ <n>]: ON|OFF <type> Fc <f> Hz [Gain <g> dB] [Q <q> | BW Oct <bw>]
//       types: PK, PEQ, MODAL, LSC, HSC, LP, LPQ, HP, HPQ, BP, NO, AP, NONE
//   Channel: L | R | all | L R | 1 | 2 | 1 2
//   # comments
//
// Lines before the first "Channel:" (and after "Channel: all") apply to both
// ears and go to `common`; "Channel: L" / "Channel: R" sections go to
// `left` / `right`. Biquads are linear and time-invariant, so collecting the
// filters per section does not change the response APO would produce.
// Anything else is skipped and reported in `warnings`.
#pragma once

#include <string>
#include <vector>

#include "gdm/Settings.h"

namespace gdm {

struct ApoProfile {
    PeqSection common;
    PeqSection left;
    PeqSection right;
    std::vector<std::string> warnings;  // "line N: ..." in English
};

ApoProfile parseApo(const std::string& text);

// Reads a file (UTF-8 or UTF-16 with BOM). On failure returns a profile whose
// warnings explain the error.
ApoProfile parseApoFile(const std::string& path);

// Copies the three sections into the common / per-ear PEQ slots.
inline void applyProfile(const ApoProfile& profile, Settings& settings) {
    settings.commonPeq = profile.common;
    settings.leftPeq = profile.left;
    settings.rightPeq = profile.right;
}

}  // namespace gdm
