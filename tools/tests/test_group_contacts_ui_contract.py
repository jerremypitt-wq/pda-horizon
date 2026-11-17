"""Host-only contract for the contacts grouping tab (not a device test)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ui = (ROOT / "src/horizon/HorizonChatUI.cpp").read_text(encoding="utf-8")
view = (ROOT / "src/horizon/HorizonView.cpp").read_text(encoding="utf-8")

filters = ui.split("void HorizonUI::buildContacts(void)", 1)[1].split("void HorizonUI::rebuildContactsList", 1)[0]
assert filters.index('lv_label_set_text(fol, "В сети")') < filters.index('lv_label_set_text(fgl, "Группировка")')
assert filters.index('lv_label_set_text(fgl, "Группировка")') < filters.index('"Избранные (%u)"')
assert "contactsFilter == 2 && !view->isGroupContact(kv.first)" in ui
assert "contactsFilter == 3 && !fav" in ui
assert "LV_FLEX_FLOW_ROW_WRAP" in filters and "filterH = W() < 400 ? 70 : 36" in filters
assert "p.which_payload_variant == meshtastic_MeshPacket_decoded_tag" in view
assert "groupContacts.observe(p.from, (uint8_t)p.channel" in view
assert "groupContacts.remove(nodeNum)" in view
assert "groupContacts.clear();" in view
print("Contacts grouping UI integration: PASS")
