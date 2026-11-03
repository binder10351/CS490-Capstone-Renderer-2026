"""Check real Vulkan frames under an existing X11 display.

Requires Python Pillow and ImageMagick's `import`. Example:
  python3 tests/check_material_frames.py build/renderer_material_tests /tmp/material-frames
"""
from pathlib import Path
import os
import subprocess
import sys
import time
from PIL import Image

executable = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
background = (0.015, 0.02, 0.05)

def srgb(values):
    return tuple(round(255 * (12.92*v if v <= 0.0031308 else 1.055*v**(1/2.4)-0.055)) for v in values)

def expect(pixel, target, name, tolerance=4):
    if any(abs(a-b) > tolerance for a, b in zip(pixel, target)):
        raise AssertionError(f'{name}: expected {target}, got {pixel}')

process = subprocess.Popen([str(executable), '--capture'], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True,
                           env={**os.environ, 'SDL_VIDEO_DRIVER': 'x11'})
pixels = {}
try:
    for line in process.stdout:
        if not line.startswith('READY '):
            print(line.strip())
            continue
        name = line.strip().split()[1]
        time.sleep(0.08)
        path = output / f'{name}.png'
        subprocess.run(['import', '-window', 'Material render tests', str(path)], check=True, timeout=10)
        with Image.open(path).convert('RGB') as image:
            pixels[name] = image.getpixel((image.width//2, image.height//2))
        process.stdin.write('\n')
        process.stdin.flush()
    if process.wait(timeout=10) != 0:
        raise RuntimeError(f'Render test exited with {process.returncode}')
finally:
    if process.poll() is None:
        process.kill()
        process.wait()

for name in ('opaque', 'mask_keep', 'double_back', 'mirrored_front'):
    expect(pixels[name], (255,0,0), name)
for name in ('mask_discard', 'culled_back'):
    expect(pixels[name], srgb(background), name)
# These uniform interior samples also verify exposure and Reinhard tone mapping.
def tone(value):
    return value / (1 + value)

expect(pixels['post_tone_mapping'], srgb((tone(1), 0, 0)), 'tone mapping')
expect(pixels['post_exposure'], srgb((tone(0.25), 0, 0)), 'exposure')
for name in ('post_tone_mapping_off', 'post_fxaa_off'):
    expect(pixels[name], srgb((0.25, 0, 0)), name)
blend = srgb((0.25+background[0]*0.75, background[1]*0.75, background[2]*0.75))
expect(pixels['blend'], blend, 'blend')
for i in range(8):
    expect(pixels[f'variant_{i}'], blend if i & 4 else (255,0,0), f'variant_{i}')
expect(pixels['sorted_blend'], srgb((0.25+background[0]*0.25, 0.5+background[1]*0.25, background[2]*0.25)), 'sorted_blend')
expect(pixels['emissive_srgb'], (200,128,240), 'emissive_srgb')
expect(pixels['emissive_16'], tuple(round(v/65535*255) for v in (40000,20000,60000)), 'emissive_16')
expect(pixels['lit_back'], pixels['lit_front'], 'double-sided normal reversal')
for name in ('emissive_off', 'lights_off', 'intensity_zero', 'shading_off'):
    expect(pixels[name], (0,0,0), name)
expect(pixels['ambient_only'], srgb((0.15,0.15,0.15)), 'ambient intensity')
expect(pixels['vertex_color_off'], (pixels['vertex_color_off'][0],)*3, 'vertex color toggle')
if pixels['direct_only'][0] < 80:
    raise AssertionError('World light intensity did not enable direct illumination')
for name in ('ui', 'ui_resized', 'ui_reloaded'):
    expect(pixels[name], (255,0,0), name)
    with Image.open(output / f'{name}.png').convert('RGB') as image:
        if image.getpixel((image.width-5, 5)) == srgb(background):
            raise AssertionError(f'{name}: menu bar was not drawn')
red, green, blue = pixels['vertex_color']
if red < 100 or red < green * 2 or red < blue * 2:
    raise AssertionError(f'Vertex color did not tint base color: {pixels["vertex_color"]}')


# Increasing emissive radiance should preserve settled auto-exposed brightness.
expect(pixels["post_auto_bright"], pixels["post_auto"], "auto exposure radiance compensation", tolerance=5)

if pixels["post_auto_transition"][0] <= pixels["post_auto_bright"][0] + 10:
    raise AssertionError("Auto exposure jumped to its target instead of adapting gradually")

for i in range(8):
    expect(pixels[f"topology_{i}"], (255, 0, 0), f"native topology {i}")

# The caster lies outside the camera frustum in every shadow test.
for name in ('shadow_strength_zero', 'shadow_mask_discard', 'shadow_texture_cutout',
             'shadow_vertex_cutout', 'shadow_blend_caster', 'shadow_out_of_range',
             'shadow_caster_moved', 'shadow_light_moved'):
    expect(pixels[name], pixels['shadow_off'], name)
shadowed = pixels['shadow_cascade_1']
if pixels['shadow_off'][0] - shadowed[0] < 40:
    raise AssertionError(f'Offscreen caster failed to shadow receiver: {pixels["shadow_off"]} -> {shadowed}')
expect(shadowed, srgb((0.05,)*3), 'shadow preserves ambient light')
for name in ('shadow_cascade_0', 'shadow_cascade_2', 'shadow_mask_keep', 'shadow_texture_disabled',
             'shadow_vertex_disabled', 'shadow_mirrored', 'shadow_double_back', 'shadow_blend_receiver',
             'shadow_resized', 'shadow_strip', 'shadow_fan', 'shadow_light_disabled',
             'shadow_caster_returned', 'shadow_camera_moved'):
    expect(pixels[name], shadowed, name)
if pixels['shadow_debug'][1] <= pixels['shadow_debug'][0] + 30:
    raise AssertionError('Second cascade debug color should be green')

expect(pixels['shadow_emissive'], srgb((0.15,0.05,0.05)), 'shadows preserve emissive light')

print(f'Checked {len(pixels)} Vulkan frames; captures: {output}')
