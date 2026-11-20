"""Check that the current firmware version is documented (not that Git dates are releases)."""
from datetime import datetime
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
strings = (ROOT / "src/horizon/HorizonStrings.h").read_text(encoding="utf-8")
match = re.search(r'kFirmwareVersion\s*=\s*"(\d+\.\d+\.\d+(?:\.[A-Za-z0-9]+)?)"', strings)
assert match, "Firmware version constant missing"
current = match.group(1)
changelog = (ROOT / "docs/FIRMWARE_CHANGELOG.md").read_text(encoding="utf-8")
entries = re.findall(r"^## (\d+\.\d+\.\d+(?:\.[A-Za-z0-9]+)?) — (\d{2}\.\d{2}\.\d{4})\s*$"
                     r"(.*?)(?=^## |\Z)", changelog, re.M | re.S)
assert entries, "No dated firmware entries"
assert entries[0][0] == current, "Newest changelog entry does not match firmware version"
assert f"**Текущая версия исходников: {current}.**" in changelog
assert len({version for version, _, _ in entries}) == len(entries), "Duplicate firmware version"
assert entries[-1][0] == "1.0.0", "Historical baseline missing"
for version, date, body in entries:
    datetime.strptime(date, "%d.%m.%Y")
    assert re.search(r"^- .+", body, re.M), f"Missing change list for {version}"
    assert re.fullmatch(r"\d+\.\d+\.\d+(?:\.[A-Za-z0-9]+)?", version)
assert f"Версия исходников Horizon: `{current}`" in (
    ROOT / "docs/HORIZON.md").read_text(encoding="utf-8")
assert "docs/FIRMWARE_CHANGELOG.md" in (ROOT / "README.md").read_text(encoding="utf-8")
print(f"Firmware changelog: PASS ({len(entries)} versions, latest {current})")
