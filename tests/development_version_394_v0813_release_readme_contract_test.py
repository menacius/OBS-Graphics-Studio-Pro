#!/usr/bin/env python3
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
plugin = read("src/obs/plugin-main.h")
readme = read("README.md")
docs_index = read("docs/README.md")
changelog = read("docs/CHANGELOG.md")
install = read("INSTALL.txt")
vcpkg = json.loads(read("vcpkg.json"))
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert "project(broadcast-graphics-live VERSION 0.8.13)" in cmake
assert 'set(OBS_BGS_PRERELEASE "alpha")' in cmake
assert 'set(OBS_BGS_DEVELOPMENT_VERSION "394")' in cmake
assert '#define PLUGIN_VERSION "0.8.13-alpha"' in build
assert '#define BGL_DEVELOPMENT_VERSION "394"' in build
assert '#define PLUGIN_VERSION "0.8.13-alpha"' in plugin
assert vcpkg["version-string"] == "0.8.13-alpha"

assert "`v0.8.13-alpha` · `Development Version 394`" in readme
assert "Highlights since Development Version 281" in readme
for feature in (
    "Trim Paths",
    "Motion Blur and temporal rendering",
    "3D lighting, materials and shadows",
    "Assets and document import",
    "Live cueing and preview",
):
    assert feature in readme

assert "`v0.8.13-alpha` Development Version 394" in docs_index
assert changelog.startswith("# v0.8.13-alpha — Development Version 394")
assert "Broadcast_Graphics_Live_v0.8.13-alpha_development-version-394_windows-x64.zip" in install
assert manifest["development_version"] == 394

current_test = "tests/development_version_394_v0813_release_readme_contract_test.py"
found = False

def visit(value):
    global found
    if isinstance(value, list):
        if current_test in value:
            found = True
        for item in value:
            visit(item)
    elif isinstance(value, dict):
        for item in value.values():
            visit(item)

visit(manifest)
assert found
print("Development Version 394 v0.8.13-alpha release/README contract: PASS")
