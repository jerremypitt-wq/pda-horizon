"""Contract for the contact screen, clocks and non-destructive expiry."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
ui = (root / "src/horizon/HorizonChatUI.cpp").read_text(encoding="utf-8")
core = (root / "src/horizon/HorizonUI.cpp").read_text(encoding="utf-8")
view = (root / "src/horizon/HorizonView.cpp").read_text(encoding="utf-8")
contacts = (root / "src/horizon/HorizonContacts.cpp").read_text(encoding="utf-8")

assert 'for (const auto &kv : view->getContacts().records())' in ui
assert 'nr.user.hw_model == meshtastic_HardwareModel_UNSET' not in ui
assert 'fmtContactStatus(view, r.id, sub, sizeof(sub))' in ui
assert 'isNodeHeardWithin(kv.first, kContactFreshSec)' not in ui
assert 'contactsRefreshPending || now - lastContactsMs >= 10000' in core
assert 'ref - lh > HzContacts::kKeepSec' in core
assert 'неизвестно' in core
assert 'contacts.load()' in view and 'contacts.save()' in view
assert 'contacts.heard(p.from,' in view
assert 'seconds > kKeepSec' in contacts
assert 'entries.erase(it)' in contacts
assert 'clearAllDms' not in contacts and 'clearConvo' not in contacts
print('Contacts UI integration: PASS')
