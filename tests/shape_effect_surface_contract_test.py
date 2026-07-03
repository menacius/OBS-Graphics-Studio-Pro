from pathlib import Path


def test_gpu_primitive_rejects_out_of_bounds_effects():
    source = (Path(__file__).parents[1] / "src/obs/title-source/gpu-masks-groups-cache.inc").read_text()
    assert "shadow.drop_enabled || shadow.long_enabled" in source
    assert "eval_background_enabled(layer, local_time)" in source
    assert "layer_has_stackable_pixel_effects(layer)" in source
    assert "clipped_effect_surface_rect()" in source
