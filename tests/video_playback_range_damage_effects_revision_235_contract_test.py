from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
schema = read('src/core/title-serialization-schema.h')
manifest = read('tests/test-suite-manifest.json')
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')

assert 'OBS_BGS_DEVELOPMENT_VERSION "243"' in cmake
assert 'BGL_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema and 'case 237:' in schema
assert '"development_version": 243' in manifest
assert 'Development Version 243' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')

video_runtime = read('src/obs/title-video-runtime.cpp')
assert 'target + 0.0005 < last_pts' in video_runtime
assert 'const bool moving_backward' in video_runtime
assert 'const int max_packets = 320' in video_runtime
assert 'look-ahead cache keeps normal forward playback' in video_runtime

props_header = read('src/editor/properties-panel.h')
props_sync = read('src/editor/properties-panel/property-synchronization.inc')
commands = read('src/editor/title-editor/commands-docks.inc')
assert 'void refresh_media_range_controls();' in props_header
assert 'void PropertiesPanel::refresh_media_range_controls()' in props_sync
assert 'props_->refresh_media_range_controls();' in commands
assert 'if (commit_undo)\n                            props_->set_layer(selected, playhead_);\n                        else\n                            props_->refresh_media_range_controls();' in commands
assert 'else\n                        layers_->update();' in commands

enum = read('src/effects/layer-effects.h')
for item in ['Grain = 45', 'FilmDistortion = 46', 'AnalogDistortion = 47', 'DigitalDistortion = 48']:
    assert item in enum

runtime = read('src/effects/effect-runtime.cpp')
for stable_id in ['bgl.builtin.grain', 'bgl.builtin.film-distortion', 'bgl.builtin.analog-distortion', 'bgl.builtin.digital-distortion']:
    assert stable_id in runtime
assert 'Noise and Grain' in runtime

preset_catalog = read('src/effects/effect-preset-catalog.cpp')
for type_id in ['QStringLiteral("grain")', 'QStringLiteral("film-distortion")', 'QStringLiteral("analog-distortion")', 'QStringLiteral("digital-distortion")']:
    assert type_id in preset_catalog
assert 'LayerEffectType::DigitalDistortion' in preset_catalog

noise_shader = read('data/effect-transitions/shaders/noise/noise.effect')
damage_shader = read('data/effect-transitions/shaders/damage-distortion/damage-distortion.effect')
assert 'profile==8' not in noise_shader and 'profile==9' not in noise_shader and 'profile==10' not in noise_shader
for token in ['composite_film', 'composite_analog', 'composite_digital', 'scratch', 'scan', 'packet']:
    assert token in damage_shader

embedded = read('src/rendering/title-effect-registry.cpp')
for item in ['case LayerEffectType::Grain:', 'case LayerEffectType::FilmDistortion:', 'case LayerEffectType::AnalogDistortion:', 'case LayerEffectType::DigitalDistortion:']:
    assert item in embedded
assert 'kEmbeddedDamageDistortionEffect' in embedded and 'PSDamage' in embedded and 'PSDamageSingle' not in embedded

for filename in ['Grain.obgeffect', 'Film Distortion.obgeffect', 'Analog Distortion.obgeffect', 'Digital Distortion.obgeffect']:
    assert (ROOT / 'data/effect-transitions' / filename).exists(), filename
for removed in ['Animated Noise Drift.obgeffect', 'Glare Sweep.obgeffect', 'Ripple Loop.obgeffect', 'Wave Warp Loop.obgeffect', 'Chromatic Pulse.obgeffect', 'Soft Bloom Highlight.obgeffect', 'Cinematic Halation Warm.obgeffect', 'Micro Contrast Clarity.obgeffect']:
    assert not (ROOT / 'data/effect-transitions' / removed).exists(), removed

hierarchy = read('src/editor/title-editor-internal/hierarchy-model.inc')
for prop in ['offset_x', 'offset_y', 'lacunarity', 'gain', 'channel_intensity']:
    assert prop in hierarchy

print('Development Version 243 video playback, range trim and damage effects contract: PASS')
