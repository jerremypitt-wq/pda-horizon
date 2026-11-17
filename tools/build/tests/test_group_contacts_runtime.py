"""Compile and exercise contact grouping evidence without firmware/LVGL."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class GroupContactRuntime(unittest.TestCase):
    def test_group_contact_evidence(self):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ not installed")
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "group_contacts"
            subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-I" + str(ROOT / "src"), str(ROOT / "tools/tests/test_group_contacts.cpp"),
                 "-o", str(exe)],
                check=True,
            )
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
