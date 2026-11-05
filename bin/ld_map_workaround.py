# PlatformIO pre-script: keep linking working when the project lives under
# a path with non-ASCII characters (e.g. Cyrillic folder names on Windows).
#
# Root cause: the pinned xtensa GCC 8.4.0 (espressif32@6.9.0) cannot open
# ABSOLUTE paths containing non-ASCII characters, and the platform passes
# the link map as an absolute -Map path. Result:
#   ld.exe: cannot open map file C:/.../firmware.map: No such file or directory
# Everything else uses project-relative paths, so only this flag breaks.
#
# Workaround: when PROJECT_DIR has non-ASCII characters, append a
# project-relative -Map AFTER the platform one. GNU ld honors the last
# -Map occurrence, so the map still lands at the same file and linking
# proceeds. For ASCII checkouts this script is a no-op.
#
# NOTE: an all-ASCII checkout path remains the preferred fix — other
# toolchain helpers of this generation (esptool, GDB scripts) may trip
# over non-ASCII paths as well.

Import("env")


def _has_non_ascii(s):
    try:
        for ch in str(s):
            if ord(ch) > 127:
                return True
    except Exception:
        pass
    return False


try:
    if _has_non_ascii(env.get("PROJECT_DIR", "")):
        pioenv = str(env.get("PIOENV", ""))
        if pioenv:
            env.Append(LINKFLAGS=["-Wl,-Map,.pio/build/%s/firmware.map" % pioenv])
            print("ld_map_workaround: non-ASCII project path, using relative -Map")
except Exception as err:  # never break a working build
    print("ld_map_workaround: skipped (%s)" % err)
