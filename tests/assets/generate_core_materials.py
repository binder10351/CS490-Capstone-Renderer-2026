"""Regenerate self-contained core material fixtures using only Python's stdlib."""
import base64
import copy
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent
blob = bytearray()
views, accessors = [], []

def attribute(values, shape, component=5126, normalized=False):
    while len(blob) % 4:
        blob.append(0)
    fmt = {5126: 'f', 5123: 'H', 5121: 'B'}[component]
    data = struct.pack('<' + fmt * len(values), *values)
    views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)})
    blob.extend(data)
    channels = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[shape]
    accessors.append({'bufferView': len(views)-1, 'componentType': component,
                      'count': len(values)//channels, 'type': shape})
    if normalized:
        accessors[-1]['normalized'] = True
    return len(accessors)-1

pos = attribute([0,0,0, 1,0,0, 0,1,0, 0,0,1], 'VEC3')
accessors[pos].update(min=[0,0,0], max=[1,1,1])
uv0 = attribute([0,0, 1,0, 0,1, 1,1], 'VEC2')
uv1 = attribute([0,0, 128,0, 0,128, 128,128], 'VEC2', 5121, True)
uv2 = attribute([0.1,0.2, 0.3,0.4, 0.5,0.6, 0.7,0.8], 'VEC2')
color = attribute([255,128,0,64, 128,255,64,255, 64,0,255,128, 0,255,128,255], 'VEC4', 5121, True)
rgb = attribute([1,0.5,0.25] * 4, 'VEC3')
normal = attribute([0,0,1] * 4, 'VEC3')
tangent = attribute([1,0,0,-1] * 4, 'VEC4')
indices = attribute([0,1,2, 0,3,1], 'SCALAR', 5123)
attrs = {'POSITION': pos, 'TEXCOORD_0': uv0, 'TEXCOORD_1': uv1,
         'TEXCOORD_2': uv2, 'COLOR_0': color, 'COLOR_1': rgb, 'TANGENT': tangent}
material = {
    'name': 'Core layered test', 'alphaMode': 'MASK', 'alphaCutoff': 0.3, 'doubleSided': True,
    'pbrMetallicRoughness': {'baseColorFactor': [0.8,0.6,0.4,0.5], 'metallicFactor': 0.2,
        'roughnessFactor': 0.7, 'baseColorTexture': {'index': 0},
        'metallicRoughnessTexture': {'index': 1, 'texCoord': 1}},
    'normalTexture': {'index': 0, 'texCoord': 1, 'scale': 0.35},
    'occlusionTexture': {'index': 0, 'strength': 0.4},
    'emissiveFactor': [0.1,0.2,0.3], 'emissiveTexture': {'index': 1, 'texCoord': 2}}
model = {
    'asset': {'version': '2.0'},
    'buffers': [{'byteLength': len(blob), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(blob).decode()}],
    'bufferViews': views, 'accessors': accessors,
    'images': [{'name': 'Shared image', 'uri': 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGM40fABAARMAjnqijmoAAAAAElFTkSuQmCC'}],
    'samplers': [{'magFilter': 9728, 'minFilter': 9984, 'wrapS': 33071, 'wrapT': 33648}],
    'textures': [{'name': 'Nearest', 'source': 0, 'sampler': 0}, {'name': 'Default sampler', 'source': 0}],
    'materials': [material, {'alphaMode': 'BLEND'}, {}],
    'meshes': [{'name': 'Shared folded mesh', 'primitives': [
        {'attributes': attrs, 'indices': indices, 'material': 0},
        {'attributes': dict(attrs, NORMAL=normal, COLOR_0=rgb), 'indices': indices, 'material': 1}]}],
    'nodes': [{'children': [1,2]}, {'mesh': 0}, {'mesh': 0, 'scale': [-1,1,1]}],
    'scenes': [{'nodes': [0]}], 'scene': 0}
(ROOT / 'core_materials.gltf').write_text(json.dumps(model, indent=2) + '\n')
glb = copy.deepcopy(model)
del glb['buffers'][0]['uri']
# Exercise embedded GLB images as well as a binary geometry chunk.
image = base64.b64decode(glb['images'][0].pop('uri').split(',')[1])
while len(blob) % 4:
    blob.append(0)
glb['bufferViews'].append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(image)})
glb['images'][0].update(bufferView=len(glb['bufferViews'])-1, mimeType='image/png')
blob.extend(image)
glb['buffers'][0]['byteLength'] = len(blob)
js = json.dumps(glb, separators=(',', ':')).encode()
js += b' ' * (-len(js) % 4)
blob.extend(b'\0' * (-len(blob) % 4))
chunks = struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(blob), 0x004E4942) + blob
(ROOT / 'core_materials.glb').write_bytes(struct.pack('<III', 0x46546C67, 2, 12 + len(chunks)) + chunks)

# Lossless 16-bit RGBA PNG, independent of third-party image libraries.
import zlib

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

png16 = b'\x89PNG\r\n\x1a\n'
png16 += chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 16, 6, 0, 0, 0))
png16 += chunk(b'IDAT', zlib.compress(b'\0' + struct.pack('>4H', 40000, 20000, 60000, 65535)))
png16 += chunk(b'IEND', b'')
model16 = copy.deepcopy(model)
model16['images'][0]['uri'] = 'data:image/png;base64,' + base64.b64encode(png16).decode()
(ROOT / 'core_materials_16.gltf').write_text(json.dumps(model16, indent=2) + '\n')
