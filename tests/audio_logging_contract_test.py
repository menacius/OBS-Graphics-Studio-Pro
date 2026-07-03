#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
runtime = (root / "src/obs/title-audio-runtime.cpp").read_text(encoding="utf-8")
preview = (root / "src/editor/title-editor/editor-audio-preview.inc").read_text(encoding="utf-8")
logger = (root / "src/core/title-logger.cpp").read_text(encoding="utf-8")

required_runtime_tags = [
    "[BGL Audio][runtime-create]",
    "[BGL Audio][runtime-destroy]",
    "[BGL Audio][delivery-mode]",
    "[BGL Audio][transport]",
    "[BGL Audio][decode-start]",
    "[BGL Audio][decode-ready]",
    "[BGL Audio][decode-failed]",
    "[BGL Audio][worker-underrun]",
    "[BGL Audio][monitor-hard-resync]",
    "[BGL Audio][monitor-catchup]",
    "[BGL Audio][monitor-clock-reset]",
    "[BGL Audio][delivery-stall]",
    "[BGL Audio][timestamp-gap]",
    "[BGL Audio][flow]",
    "[BGL Audio][worker-idle]",
]
for tag in required_runtime_tags:
    assert tag in runtime, tag

for tag in [
    "[editor-preview-create-request]",
    "[editor-preview-create-failed]",
    "[editor-preview-created]",
    "[editor-preview-seek]",
    "[editor-preview-sync]",
    "[editor-preview-release]",
]:
    assert tag in preview, tag

# Runtime and editor logs must go through the BGL file logger, not only blog().
assert '#include "title-logger.h"' in runtime
assert 'TitleLogger::log(level, "Audio", message);' in runtime
assert "BGL_LOG_INFO(\n        \"Audio\"" in preview
assert "blog(LOG_" not in runtime
assert "blog(LOG_" not in preview
assert '{QStringLiteral("Audio"), QStringLiteral("Audio")' in logger

# Flow logs are throttled; packet-by-packet INFO spam is forbidden.
assert "1000000000ULL" in runtime
assert "log_last_summary_ns_" in runtime

print("audio logging contract passed")
