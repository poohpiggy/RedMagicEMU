#!/usr/bin/env python3
"""Generates res/values*/strings.xml and res/xml/locales_config.xml.

Existing XML is the source of truth for translations that predate the Python
language modules. Modules are overlaid when present, which keeps legacy locale
files intact while allowing new locales to be maintained directly in XML.

Run: python app/l10n/gen.py
Check only: python app/l10n/gen.py --check
"""
import os
import re
import runpy
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "src", "main", "res")
sys.path.insert(0, HERE)
from keys import KEYS  # noqa: E402

DIRS = {
    "en": "values",
    "en-US": "values-en-rUS",
    "en-GB": "values-en-rGB",
    "ru": "values-ru",
    "uk": "values-uk",
    "de": "values-de",
    "fr": "values-fr",
    "es": "values-es",
    "pt-BR": "values-pt-rBR",
    "it": "values-it",
    "pl": "values-pl",
    "tr": "values-tr",
    "ar": "values-ar",
    "fa": "values-fa",
    "hi": "values-hi",
    "id": "values-in",
    "vi": "values-vi",
    "zh-CN": "values-zh-rCN",
    "zh-TW": "values-zh-rTW",
    "zh-HK": "values-zh-rHK",
    "ja": "values-ja",
    "ko": "values-ko",
}
REQUIRED = {"en", "en-US", "en-GB", "zh-CN", "zh-TW", "zh-HK"}
PLACEHOLDER = re.compile(r"%(?:\d+\$)?[sd]|%%")


def android_escape(s: str) -> str:
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    s = s.replace("\\", "\\\\").replace("'", "\\'").replace('"', '\\"').replace("\n", "\\n")
    if s.startswith("@") or s.startswith("?"):
        s = "\\" + s
    return s


def xml_escape(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def load_module(code):
    path = os.path.join(HERE, "langs", code + ".py")
    if not os.path.isfile(path):
        return {}
    return {key: android_escape(value) for key, value in runpy.run_path(path)["S"].items()}


def load_existing(directory):
    path = os.path.join(RES, directory, "strings.xml")
    if not os.path.isfile(path):
        return {}
    return {
        node.attrib["name"]: node.text or ""
        for node in ET.parse(path).getroot().findall("string")
        if node.attrib["name"] != "app_name"
    }


def main():
    check_only = "--check" in sys.argv[1:]
    translations = {}
    base = load_existing("values")
    partial = []
    for code, directory in DIRS.items():
        s = load_existing(directory)
        s.update(load_module(code))
        lack = [k for k in KEYS if k not in s]
        extra = [k for k in s if k not in KEYS]
        if code in REQUIRED and (lack or extra):
            print(f"{code}: missing={lack} extra={extra}")
        elif code not in REQUIRED and lack:
            partial.append(code)
        if code in REQUIRED:
            assert not lack, f"{code}: missing keys {lack}"
            for key in KEYS:
                assert s[key].strip(), f"{code}: empty value for {key}"
                if sorted(PLACEHOLDER.findall(s[key])) != sorted(PLACEHOLDER.findall(base[key])):
                    raise AssertionError(f"{code}: placeholder mismatch in {key}")
        translations[code] = s

    if check_only:
        print(f"ok: {len(REQUIRED)} complete languages, {len(KEYS)} strings; partial: {', '.join(partial) or 'none'}")
        return

    for code, directory in DIRS.items():
        s = translations[code]
        out = ['<?xml version="1.0" encoding="utf-8"?>', "<!-- создано app/l10n/gen.py, правьте langs/*.py -->", "<resources>"]
        if code == "en":
            out.append('    <string name="app_name" translatable="false">AEmulator</string>')
        for k in KEYS:
            if k in s:
                out.append(f'    <string name="{k}">{xml_escape(s[k])}</string>')
        out.append("</resources>\n")
        path = os.path.join(RES, directory)
        os.makedirs(path, exist_ok=True)
        with open(os.path.join(path, "strings.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out))
    tags = [code for code in DIRS if code != "en"]
    xml = ['<?xml version="1.0" encoding="utf-8"?>', '<locale-config xmlns:android="http://schemas.android.com/apk/res/android">']
    xml += [f'    <locale android:name="{t}" />' for t in tags]
    xml.append("</locale-config>\n")
    os.makedirs(os.path.join(RES, "xml"), exist_ok=True)
    with open(os.path.join(RES, "xml", "locales_config.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(xml))
    print(f"готово: {len(DIRS)} языков, {len(KEYS)} строк")


if __name__ == "__main__":
    main()
