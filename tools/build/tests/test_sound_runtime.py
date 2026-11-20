"""Host checks of sound persistence, playback and both Arduino-ESP32 LEDC APIs."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class SoundRuntime(unittest.TestCase):
    def test_pwm_and_settings_arduino_2_and_3(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ not installed")
        with tempfile.TemporaryDirectory() as tmp:
            for major in (2, 3):
                with self.subTest(arduino_esp32_major=major):
                    exe = Path(tmp) / f"sound_{major}"
                    subprocess.run([
                        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-DARDUINO_ARCH_ESP32", f"-DTEST_ARDUINO_MAJOR={major}",
                        "-I" + str(ROOT / "src"),
                        "-I" + str(ROOT / "tools/build/tests/sound_stubs"),
                        "-I" + str(ROOT / "tools/tests/stubs"),
                        *[str(ROOT / f"src/horizon/{name}.cpp") for name in
                          ("HorizonConfig", "HorizonStrings", "HorizonSound")],
                        str(ROOT / "tools/build/tests/test_sound_runtime.cpp"),
                        "-o", str(exe),
                    ], check=True)
                    subprocess.run([str(exe)], check=True)

    def test_sound_ui_dropdown_contract(self):
        setup = (ROOT / "src/horizon/HorizonSetupUI.cpp").read_text(encoding="utf-8")
        ui = setup.split("void HorizonUI::buildSetSound(void)", 1)[1].split(
            "// ---------------- настройки: каналы", 1)[0]
        self.assertIn('lv_dropdown_set_options(volume, "Тихо\\nГромко")', ui)
        self.assertIn('lv_dropdown_set_options(ringtone, "Тройной\\nДвойной\\nОдиночный")', ui)
        self.assertIn('cbSoundVolume, LV_EVENT_VALUE_CHANGED', ui)
        self.assertIn('cbSoundSet, LV_EVENT_VALUE_CHANGED', ui)
        self.assertIn('lv_dropdown_get_selected(', ui)
        self.assertNotIn('rowButton(body, title', ui)
        self.assertIn('HorizonSound::instance().stop()', ui)


if __name__ == "__main__":
    unittest.main()
