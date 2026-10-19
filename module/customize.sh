# Magisk installer hook (sourced by Magisk's module installer).
#
# Builds the overlay for /vendor/etc/audio_effects_sec.xml from the phone's
# own copy at install time, so firmware updates are picked up by reinstalling.

ORIG=/vendor/etc/audio_effects_sec.xml
OUT=$MODPATH/system/vendor/etc/audio_effects_sec.xml

ui_print "- Galaxy Downmixer (stage 0)"

[ "$API" -ge 30 ] || abort "! Android 11 or newer is required"
case "$ARCH" in
    arm|arm64) ;;
    *) abort "! Unsupported architecture: $ARCH" ;;
esac
[ -f "$ORIG" ] || abort "! $ORIG not found (this module targets the Galaxy S21 effect config)"

mkdir -p "$MODPATH/system/vendor/etc"
if ! awk -f "$MODPATH/patch_effects_xml.awk" "$ORIG" > "$OUT"; then
    abort "! Could not patch $ORIG"
fi
rm -f "$MODPATH/patch_effects_xml.awk"
ui_print "- Patched audio_effects_sec.xml ($(grep -c galaxy_downmixer "$OUT") entries)"

if [ "$ARCH" != "arm64" ]; then
    rm -rf "$MODPATH/system/vendor/lib64"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
for d in lib lib64; do
    [ -d "$MODPATH/system/vendor/$d/soundfx" ] &&
        set_perm_recursive "$MODPATH/system/vendor/$d/soundfx" 0 0 0755 0644 u:object_r:vendor_file:s0
done
set_perm_recursive "$MODPATH/system/vendor/etc" 0 0 0755 0644 u:object_r:vendor_configs_file:s0

ui_print "- Options: /data/adb/modules/galaxy_downmixer/system/vendor/etc/galaxy_downmixer/options.conf"
ui_print "- Reboot to activate"
