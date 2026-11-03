"""Generate native strip/fan fixtures and retained source metadata."""
import base64
import json
import struct
from pathlib import Path

blob = bytearray()
views, accessors, primitives = [], [], []

def accessor(values, shape, component=5126):
    while len(blob) % 4:
        blob.append(0)
    data = struct.pack('<' + ('f' if component == 5126 else 'H') * len(values), *values)
    views.append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)})
    blob.extend(data)
    width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[shape]
    a = {'bufferView': len(views)-1, 'componentType': component,
         'count': len(values)//width, 'type': shape}
    if shape == 'VEC3':
        a['min'] = [min(values[i::3]) for i in range(3)]
        a['max'] = [max(values[i::3]) for i in range(3)]
    accessors.append(a)
    return len(accessors)-1

normals = accessor([0, 0, 1]*4, 'VEC3')
indices = accessor([0, 1, 2, 3], 'SCALAR', 5123)
uv = accessor([0, 0, 1, 0, 0, 1, 1, 1], 'VEC2')
for mode in (5, 6):
    positions = ([-1,-1,0, 1,-1,0, -1,1,0, 1,1,0] if mode == 5 else
                 [-1,-1,0, 1,-1,0, 1,1,0, -1,1,0])
    position = accessor(positions, 'VEC3')
    for indexed in (True, False):
        for normal in (True, False):
            attributes = {'POSITION': position, 'TEXCOORD_0': uv, '_CUSTOM': uv}
            if normal:
                attributes['NORMAL'] = normals
            primitive = {'mode': mode, 'attributes': attributes, 'material': 0,
                         'extras': {'label': 'retained primitive metadata'}}
            if indexed:
                primitive['indices'] = indices
            primitives.append(primitive)
times = accessor([0, 1, 2, 3], 'SCALAR')
translations = accessor([0,0,0, 1,0,0, 2,0,0, 3,0,0], 'VEC3')
model = {'asset': {'version': '2.0', 'generator': 'topology tests', 'extras': {'author': 'test'}},
         'buffers': [{'uri': 'data:application/octet-stream;base64,' + base64.b64encode(blob).decode(), 'byteLength': len(blob)}],
         'bufferViews': views, 'accessors': accessors,
         'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [0,0,0,1]}, 'emissiveFactor': [1,0,0]}],
         'meshes': [{'name': 'topologies', 'primitives': primitives}],
         'nodes': [{'mesh': 0, 'camera': 0, 'extras': {'nodeTag': 42}}, {'name': 'unused joint'}],
         'cameras': [{'type': 'perspective', 'perspective': {'yfov': 0.8, 'znear': 0.1}}],
         'animations': [{'samplers': [{'input': times, 'output': translations, 'interpolation': 'LINEAR'}],
                         'channels': [{'sampler': 0, 'target': {'node': 1, 'path': 'translation'}}]}],
         'skins': [{'joints': [1], 'name': 'retained unused skin'}],
         'scenes': [{'nodes': [0]}, {'name': 'other scene', 'nodes': [1]}], 'scene': 0,
         'extras': {'documentTag': 'preserved'}}
Path(__file__).with_name('topologies.gltf').write_text(json.dumps(model, indent=2) + '\n')
