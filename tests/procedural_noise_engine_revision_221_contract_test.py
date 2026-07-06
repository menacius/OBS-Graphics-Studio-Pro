#!/usr/bin/env python3
"""Current procedural Noise contract retained under the historical 221 test name."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
def read(p): return (ROOT/p).read_text(encoding="utf-8")
cmake=read("CMakeLists.txt"); build=read("src/core/build-info.h"); schema=read("src/core/title-serialization-schema.h")
runtime=read("src/effects/effect-runtime.cpp"); defaults=read("src/effects/effect-preset-catalog.cpp")
shader=read("data/effect-transitions/shaders/noise/noise.effect"); panel=read("src/effects/effects-panel.cpp")
loader=read("src/core/title-data.cpp"); manifest=json.loads(read("tests/test-suite-manifest.json"))
assert 'OBS_BGS_DEVELOPMENT_VERSION "239"' in cmake
assert 'BGL_DEVELOPMENT_VERSION "239"' in build
assert 'kCurrentDevelopmentVersion = 239' in schema and 'case 221:' in schema and 'case 224:' in schema and 'case 225:' in schema
assert manifest["development_version"] == 239
assert '"bgl.builtin.noise"' in runtime and 'true, 4, LayerEffectSpace::LayerSpace' in runtime
assert 'effect.effect_profile = 3; /* Clouds / fBM */' in defaults and 'case LayerEffectType::Grain:' in defaults
for profile in ("Fine Grain","Film Grain","Digital Sensor","Clouds / fBM","Turbulence","Ridged","Cellular","Blue-noise Dither"):
    assert profile in panel
assert "NoiseEngineLegacy" not in panel and "NoiseEngineProcedural" not in panel
assert "noiseVersion" not in shader and "legacy_profile" not in shader
for fn in ("gradient_noise","cellular","fractal","blue"):
    assert fn in shader
assert "effect.extension_schema_version < descriptor->schema_version" in loader
print("Current procedural Noise/Grain schema-4 contract passed")
