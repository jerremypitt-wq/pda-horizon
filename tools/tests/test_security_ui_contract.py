"""Static integration checks for the settings/lock flow (no LVGL toolchain needed)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
setup = (ROOT / "src/horizon/HorizonSetupUI.cpp").read_text()
ui = (ROOT / "src/horizon/HorizonUI.cpp").read_text()
chat = (ROOT / "src/horizon/HorizonChatUI.cpp").read_text()
theme = (ROOT / "src/horizon/HorizonTheme.cpp").read_text()
map_ui = (ROOT / "src/horizon/HorizonMapUI.cpp").read_text()
config = (ROOT / "src/horizon/HorizonConfig.cpp").read_text()

settings = setup.split("void HorizonUI::buildSettings(void)", 1)[1].split(
    "// ---------------- настройки: раздел мастера", 1
)[0]
assert settings.index('"Звук"') < settings.index('"Инструменты Mesh"') < settings.index('"О ПДА"')
master = setup.split("void HorizonUI::buildSetMaster(void)", 1)[1].split(
    "// ---------------- настройки: время", 1
)[0]
assert '"Инструменты Mesh"' not in master
assert "void HorizonUI::buildSetSecurity(void)" in setup
assert "void HorizonUI::requestUserPin(void)" in setup
assert "cfg.lockEnabled = true;" in setup
assert "c.lockEnabled = !c.lockEnabled;" in setup
assert '"Заблокировать сейчас"' in setup and "self->showLock();" in setup
assert "cfg.setPin(next.c_str())" in setup
assert "lv_textarea" not in setup.split("void HorizonUI::buildSetPin(void)", 1)[1].split(
    "void HorizonUI::buildSetSecurity(void)", 1
)[0]
assert 'askPin("Введите PIN:"' in ui
assert "if (current != Screen::LOCK)" in ui  # cancel button hidden on lock
assert "hzShouldLock(cfg.lockEnabled, cfg.lockTimeoutSec, inact, pressed)" in ui
assert "lastTouchMs = now" in ui
assert "if (current == Screen::LOCK)" in map_ui and "if (current == Screen::LOCK)" in ui
assert '"userPin=%s' in config and '"lockTimeoutSec=%u' in config
assert "outgoing && !cyber ? p.accentText : p.text" in chat
assert ".outgoing = 0x031704" in theme and ".danger = 0xFF2020" in theme
assert "styleDangerButton" in theme and "lv_color_black(), LV_PART_MAIN" in theme
assert 'kFirmwareVersion = "1.13.0.ZI"' in (ROOT / "src/horizon/HorizonStrings.h").read_text()
print("Security/settings/theme integration: PASS")
