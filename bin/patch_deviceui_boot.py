# PlatformIO pre-script: убрать зелёную вспышку при загрузке.
# LGFXDriver::init_lgfx() в device-ui красит весь экран в зелёный Meshtastic
# (0x3DDA83) между включением подсветки и первым кадром LVGL - видна доля
# секунды при каждой загрузке. Править форк нельзя (нет прав на push),
# поэтому патчим копию библиотеки в libdeps перед сборкой. Идемпотентно:
# повторно применяется без изменений, при обновлении библиотеки - заново.
Import("env")
import glob
import os

OLD = "color565(0x3D, 0xDA, 0x83)"
NEW = "color565(0x00, 0x00, 0x00)"

libdeps = env.subst(os.path.join("$PROJECT_LIBDEPS_DIR", "$PIOENV"))
cands = glob.glob(os.path.join(libdeps, "*", "include", "graphics", "driver", "LGFXDriver.h"))
if not cands:
    print("Horizon: WARNING: LGFXDriver.h not found in libdeps, boot fill not patched")
for path in cands:
    with open(path, encoding="utf-8") as f:
        s = f.read()
    if OLD in s:
        s = s.replace(OLD, NEW)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(s)
        print(f"Horizon: patched boot fill to black in {path}")
    else:
        print(f"Horizon: boot fill already patched in {path}")
