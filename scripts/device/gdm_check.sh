#!/system/bin/sh
# Runs on the phone as root (pushed by scripts/device_check.sh). Read-only.

section() { echo; echo "===== $1"; }

section "module"
MOD=/data/adb/modules/galaxy_downmixer
if [ -d "$MOD" ]; then
    grep '^version=' "$MOD/module.prop"
    for f in disable remove; do [ -e "$MOD/$f" ] && echo "flag: $f"; done
else
    echo "not installed"
fi

section "files seen under /vendor (SELinux labels)"
ls -Z /vendor/lib/soundfx/libgalaxydownmixer.so /vendor/lib64/soundfx/libgalaxydownmixer.so \
    /vendor/etc/audio_effects_sec.xml /vendor/etc/galaxy_downmixer/options.conf 2>&1
echo "audio_effects_sec.xml entries: $(grep -c galaxy_downmixer /vendor/etc/audio_effects_sec.xml)"

section "loaded by the audio HAL"
HAL=$(pidof android.hardware.audio.service)
echo "android.hardware.audio.service pid $HAL"
grep -m1 galaxydownmixer "/proc/$HAL/maps" || echo "libgalaxydownmixer.so NOT mapped"

DUMP=/data/local/tmp/gdm_af_dump.txt
dumpsys media.audio_flinger > "$DUMP" 2>&1

section "effect library list (dumpsys)"
grep -A7 'Library galaxy_downmixer' "$DUMP" || echo "not listed"
grep 'NOT loaded' -A3 "$DUMP" | grep -i galaxy

section "active playback tracks (Id Active Client Session PortId S Flags Format ChnMask SRate)"
grep -E ' yes +[0-9]+ +[0-9]+ +[0-9]+ [A-Z] +0x[0-9a-f]+ [0-9A-F]{8} [0-9A-F]{8} ' "$DUMP" | head

section "Galaxy Downmixer instances in effect chains"
grep -B3 -A22 'name: Galaxy Downmixer' "$DUMP" |
    grep -E 'effects for session|name:|Channels|^ +0x[0-9a-f]+ +[0-9]+ +[0-9]+|Galaxy Downmixer: session' || echo "none"

section "logcat (GalaxyDownmixer)"
logcat -d -s GalaxyDownmixer | tail -25

section "SELinux denials mentioning galaxy / hal_audio"
logcat -d | grep 'avc: *denied' | grep -iE 'galaxy|downmix' | tail -10
dmesg 2>/dev/null | grep 'avc: *denied' | grep -iE 'galaxy|downmix' | tail -10
echo "(end)"
