"""Regression checks for the map screen without a board or the firmware fork.

These are *not* a substitute for building/flashing the firmware: LVGL and the
Elecrow touch controller must still be tested on the device.
"""
from pathlib import Path
import random
import unittest

ROOT = Path(__file__).resolve().parents[3]
UI = (ROOT / "src/horizon/HorizonMapUI.cpp").read_text(encoding="utf-8")
CORE = (ROOT / "src/horizon/HorizonUI.cpp").read_text(encoding="utf-8")
DRIVER = (ROOT / "src/horizon/HorizonDisplay.cpp").read_text(encoding="utf-8")
STORE = (ROOT / "src/horizon/HorizonMap.cpp").read_text(encoding="utf-8")
VIEW = (ROOT / "src/horizon/HorizonView.cpp").read_text(encoding="utf-8")


class MapUiContracts(unittest.TestCase):
    def test_cover_cannot_expose_background(self):
        for w, h in ((480, 298), (480, 320), (320, 458), (320, 480)):
            for zoom in (1, 1.5, 2, 3):
                scale = max(w / 480, h / 320) * zoom
                self.assertGreaterEqual(480 - w / scale, -1e-6)
                self.assertGreaterEqual(320 - h / scale, -1e-6)
                for ox, oy in ((0, 0), (480 - w / scale, 320 - h / scale)):
                    for x, y in ((0, 0), (w - 1, h - 1)):
                        self.assertTrue(0 <= int(ox + x / scale) < 480)
                        self.assertTrue(0 <= int(oy + y / scale) < 320)

    def test_visible_markers_roundtrip(self):
        rng = random.Random(42)
        for w, h in ((480, 298), (320, 458)):
            for zoom in (1, 1.5, 2, 3):
                scale = max(w / 480, h / 320) * zoom
                for _ in range(3000):
                    ox = rng.uniform(0, 480 - w / scale)
                    oy = rng.uniform(0, 320 - h / scale)
                    xpm, ypm = rng.randrange(1001), rng.randrange(1001)
                    dx = (xpm * 479 / 1000 - ox) * scale
                    dy = (ypm * 319 / 1000 - oy) * scale
                    sx = int(dx + (0.5 if dx >= 0 else -0.5))
                    sy = int(dy + (0.5 if dy >= 0 else -0.5))
                    if 0 <= sx < w and 0 <= sy < h:
                        gotx = int((ox + sx / scale) * 1000 / 479 + 0.5)
                        goty = int((oy + sy / scale) * 1000 / 319 + 0.5)
                        self.assertLessEqual(abs(gotx - xpm), 2)
                        self.assertLessEqual(abs(goty - ypm), 2)

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

    def test_form_draft_survives_contact_picker(self):
        self.assertIn("s == Screen::CONTACTS && mapPickMode", CORE)
        self.assertIn("m->formDraft = true", UI)
        self.assertIn("self->mapPickMode = true", UI)
        self.assertIn("self->mapPickPeer = id", (ROOT / "src/horizon/HorizonChatUI.cpp").read_text(encoding="utf-8"))
        self.assertIn("m->draftName", UI)
        self.assertIn("m->draftComment", UI)
        self.assertIn('"Публичная\\nЧастная"', UI)
        self.assertIn('"Общая\\nГруппировка\\nОтправить контакту"', UI)

    def test_keyboard_only_map_mode(self):
        self.assertIn("current == Screen::MAP_FORM", CORE)
        self.assertIn('"ь", "OK", "<<"', CORE)
        self.assertIn('if (!strcmp(txt, "OK"))', CORE)
        self.assertIn('kbdHide();', CORE)
        self.assertIn('kbdLayout == 0 && i == 37', CORE)
        self.assertIn('kbdLayout == 2 && i == 38', CORE)

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
