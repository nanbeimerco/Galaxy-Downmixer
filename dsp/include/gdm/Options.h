// Reader for the on-device options file (key=value lines), used until the
// control app exists. Example:
//
//   enabled=1            # 0: pass the input through untouched (A/B test)
//   swap=0
//   mute=TBL,TBR
//   master_db=-3
//   lfe_lpf=1
//   eq_file=/vendor/etc/galaxy_downmixer/eq.txt
//   log_interval_s=5     # 0 disables the periodic meter log
#pragma once

#include <string>
#include <vector>

#include "gdm/Settings.h"

namespace gdm {

struct Options {
    bool enabled = true;
    std::string eqFile;
    int logIntervalS = 5;
    std::vector<std::string> warnings;
};

// Updates `settings` (swap, mute, master, LFE) and returns the remaining
// options. Unknown keys and bad values are reported as warnings.
Options parseOptions(const std::string& text, Settings& settings);

}  // namespace gdm
