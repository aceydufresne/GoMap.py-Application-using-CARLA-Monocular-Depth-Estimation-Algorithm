

import argparse
from collections import defaultdict
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Vector


# Editable settings, in Blender units.
IMAGE_WIDTH = 8.0
DEPTH_STEP = 0.0036    # Average depth increment; total relief depth = count * this.
BACK_TO_FRONT_RATIO = 24.0  # Stronger thickness increase toward the back.
EXTRA_BACK_THICKNESS = 0.45 # Extra solid backing on the bottom slab.
GRAY = (0.5, 0.5, 0.5, 1.0)
MIN_COLOR_PIXELS = 4
COLLECTION_NAME = 'Generated Contour Layers'


def boundary_loops(mask):
    """Trace pixel boundaries, retaining holes and disconnected components.

    Coordinates use Blender's bottom-up image order. Outer loops are CCW;
    hole loops are CW. At diagonal contacts, keep the regions separate.
    """
    padded = np.pad(mask, 1)
    edges = defaultdict(list)
    sides = (
        (mask & ~padded[:-2, 1:-1], (0, 0), (1, 0)),  # bottom
        (mask & ~padded[1:-1, 2:], (1, 0), (1, 1)),   # right
        (mask & ~padded[2:, 1:-1], (1, 1), (0, 1)),   # top
        (mask & ~padded[1:-1, :-2], (0, 1), (0, 0)),  # left
    )
    for selected, start, end in sides:
        ys, xs = np.nonzero(selected)
        for x, y in zip(xs.tolist(), ys.tolist()):
            edges[(x + start[0], y + start[1])].append((x + end[0], y + end[1]))

    direction = {(1, 0): 0, (0, 1): 1, (-1, 0): 2, (0, -1): 3}
    turn_priority = {1: 0, 0: 1, 3: 2, 2: 3}
    loops = []
    while edges:
        start = next(iter(edges))
        point = start
        previous = None
        loop = []
        while True:
            loop.append(point)
            choices = edges[point]
            if previous is None or len(choices) == 1:
                following = choices[0]
            else:
                incoming = direction[(point[0] - previous[0], point[1] - previous[1])]
                following = min(choices, key=lambda q: turn_priority[
                    (direction[(q[0] - point[0], q[1] - point[1])] - incoming) % 4])
            choices.remove(following)
            if not choices:
                del edges[point]
            previous, point = point, following
            if point == start:
                break
        # Remove only collinear vertices: preserve the original pixel silhouette.
        simplified = []
        for i, p in enumerate(loop):
            a, b = loop[i - 1], loop[(i + 1) % len(loop)]
            if (p[0] - a[0]) * (b[1] - p[1]) != (p[1] - a[1]) * (b[0] - p[0]):
                simplified.append(p)
        if len(simplified) >= 3:
            loops.append(simplified)
    return loops


def area(loop):
    return sum(a[0] * b[1] - b[0] * a[1]
               for a, b in zip(loop, loop[1:] + loop[:1])) / 2


