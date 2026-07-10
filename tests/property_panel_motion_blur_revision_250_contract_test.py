#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        raise AssertionError(f"unexpected {label}: {needle}")


def main() -> None:
    build = read("src/core/build-info.h")
    cmake = read("CMakeLists.txt")
    popup = read("src/editor/properties-panel/popup-state.inc")
    live = read("src/editor/properties-panel/construction-gradient-image-signals.inc")
    sync = read("src/editor/properties-panel/property-synchronization.inc")
    selection = read("src/editor/properties-panel/selection-refresh.inc")
    motion = read("src/obs/title-source/gpu-resources-primitives.inc")
    shader = read("data/effect-transitions/shaders/motion-blur/motion-blur.effect")
    readme = read("README.md")

    if not any(v in build for v in [f'BGL_DEVELOPMENT_VERSION "{v}"' for v in range(250, 300)]):
        raise AssertionError("missing build development version 250/251")
    if not any(f'OBS_BGS_DEVELOPMENT_VERSION "{v}"' in cmake for v in range(250, 300)):
        raise AssertionError("missing cmake development version >=250")

    require(live, 'QGroupBox(QStringLiteral("Live Properties")', "single live properties panel")
    require(live, 'add_form_row(live_edit_form, bgl_tr("OBSTitles.AssetPlayback"), cmb_video_playback_)',
            "asset playback row moved into live properties")
    require(popup, 'live_playback_box_ = nullptr;', "no standalone live playback panel")
    forbid(popup, 'bgl_add_panel_section(vl, live_playback_box_)', "standalone live playback panel wrapper")
    require(sync, 'show_video_live_playback', "video live playback row visibility")
    require(selection, 'show_video_live_playback || show_ignore_persistence', "live panel visibility includes video playback")

    require(selection, 'QStringLiteral("Video Source")', "video source panel title")
    require(selection, 'QStringLiteral("Image Source")', "image source panel title")
    require(selection, 'Box size now lives inside Image Source/Video Source', "box size embedded in source panel")
    require(selection, 'QStringLiteral("Video Box Mode")', "video box mode row label")
    require(selection, 'QStringLiteral("Replace Video")', "video-specific source button")
    require(popup, 'QGroupBox(QStringLiteral("Video Timing & Loading")', "video timing/loading panel title")

    require(motion, 'motion blur must never reduce the', "temporal motion blur alpha-preservation comment")
    require(motion, 'const double sharp_mix = 1.0;', "temporal motion blur keeps sharp layer full opacity")
    require(motion, 'const double blur_mix = mix;', "temporal motion blur trail strength")
    require(shader, 'result.a = max(center.a, result.a);', "shader motion blur preserves center alpha")
    require(shader, 'result.rgb = min(result.rgb, result.a.xxx);', "shader motion blur clamps premultiplied output")

    require(readme, 'Development Version 250', "README version note")
    require(readme, 'all live/runtime controls are consolidated into one Live Properties panel',
            "README live properties summary")

    print("revision 250 property panel organization and motion blur contract OK")


if __name__ == "__main__":
    main()
