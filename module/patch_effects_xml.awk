# Adds the Galaxy Downmixer library and effect to an audio_effects XML (v2.0)
# and attaches the effect to the music stream as a post-processing effect.
# Idempotent: lines mentioning galaxy_downmixer are dropped and re-added, so
# patching an already patched file gives the same result.
# Exit status 3 means a required section was not found.

/galaxy_downmixer/ { next }

/<libraries>/ && !lib {
    print
    print "        <library name=\"galaxy_downmixer\" path=\"libgalaxydownmixer.so\"/>"
    lib = 1
    next
}

/<effects>/ && !eff {
    print
    print "        <effect name=\"galaxy_downmixer\" library=\"galaxy_downmixer\" uuid=\"a6e3f002-29bf-40a2-bfcf-df47efb3a224\"/>"
    eff = 1
    next
}

/<postprocess>/ { inpost = 1 }

inpost && /<stream type="music">/ && !music {
    print
    print "            <apply effect=\"galaxy_downmixer\"/>"
    music = 1
    next
}

inpost && /<\/postprocess>/ {
    if (!music) {
        print "        <stream type=\"music\">"
        print "            <apply effect=\"galaxy_downmixer\"/>"
        print "        </stream>"
        music = 1
    }
    inpost = 0
}

/<\/audio_effects_conf>/ && !music {
    print "    <postprocess>"
    print "        <stream type=\"music\">"
    print "            <apply effect=\"galaxy_downmixer\"/>"
    print "        </stream>"
    print "    </postprocess>"
    music = 1
}

{ print }

END { if (!lib || !eff || !music) exit 3 }