def srgb_to_linear(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def extruded_pixel_mesh(name, mask, scale, thickness):
    """Build exact filled regions, including holes, without polygon-fill ambiguity."""
    height, width = mask.shape
    vertices, faces, indices = [], [], {}
    def vertex(x, y, side):
        key = (x, y, side)
        if key not in indices:
            indices[key] = len(vertices)
            vertices.append(((x - width / 2) * scale,
                             (y - height / 2) * scale, side * thickness / 2))
        return indices[key]
    ys, xs = np.nonzero(mask)
    for x, y in zip(xs.tolist(), ys.tolist()):
        square = [(x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1)]
        top = [vertex(a, b, 1) for a, b in square]
        bottom = [vertex(a, b, -1) for a, b in square]
        faces.extend((top, bottom[::-1]))
        neighbors = [(x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)]
        for i, (nx, ny) in enumerate(neighbors):
            if nx < 0 or nx >= width or ny < 0 or ny >= height or not mask[ny, nx]:
                j = (i + 1) % 4
                faces.append((bottom[i], bottom[j], top[j], top[i]))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    return mesh


def build(input_path):
    if DEPTH_STEP <= 0 or EXTRA_BACK_THICKNESS < 0 or BACK_TO_FRONT_RATIO < 1:
        raise ValueError('DEPTH_STEP must be positive; backing nonnegative; depth ratio >= 1')
    image = bpy.data.images.load(str(input_path.resolve()), check_existing=False)
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    rgba = pixels.reshape(height, width, 4)
    rgb = np.rint(np.clip(rgba[:, :, :3], 0, 1) * 255).astype(np.uint8)
    codes = (rgb[:, :, 0].astype(np.int32) << 16) | (rgb[:, :, 1].astype(np.int32) << 8) | rgb[:, :, 2]
    # Infer the background from the most frequent perimeter color.
    border = np.concatenate((codes[0], codes[-1], codes[:, 0], codes[:, -1]))
    values, counts = np.unique(border, return_counts=True)
    background = values[np.argmax(counts)]
    colors, counts = np.unique(codes[rgba[:, :, 3] > 0.5], return_counts=True)
    layers = []
    for color, count in zip(colors.tolist(), counts.tolist()):
        if color == background or count < MIN_COLOR_PIXELS:
            continue
        loops = boundary_loops((codes == color) & (rgba[:, :, 3] > 0.5))
        if loops:
            layers.append((max(abs(area(loop)) for loop in loops), color, loops))
    layers.sort(key=lambda item: (-item[0], item[1]))
    if not layers:
        raise ValueError('No foreground color regions found')

    # Isolate the result from existing objects, including Blender's default cube.
    scene = bpy.data.scenes.new('Contour Layers Scene')
    bpy.context.window.scene = scene

    # Replace only this script's generated collection; preserve other scene objects.
    old = bpy.data.collections.get(COLLECTION_NAME)
    if old:
        for obj in list(old.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(old)
    collection = bpy.data.collections.new(COLLECTION_NAME)
    scene.collection.children.link(collection)
    scale = IMAGE_WIDTH / width
    material = bpy.data.materials.new('Contour Neutral Gray')
    material.use_nodes = True
    material.diffuse_color = GRAY
    shader = material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = GRAY
    shader.inputs['Roughness'].default_value = 0.75
    # Rank 0 is furthest from the camera. Allocate more of the fixed depth
    # budget to back layers, compressing small details near the front.
    weights = np.linspace(BACK_TO_FRONT_RATIO, 1.0, len(layers))
    increments = weights / weights.sum() * (len(layers) * DEPTH_STEP)
    front_depths = np.cumsum(increments)
    rank_map = np.full((height, width), -1, dtype=np.int32)
    for rank, (_, color, _) in enumerate(layers):
        rank_map[(codes == color) & (rgba[:, :, 3] > 0.5)] = rank
    for rank, (largest_area, color, loops) in enumerate(layers):
        label = f'Contour_{rank:03d}_{color:06X}'
        layer_depth = float(front_depths[rank])
        back_extension = EXTRA_BACK_THICKNESS if rank == 0 else 0.0
        back_depth = float(front_depths[rank - 1]) if rank else -back_extension
        thickness = layer_depth - back_depth
        # Include the footprints of every region above this layer. These
        # supporting slabs meet face-to-face instead of forming floating pieces.
        mesh = extruded_pixel_mesh(label, rank_map >= rank,
                                   scale, thickness)
        obj = bpy.data.objects.new(label, mesh)
        collection.objects.link(obj)
        obj.location.z = (layer_depth + back_depth) / 2
        obj['largest_visible_contour_area_pixels'] = largest_area
        obj['color_hex'] = f'#{color:06X}'
        obj['thickness'] = thickness
        obj['back_depth'] = back_depth
        obj['front_depth'] = layer_depth
        obj['depth_increment'] = float(increments[rank])
        obj['depth_rank'] = rank
        mesh.materials.append(material)

    slabs = sorted((o for o in collection.objects if o.type == 'MESH'),
                   key=lambda o: o['depth_rank'])
    assert all(a['thickness'] > b['thickness'] for a, b in zip(slabs, slabs[1:]))
    assert all(abs(a['front_depth'] - b['back_depth']) < 1e-9
               for a, b in zip(slabs, slabs[1:]))
    print(f'Actual bottom thickness: {slabs[0]["thickness"]:.6f}; '
          f'front thickness: {slabs[-1]["thickness"]:.6f}; all levels touch')

    camera_data = bpy.data.cameras.new('Contour Camera')
    camera = bpy.data.objects.new('Contour Camera', camera_data)
    collection.objects.link(camera)
    camera.location = (0, 0, len(layers) * DEPTH_STEP + 12)
    camera.rotation_euler = (Vector((0, 0, 0)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.type = 'ORTHO'
    camera_data.ortho_scale = max(IMAGE_WIDTH, height * scale) * 1.08
    camera_data.clip_end = camera.location.z + 100
    scene.camera = camera
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 8
    scene.render.resolution_x = width * 2
    scene.render.resolution_y = height * 2
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'None'
    scene.world = bpy.data.worlds.new('Contour World')
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (0.009, 0.009, 0.009, 1)
    # Broad lights reveal the depth of the uniformly gray slabs.
    target = Vector((0, 0, len(layers) * DEPTH_STEP / 2))
    for name, offset, power in (
            ('Contour Key', (10, -8, 18), 2400),
            ('Contour Fill', (-9, 4, 14), 1800),
            ('Contour Side', (13, 6, 7), 1500)):
        light_data = bpy.data.lights.new(name, 'AREA')
        light_data.energy = power
        light_data.size = 8
        light = bpy.data.objects.new(name, light_data)
        collection.objects.link(light)
        light.location = target + Vector(offset)
        light.rotation_euler = (target - light.location).to_track_quat('-Z', 'Y').to_euler()
    # Make the saved viewport open in the generated camera view.
    for screen in bpy.data.screens:
        for region in screen.areas:
            if region.type == 'VIEW_3D':
                region.spaces.active.region_3d.view_perspective = 'CAMERA'
    bpy.data.images.remove(image)
    print(f'Created {len(layers)} gray relief layers; back/front increment ratio '
          f'{BACK_TO_FRONT_RATIO}; total relief depth {float(front_depths[-1]):.6f}')
    return collection


def main():
    base = Path(globals().get('__file__', bpy.data.filepath or '.')).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=base / 'ContourInputExample_fewer_contours.png')
    parser.add_argument('--save', type=Path)
    parser.add_argument('--render', type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    build(args.input)
    if args.save:
        args.save.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(args.save.resolve()))
    if args.render:
        args.render.parent.mkdir(parents=True, exist_ok=True)
        bpy.context.scene.render.filepath = str(args.render.resolve())
        bpy.ops.render.render(write_still=True)


if __name__ == '__main__':
    main()
