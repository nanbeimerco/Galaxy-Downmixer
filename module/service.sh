#!/system/bin/sh
# Runs in background after Android boots.
mkdir -p /data/snd/GalaxyDownmixer
chown audioserver:audio /data/snd/GalaxyDownmixer
chmod 775 /data/snd/GalaxyDownmixer
mkdir -p /sdcard/Music/GalaxyDownmixer
