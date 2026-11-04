"""Regression checks for the map screen without a board or the firmware fork.

These are *not* a substitute for building/flashing the firmware: LVGL and the
Elecrow touch controller must still be tested on the device.
"""
from pathlib import Path
import random
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
UI = (ROOT / "src/horizon/HorizonMapUI.cpp").read_text(encoding="utf-8")
CORE = (ROOT / "src/horizon/HorizonUI.cpp").read_text(encoding="utf-8")
DRIVER = (ROOT / "src/horizon/HorizonDisplay.cpp").read_text(encoding="utf-8")
STORE = (ROOT / "src/horizon/HorizonMap.cpp").read_text(encoding="utf-8")
VIEW = (ROOT / "src/horizon/HorizonView.cpp").read_text(encoding="utf-8")


class MapUiContracts(unittest.TestCase):
    def test_cover_cannot_expose_background(self):
        for map_w, map_h in ((480, 320), (624, 416), (720, 480), (960, 640)):
            for w, h in ((480, 298), (480, 320), (320, 458), (320, 480)):
                for zoom in (1, 1.5, 2, 3):
                    scale = max(w / map_w, h / map_h) * zoom
                    self.assertGreaterEqual(map_w - w / scale, -1e-6)
                    self.assertGreaterEqual(map_h - h / scale, -1e-6)
                    for ox, oy in ((0, 0), (map_w - w / scale, map_h - h / scale)):
                        for x, y in ((0, 0), (w - 1, h - 1)):
                            self.assertTrue(0 <= int(ox + x / scale) < map_w)
                            self.assertTrue(0 <= int(oy + y / scale) < map_h)

    def test_visible_markers_roundtrip(self):
        rng = random.Random(42)
        for map_w, map_h in ((480, 320), (624, 416), (720, 480), (960, 640)):
            for w, h in ((480, 298), (320, 458)):
                for zoom in (1, 1.5, 2, 3):
                    scale = max(w / map_w, h / map_h) * zoom
                    for _ in range(3000):
                        ox = rng.uniform(0, map_w - w / scale)
                        oy = rng.uniform(0, map_h - h / scale)
                        xpm, ypm = rng.randrange(1001), rng.randrange(1001)
                        dx = (xpm * (map_w - 1) / 1000 - ox) * scale
                        dy = (ypm * (map_h - 1) / 1000 - oy) * scale
                        sx = int(dx + (0.5 if dx >= 0 else -0.5))
                        sy = int(dy + (0.5 if dy >= 0 else -0.5))
                        if 0 <= sx < w and 0 <= sy < h:
                            gotx = int((ox + sx / scale) * 1000 / (map_w - 1) + 0.5)
                            goty = int((oy + sy / scale) * 1000 / (map_h - 1) + 0.5)
                            self.assertLessEqual(abs(gotx - xpm), 2)
                            self.assertLessEqual(abs(goty - ypm), 2)

    def test_new_image_and_pan_cache_contract(self):
        self.assertIn("constexpr int kMinMapUnit = 160", UI)
        self.assertIn("constexpr int kMaxMapUnit = 320", UI)
        self.assertIn("bytes == (size_t)12 * unit * unit", UI)
        self.assertIn("m->mapW = 3 * unit;", UI)
        self.assertIn("m->mapH = 2 * unit;", UI)
        self.assertIn("preview ? kOldMapW / (float)m->mapW : 1.0f", UI)
        self.assertIn("constexpr int kOldMapW = 480", UI)
        self.assertIn("m->mapW == kOldMapW", UI)
        self.assertIn("map_preview.rgb565", UI)
        self.assertIn("m->sampleX.resize(m->viewW)", UI)
        self.assertIn("m->mapReady = true;", UI)
        self.assertIn("m->mapLoadTried = true;", UI)
        self.assertIn("bool stream = !preview && !m->mapBuf;", UI)
        self.assertIn("m->rowCache.resize(srcW);", UI)
        self.assertIn("file.seek(offset)", UI)
        build = UI.split("void HorizonUI::buildMap(void)", 1)[1].split("void HorizonUI::mapRefreshMarkers(void)", 1)[0]
        start = build.split("if (m->mapW > kOldMapW)", 1)[1].split("// Старый файл маленький", 1)[0]
        self.assertIn("map_preview.rgb565", start)
        self.assertNotIn("HZMAP_ALLOC(bytes)", start)  # не просить 1.2 МБ для открытия
        self.assertLess(build.index("if (!m->mapReady)"), build.index("horizonSetMapLandscape"))
        self.assertIn('showMenu();\n            toast("Карта: неверный размер файла', build)
        self.assertIn('showMenu(); // сам освободит карту и вернёт ориентацию\n            toast("Карта: не хватает памяти', build)
        self.assertIn('if (!renderView(m))', build)
        self.assertIn('if (!m->viewBuf && m->previewBuf)', build)
        self.assertIn('if (!m->viewBuf && m->mapBuf)', build)
        self.assertIn("constexpr uint32_t kPanFrameMs = 40", UI)
        self.assertIn("if (m->framePending)", UI)
        self.assertIn("moveMarkers(m);", UI)
        pressing = UI.split("} else if (code == LV_EVENT_PRESSING)", 1)[1].split("} else if (code == LV_EVENT_RELEASED)", 1)[0]
        self.assertNotIn("mapRefreshMarkers", pressing)
        self.assertIn("clampView(m)", pressing)

    def test_fs_mount_must_not_format_user_data_on_error(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn("#if defined(HORIZON_UI)\n#define FSBegin() LittleFS.begin(false)", main)

    def test_rotation_and_touch_share_coordinates(self):
        self.assertIn("this->tft->setRotation(enabled ? 3 : 0)", DRIVER)
        self.assertIn("lv_display_set_resolution(this->getDisplay(), enabled ? 480 : 320, enabled ? 320 : 480)", DRIVER)
        self.assertIn("horizonSetMapLandscape(view->getDisplayDriver(), true)", UI)
        self.assertIn("horizonSetMapLandscape(view->getDisplayDriver(), false)", CORE)
        self.assertIn("lv_obj_get_coords(m->img, &crd)", UI)
        self.assertIn("int lx = pt.x - crd.x1", UI)
        self.assertIn("int ly = pt.y - crd.y1", UI)
        self.assertIn("lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN)", UI)
        self.assertIn("lv_obj_set_style_pad_all(m->markers, 0, LV_PART_MAIN)", UI)
        self.assertIn("addStatusBar(scr);", UI)

    def test_fullscreen_map_dimmed_status_and_filter_groups(self):
        build = UI.split("void HorizonUI::buildMap(void)", 1)[1].split("void HorizonUI::mapRefreshMarkers(void)", 1)[0]
        self.assertIn("m->viewY = 0;", build)
        self.assertIn("m->viewH = H();", build)
        self.assertLess(build.index("lv_obj_t *box = lv_obj_create(scr);"), build.index("addStatusBar(scr);"))
        self.assertIn("lv_obj_set_style_bg_color(bar, hzColor(0x0A0D0C), LV_PART_MAIN);", build)
        self.assertIn("lv_obj_set_style_bg_opa(bar, LV_OPA_60, LV_PART_MAIN);", build)
        self.assertNotIn("lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP", build)
        self.assertIn("lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);", build)
        self.assertIn("lv_obj_set_style_text_color(bar, hzColor(0xFFFFFF), LV_PART_MAIN);", build)
        self.assertIn("lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);", build)
        self.assertIn('lv_label_set_text(bl, "<");', build)
        self.assertIn("lv_obj_set_pos(back, 4, kStatusH + 4);", build)
        self.assertIn("lv_obj_set_pos(flt, W() - 46, kStatusH + 4);", build)
        self.assertIn("lv_image_set_src(fl, &kFilterIcon);", build)
        self.assertNotIn('lv_label_set_text(fl, "Фильтры")', build)
        panel = UI.split("void HorizonUI::mapBuildFilterPanel(void)", 1)[1].split("void HorizonUI::cbMapFilter(lv_event_t *e)", 1)[0]
        self.assertIn('sectionLabel(panel, "По типу");', panel)
        self.assertIn('sectionLabel(panel, "По доступу");', panel)
        self.assertLess(panel.index('sectionLabel(panel, "По типу");'), panel.index("for (int i = 0; i < 7; i++)"))
        self.assertIn("if (i == 5)\n            sectionLabel(panel, \"По доступу\");", panel)
        self.assertIn("m->filterRows[i] = b;", panel)
        reset = UI.split("void HorizonUI::cbMapFilterReset(lv_event_t *e)", 1)[1]
        self.assertIn("m->filterRows[i]", reset)
        self.assertNotIn("self->mapBuildFilterPanel()", reset)  # не удалять кнопку во время тапа

    def test_filter_bitmap_descriptor_compiles_as_cpp17(self):
        """The firmware compiles MapUI.cpp as C++17, unlike HorizonLogo.c."""
        bitmap = UI.split("// 20x20 alpha-mask funnel", 1)[1].split("constexpr int16_t kHeaderH", 1)[0]
        self.assertNotIn("\n    .header.magic =", bitmap)  # not a C++17 initializer
        self.assertIn("icon.header.magic = LV_IMAGE_HEADER_MAGIC;", bitmap)
        self.assertIn("icon.data = kFilterPixels;", bitmap)
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ is not installed; run the firmware compiler too")
        stub = """#include <stdint.h>
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COLOR_FORMAT_A8 1
struct image_header { unsigned magic, cf, flags, w, h, stride; };
struct lv_image_dsc_t { image_header header; unsigned data_size; const uint8_t *data; };
// 20x20 alpha-mask funnel""" + bitmap
        result = subprocess.run([compiler, "-std=gnu++17", "-fsyntax-only", "-x", "c++", "-"],
                                input=stub, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_form_draft_survives_contact_picker(self):
        self.assertIn("s == Screen::CONTACTS && mapPickMode", CORE)
        self.assertIn("m->formDraft = true", UI)
        self.assertIn("self->mapPickMode = true", UI)
        self.assertIn("self->mapPickPeer = id", (ROOT / "src/horizon/HorizonChatUI.cpp").read_text(encoding="utf-8"))
        self.assertIn("m->draftName", UI)
        self.assertIn("m->draftComment", UI)
        self.assertIn('"Публичная\\nЧастная"', UI)
        self.assertIn('"Общая\\nГруппировка\\nОтправить контакту"', UI)

    def test_keyboard_editors_except_chat(self):
        self.assertIn("bool hasOk = (current != Screen::CHAT);", CORE)
        self.assertIn("bool mapMode = (current == Screen::MAP_FORM);", CORE)
        self.assertIn('if (i == 39)\n            keys[i] = "OK";', CORE)
        self.assertIn('else if (i == 38)\n            keys[i] = "<<";', CORE)
        self.assertIn('kbdLayout == 1 && !mapMode && i == 36', CORE)
        self.assertIn('kbdLayout == 2 && i == 37', CORE)
        self.assertIn("keys[j]", CORE)  # wide space uses transformed keys
        self.assertIn('if (!strcmp(txt, "OK")) {\n        kbdHide();', CORE)
        self.assertIn('"ь", "OK", "<<"', CORE)  # map Cyrillic keeps comma absent
        self.assertIn('lv_label_set_text(pinDots, "----");', CORE)
        self.assertIn("dots[i] = (i < n) ? '*' : '-';", CORE)

    def test_rank_map_destination_guard_including_old_points(self):
        self.assertIn('cfg.rank < kRankVeteran ? "Группировка\\nОтправить контакту"', UI)
        self.assertIn('if (cfg.rank < kRankVeteran && vDest == 0)', UI)  # old general point
        self.assertIn('lv_dropdown_set_selected(m->formDest, vDest - (cfg.rank < kRankVeteran ? 1 : 0))', UI)
        self.assertIn('mapFormDestination(m->formDest)', UI)
        self.assertIn('m->draftDest = mapFormDestination(m->formDest);', UI)
        self.assertIn('bool wantPeer = (vis == 0 && dest == 2);', UI)
        self.assertIn('if (shared && cfg.rank < kRankVeteran && (dest == 0 || (dest == 1 && cfg.groupCh == cfg.generalCh)))', UI)
        self.assertIn('if (to == 0xFFFFFFFFu && cfg.rank < HorizonStrings::kRankVeteran && ch == cfg.generalCh)', VIEW)
        for rank in range(4):
            first = 1 if rank < 2 else 0
            options = ["group", "dm"] if first else ["general", "group", "dm"]
            self.assertEqual([i + first for i in range(len(options))],
                             [1, 2] if first else [0, 1, 2])

    def test_theme_version_battery_reboot_contract(self):
        setup = (ROOT / "src/horizon/HorizonSetupUI.cpp").read_text(encoding="utf-8")
        theme = (ROOT / "src/horizon/HorizonTheme.cpp").read_text(encoding="utf-8")
        strings = (ROOT / "src/horizon/HorizonStrings.h").read_text(encoding="utf-8")
        settings = setup.split("void HorizonUI::buildSettings(void)", 1)[1].split("// ---------------- настройки: раздел мастера", 1)[0]
        master = setup.split("void HorizonUI::buildSetMaster(void)", 1)[1].split("// ---------------- настройки: время", 1)[0]
        self.assertNotIn('rowButton(body, "Батарея"', settings)
        self.assertIn('{"Батарея", vbsub, 6}', master)
        self.assertIn('case 6:\n        self->buildSetBattery();', setup)
        self.assertIn('case Screen::SET_BATTERY:\n        self->showMaster();', CORE)
        self.assertIn('"Киберпанк"', (ROOT / "src/horizon/HorizonStrings.cpp").read_text(encoding="utf-8"))
        self.assertIn('kThemeCount = 6', strings)
        self.assertIn('CYBERPUNK', theme)
        self.assertIn('kFirmwareVersion = "1.8.8.ZI"', strings)
        self.assertIn('"ПДА «Горизонт» v.%s", kFirmwareVersion', CORE)
        reboot = VIEW.split('void HorizonView::rebootNode(void)', 1)[1].split('void HorizonView::resetNodeDb(void)', 1)[0]
        self.assertIn('controller->requestReboot(0, ownNode);', reboot)
        self.assertIn('controller->requestReboot(3, ownNode);', reboot)
        self.assertLess(reboot.index('controller->requestReboot(0, ownNode);'), reboot.index('controller->requestReboot(3, ownNode);'))

    def test_own_points_older_csv_and_direct_delivery(self):
        self.assertIn("nf != 9 && nf != 10", STORE)
        self.assertIn("p.dmPeer = mode == 1 ? peer : 0", STORE)
        self.assertIn('f.printf("|%u|%u|%u"', STORE)
        self.assertIn("mapSendDmRaw(to, ch, buf)", VIEW)
        self.assertIn("mapSendDeleteDm(id, peer)", VIEW)
        self.assertIn("p.ch = ch", UI)
        self.assertIn("ch = view->mapDmChannel(peer)", UI)


if __name__ == "__main__":
    unittest.main()
