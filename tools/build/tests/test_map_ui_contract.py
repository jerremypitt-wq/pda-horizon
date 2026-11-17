"""Regression checks for the map screen without a board or the firmware fork.

These are *not* a substitute for building/flashing the firmware: LVGL and the
Elecrow touch controller must still be tested on the device.
"""
from pathlib import Path
import random
import re
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
        self.assertIn("mapFilePath(path, sizeof(path), m->tab, true)", UI)
        self.assertIn("m->sampleX.resize(m->viewW)", UI)
        self.assertIn("m->mapReady = true;", UI)
        self.assertIn("m->mapLoadTried = true;", UI)
        self.assertIn("bool stream = !preview && !m->mapBuf;", UI)
        self.assertIn("m->rowCache.resize(srcW);", UI)
        self.assertIn("file.seek(offset)", UI)
        build = UI.split("void HorizonUI::buildMap(void)", 1)[1].split("void HorizonUI::mapRefreshMarkers(void)", 1)[0]
        start = build.split("if (m->mapW > kOldMapW)", 1)[1].split("// Старый файл маленький", 1)[0]
        self.assertIn("mapFilePath(path, sizeof(path), m->tab, true)", start)
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

    def test_compact_floor_button_replaced_by_selected_tab_list(self):
        build = UI.split("void HorizonUI::buildMap(void)", 1)[1].split("void HorizonUI::mapRefreshMarkers(void)", 1)[0]
        tabs = UI.split("void HorizonUI::cbMapTabs(lv_event_t *e)", 1)[1].split("void HorizonUI::cbMapTabSelect", 1)[0]
        select = UI.split("void HorizonUI::cbMapTabSelect(lv_event_t *e)", 1)[1].split("void HorizonUI::cbMapZoom", 1)[0]
        self.assertIn('lv_obj_set_size(m->tabBtn, 44, 38);', build)
        self.assertIn('lv_label_set_text(arrow, "^");', build)
        self.assertNotIn('lv_label_set_text(title, t.name.c_str());', build)
        self.assertIn('lv_obj_align(panel, LV_ALIGN_BOTTOM_LEFT, 4, -4)', tabs)
        self.assertIn('lv_obj_add_flag(m->tabBtn, LV_OBJ_FLAG_HIDDEN);', tabs)
        self.assertIn('HorizonTheme::styleButton(btn, tab.id == m->tab);', tabs)
        self.assertIn('lv_obj_set_style_text_font(label, HorizonTheme::fontSmall(), LV_PART_MAIN);', tabs)
        self.assertIn('lv_obj_add_flag(m->tabPanel, LV_OBJ_FLAG_HIDDEN);', select)
        self.assertIn('lv_obj_remove_flag(m->tabBtn, LV_OBJ_FLAG_HIDDEN);', select)
        self.assertLess(select.index('if (id == m->tab)'), select.index('self->showMap();'))

    def test_floor_tabs_point_links_and_radio_contract(self):
        self.assertIn('if (p.tab != m->tab ||', UI)
        self.assertIn('if (st.tabs().size() > 1)', UI)
        self.assertIn('LV_ALIGN_BOTTOM_LEFT, 4, -4', UI)
        self.assertIn('void HorizonUI::cbMapTabSelect', UI)
        self.assertIn('if (!st.hasTab(point->tab))', UI)
        self.assertLess(UI.index('if (!st.hasTab(point->tab))'), UI.index('strncpy(localId, point->id'))
        self.assertIn('mapUi->tab = point->tab;', UI)
        self.assertIn('m->pulseStartedMs = nowMs();', UI)
        self.assertIn('constexpr const char *kPoi2Prefix = "HZ:POI2:"', VIEW)
        self.assertIn('if (!mapStore.hasTab(tab)) return;', VIEW)
        self.assertIn('nr.head.tab = tab;', VIEW)
        self.assertIn('p.tab = m->tab;', UI)
        self.assertIn('p.tab = (uint8_t)tab;', STORE)
        self.assertIn('if (tabs_.empty()) tabs_.push_back({0, "Карта"});', STORE)

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

    def test_sleep_touch_is_filtered_before_lvgl_wakeup(self):
        self.assertIn('lv_indev_set_read_cb(this->touch, readWakeTouch);', DRIVER)
        self.assertIn('count = self->tft->getTouch(points, 5)', DRIVER)
        self.assertIn('self->powerSaving || (self->screenTimeout && inactive >= self->screenTimeout)', DRIVER)
        self.assertLess(DRIVER.index('data->state = LV_INDEV_STATE_RELEASED;\n            data->point = self->lastTouch;\n            return;'),
                        DRIVER.index('data->state = count ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;'))
        self.assertIn('self->wakeGate.sample', DRIVER)
        self.assertIn('self->forceWakeup();', DRIVER)
        self.assertIn('if (count == 0) {\n                self->wakeSession = false;', DRIVER)

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
        bitmap = UI.split("// 20x20 A8: ровный треугольник вниз", 1)[1].split("constexpr int16_t kHeaderH", 1)[0]
        self.assertNotIn("\n    .header.magic =", bitmap)  # not a C++17 initializer
        self.assertIn("icon.header.magic = LV_IMAGE_HEADER_MAGIC;", bitmap)
        self.assertIn("icon.data = kFilterPixels;", bitmap)
        pixels = UI.split("kFilterPixels[] = {", 1)[1].split("};", 1)[0]
        values = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", pixels)]
        self.assertEqual(len(values), 20 * 20)
        rows = [values[i:i + 20] for i in range(0, len(values), 20)]
        self.assertTrue(all(row == row[::-1] for row in rows))  # ровно по центру
        widths = [sum(v > 0 for v in row) for row in rows]
        self.assertEqual(widths[:4], [0] * 4)
        self.assertEqual(widths[17:], [0] * 3)
        self.assertEqual(widths[4], 16)
        self.assertEqual(widths[16], 2)
        self.assertTrue(all(a >= b for a, b in zip(widths[4:16], widths[5:17])))
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ is not installed; run the firmware compiler too")
        stub = """#include <stdint.h>
#define LV_ATTRIBUTE_MEM_ALIGN
#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COLOR_FORMAT_A8 1
struct image_header { unsigned magic, cf, flags, w, h, stride; };
struct lv_image_dsc_t { image_header header; unsigned data_size; const uint8_t *data; };
// 20x20 A8: ровный треугольник вниз""" + bitmap
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
        self.assertIn('"Публичная\\nЧастная\\nПоделиться"', UI)
        self.assertIn('"Общая\\nГруппировка"', UI)

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
        self.assertIn('cfg.rank < kRankVeteran ? "Группировка"', UI)
        self.assertIn('if (cfg.rank < kRankVeteran && vDest == 0)', UI)  # old general point
        self.assertIn('lv_dropdown_set_selected(m->formDest, vDest - (cfg.rank < kRankVeteran ? 1 : 0))', UI)
        self.assertIn('mapFormDestination(m->formDest)', UI)
        self.assertIn('m->draftDest = mapFormDestination(m->formDest);', UI)
        self.assertIn('bool wantPeer = (vis == 2);', UI)
        self.assertIn('bool wantDest = (vis == 0);', UI)
        self.assertIn('if (vis == 0 && cfg.rank < kRankVeteran && (dest == 0 || cfg.groupCh == cfg.generalCh))', UI)
        self.assertIn('if (to == 0xFFFFFFFFu && cfg.rank < HorizonStrings::kRankVeteran && ch == cfg.generalCh)', VIEW)
        for rank in range(4):
            first = 1 if rank < 2 else 0
            options = ["group"] if first else ["general", "group"]
            self.assertEqual([i + first for i in range(len(options))],
                             [1] if first else [0, 1])

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
        cyber = theme.split('// CYBERPUNK', 1)[1].split('},', 1)[0]
        for field in ('bg', 'panel', 'panel2', 'headerBg', 'tileEnd'):
            self.assertIn(f'.{field} = 0x000000', cyber)
        for field in ('accent', 'text', 'ok'):
            self.assertIn(f'.{field} = 0x09B80F', cyber)
        self.assertIn('.accentText = 0x000000', cyber)
        version = re.search(r'kFirmwareVersion = "(\d+\.\d+\.\d+\.ZI)"', strings)
        self.assertIsNotNone(version)
        self.assertIn(f'Версия исходников Horizon: `{version.group(1)}`',
                      (ROOT / 'docs/HORIZON.md').read_text(encoding='utf-8'))
        self.assertIn('"ПДА «Горизонт» v.%s", kFirmwareVersion', CORE)
        reboot = VIEW.split('void HorizonView::rebootNode(void)', 1)[1].split('void HorizonView::resetNodeDb(void)', 1)[0]
        self.assertIn('controller->requestReboot(0, ownNode);', reboot)
        self.assertIn('controller->requestReboot(3, ownNode);', reboot)
        self.assertLess(reboot.index('controller->requestReboot(0, ownNode);'), reboot.index('controller->requestReboot(3, ownNode);'))

    def test_personal_copy_link_and_ripple_contract(self):
        chat = (ROOT / "src/horizon/HorizonChatUI.cpp").read_text(encoding="utf-8")
        header = (ROOT / "src/horizon/HorizonMap.h").read_text(encoding="utf-8")
        self.assertIn('char sourceId[28]', header)
        self.assertIn('kHzPoiLinkPrefix = "HZ:MAPLINK:"', header)
        self.assertIn('kDirectDelPath = "/hzmap/dm_del.csv"', STORE)
        self.assertIn('nf < 9 || nf > 12', STORE)
        self.assertIn('saveDirectDismissed();', STORE)
        self.assertIn('findBySource', STORE)
        self.assertIn('const bool direct = ownNode && to == ownNode;', VIEW)
        self.assertIn('mapStore.findBySource(id) || mapStore.directDismissed(id)', VIEW)
        self.assertIn('mapAllocId(poi.id, sizeof(poi.id))', VIEW)
        self.assertIn('poi.shared = false;', VIEW)
        self.assertIn('mapStore.saveMy();', VIEW)
        self.assertIn('if (imported || !shared)', VIEW)
        self.assertIn('sendDirectMessage(peer, link);', VIEW)
        self.assertIn('if (!mapStore.find(id) || mapStore.isMine(id))', VIEW)
        self.assertIn('!msg || to != 0xFFFFFFFFu', VIEW)
        self.assertIn('if (!imported)\n            e->sender = sender;', UI)
        self.assertIn('bool wantPeer = (vis == 2);', UI)
        self.assertIn('bool wantDest = (vis == 0);', UI)
        self.assertIn('cbMapChatLink', chat)
        self.assertIn('lv_label_set_text_fmt(body, "Точка: %s"', chat)
        self.assertNotIn('LV_TEXT_DECOR_UNDERLINE', chat)
        self.assertIn('lv_obj_set_style_text_color(body, hzColor(outgoing ? p.text : p.accent)', chat)
        self.assertIn('self->mapOpenPoint(copy);', chat)
        self.assertIn('onMapPoint(poi.id, poi.name.c_str());', VIEW)
        self.assertIn('self->mapOpenPoint(mapId);', CORE)
        self.assertIn("st.directDismissed(id) || id[0] == 'B'", UI)
        self.assertIn('hzPoiAuthorNode(id) == view->getMyNodeNum()', UI)
        self.assertIn('uint32_t phase = elapsed % 500;', UI)
        self.assertIn('elapsed >= 2500', UI)
        self.assertIn('mapPulseTick(now);', CORE)

    def test_explicit_point_reset_leaves_original_and_chats_alone(self):
        setup = (ROOT / "src/horizon/HorizonSetupUI.cpp").read_text(encoding="utf-8")
        reset = setup.split('void HorizonUI::buildSetReset(void)', 1)[1].split('// ---------------- настройки: о ПДА', 1)[0]
        self.assertIn('{"Сбросить точки", "Оставить только исходные точки карты"}', reset)
        self.assertIn('for (int i = 0; i < 5; i++)', reset)
        self.assertIn('self->askConfirm("Удалить все созданные и принятые точки', reset)
        self.assertIn('self->view->mapResetPoints()', reset)
        self.assertIn("self->pendingMapId[0] = '\\0';", reset)
        self.assertIn('bool HorizonView::mapResetPoints(void)', VIEW)
        self.assertIn('poiRx_.clear();', VIEW)
        store_reset = STORE.split('bool HzMapStore::resetToBaked(void)', 1)[1].split('void HzMapStore::saveMy(void)', 1)[0]
        self.assertIn('kMyPath, kNetPath, kDelPath, kDirectDelPath', store_reset)
        self.assertIn('mapFs.open(kBakedPath, "r")', store_reset)
        self.assertIn('loadAll();', store_reset)
        self.assertNotIn('kBakedPath, "w"', store_reset)
        self.assertNotIn('sendChannelRaw', store_reset)

    def test_own_points_older_csv_and_direct_delivery(self):
        self.assertIn("nf < 9 || nf > 12", STORE)
        self.assertIn("p.dmPeer = mode == 1 ? peer : 0", STORE)
        self.assertIn('f.printf("|%u|%u|%u|%s|%u"', STORE)
        self.assertIn("mapSendDmRaw(to, ch, buf)", VIEW)
        self.assertIn("mapSendDeleteDm(id, peer)", VIEW)
        self.assertIn("p.ch = ch", UI)
        self.assertIn("ch = view->mapDmChannel(peer)", UI)


if __name__ == "__main__":
    unittest.main()
