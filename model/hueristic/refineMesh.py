import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def function_index(encoded):
    result = {}
    for path in encoded['piecewisePaths']:
        for segment in path['segments']:
            kind, data = segment['type'], segment['data']
            if kind not in ('line', 'bezier'):
                raise ValueError(f'Unsupported function type: {kind}')
            raw_id = int(data['id'])
            # CARLA classifier uses -id-1 to distinguish Beziers from straight lines.
            key = raw_id if kind == 'line' else -raw_id - 1
            if key in result:
                raise ValueError(f'Duplicate encoded function ID: {key}')
            result[key] = (kind, data)
    return result


def sample_function(kind, data, spacing):
    if kind == 'line':
        a, b = Vector(data['start']), Vector(data['end'])
        count = max(1, math.ceil((b-a).length / spacing))
        return [a.lerp(b, i/count) for i in range(count+1)]
    a, b, c = (Vector(data[k]) for k in ('p0', 'p1', 'p2'))
    count = max(1, math.ceil(((b-a).length + (c-b).length) / spacing))
    return [a*(1-t)**2 + b*(2*t*(1-t)) + c*t*t
            for t in (i/count for i in range(count+1))]


def selected_functions(encoded, report, groups, min_score):
    index = function_index(encoded)
    selected = {}
    requested = set(groups or [])
    if requested - set(range(1, len(report['detections'])+1)):
        raise ValueError('--group is outside the 1-based detection range')
    for group, detection in enumerate(report['detections'], 1):
        if requested and group not in requested:
            continue
        if float(detection.get('score', 0)) < min_score:
            continue
        for match in detection['matches']:
            key = int(match['inputFunctionID'])
            if key not in index:
                raise ValueError(f'Matched input function {key} is missing from encoder JSON')
            kind, data = index[key]
            if match.get('type', kind) != kind:
                raise ValueError(f'Function type mismatch for ID {key}')
            entry = selected.setdefault(key, {'kind': kind, 'data': data, 'references': []})
            entry['references'].append({
                'group': group, 'meshID': detection.get('meshID'),
                'referenceView': detection.get('referenceView'),
                'referenceFunctionID': match.get('referenceFunctionID'),
                'score': detection.get('score'),
            })
    if not selected:
        raise ValueError('No matched functions selected')
    return selected


def make_bvh(objects):
    vertices, faces = [], []
    for obj in objects:
        offset = len(vertices)
        vertices.extend(v.co.copy() for v in obj.data.vertices)
        faces.extend(tuple(offset+i for i in p.vertices) for p in obj.data.polygons)
    if not faces:
        raise ValueError('Target has no mesh faces')
    return BVHTree.FromPolygons(vertices, faces, all_triangles=False)


def duplicate_targets(source, collection):
    graph = bpy.context.evaluated_depsgraph_get()
    result = []
    for obj in source:
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(graph), depsgraph=graph)
        mesh.transform(obj.matrix_world)
        copy = bpy.data.objects.new(obj.name + '_Refined', mesh)
        copy['source_object'] = obj.name
        collection.objects.link(copy)
        result.append(copy)
    bpy.context.view_layer.update()
    return result


def curve_overlay(collection, key, runs, entry, radius):
    curve = bpy.data.curves.new(f'Function_{key}', 'CURVE')
    curve.dimensions = '3D'
    curve.bevel_depth = radius
    curve.bevel_resolution = 2
    for run in runs:
        if len(run) < 2:
            continue
        spline = curve.splines.new('POLY')
        spline.points.add(len(run)-1)
        for point, xyz in zip(spline.points, run):
            point.co = (*xyz, 1)
    obj = bpy.data.objects.new(curve.name, curve)
    collection.objects.link(obj)
    obj['inputFunctionID'] = key
    obj['function_type'] = entry['kind']
    obj['classifier_references_json'] = json.dumps(entry['references'])
    return obj


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--encoded', type=Path, required=True)
    p.add_argument('--classifier', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True, help='New .blend file')
    p.add_argument('--builder', type=Path, help='Optional contours_to_blender.py to run first')
    p.add_argument('--contour', type=Path, help='Flat-color contour PNG for --builder')
    p.add_argument('--mesh-collection', default='Generated Contour Layers')
    p.add_argument('--image-width', type=float, default=8.0, help='Full source image width in world units')
    p.add_argument('--center-x', type=float, default=0)
    p.add_argument('--center-y', type=float, default=0)
    p.add_argument('--mode', choices=('groove', 'ridge', 'overlay'), default='groove')
    p.add_argument('--depth-pixels', type=float, default=0.5)
    p.add_argument('--radius-pixels', type=float, default=2.0)
    p.add_argument('--sample-spacing', type=float, default=0.5)
    p.add_argument('--group', type=int, action='append', help='Only this 1-based classifier detection; repeatable')
    p.add_argument('--min-score', type=float, default=-math.inf)
    p.add_argument('--overwrite', action='store_true')
    args = p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    numbers = (args.image_width, args.depth_pixels, args.radius_pixels, args.sample_spacing,
               args.center_x, args.center_y)
    if not all(math.isfinite(x) for x in numbers):
        p.error('Geometry settings must be finite')
    if min(args.image_width, args.radius_pixels, args.sample_spacing) <= 0 or args.depth_pixels < 0:
        p.error('Widths/spacing must be positive; depth must be nonnegative')
    if args.output.suffix.lower() != '.blend':
        p.error('--output must end in .blend')
    output = args.output.resolve()
    report_path = output.with_suffix('.functions.json')
    if any(x.exists() for x in (output, report_path)) and not args.overwrite:
        p.error('Output exists; choose a new filename or use --overwrite')
    if bpy.data.filepath and output == Path(bpy.data.filepath).resolve():
        p.error('Choose a different file from the input .blend')
    encoded, classifier = read_json(args.encoded), read_json(args.classifier)
    width, height = int(encoded['width']), int(encoded['height'])
    if min(width, height) <= 0:
        p.error('Invalid encoder image dimensions')
    selected = selected_functions(encoded, classifier, args.group, args.min_score)
    if bool(args.builder) != bool(args.contour):
        p.error('--builder and --contour must be supplied together')
    if args.builder:
        image = bpy.data.images.load(str(args.contour.resolve()), check_existing=False)
        cw, ch = image.size
        bpy.data.images.remove(image)
        if abs(cw/ch - width/height) > 1e-6:
            p.error('Contour and encoded images must have the same aspect ratio and framing')
        spec = importlib.util.spec_from_file_location('contour_builder', args.builder)
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        builder.IMAGE_WIDTH = args.image_width
        builder.build(args.contour)
        if args.center_x or args.center_y:
            p.error('The supplied contour builder centers its mesh at (0,0)')
    source_collection = bpy.data.collections.get(args.mesh_collection)
    if source_collection is None:
        raise ValueError(f'Mesh collection not found: {args.mesh_collection}')
    source = [o for o in source_collection.all_objects if o.type == 'MESH']
    if not source:
        raise ValueError('Mesh collection contains no mesh objects')
    targets = bpy.data.collections.new('Classifier Refined Mesh')
    overlays = bpy.data.collections.new('Classifier Function Overlay')
    bpy.context.scene.collection.children.link(targets)
    bpy.context.scene.collection.children.link(overlays)
    copies = duplicate_targets(source, targets)
    bvh = make_bvh(copies)
    z_values = [v.co.z for obj in copies for v in obj.data.vertices]
    zmin, zmax = min(z_values), max(z_values)
    scale = args.image_width / width
    margin = max(args.image_width, zmax-zmin, 1.0)
    epsilon = scale * 0.001
    def xy(pixel):
        return (args.center_x + (pixel.x + 0.5 - width/2)*scale,
                args.center_y + (height/2 - pixel.y - 0.5)*scale)
    def front(x, y):
        return bvh.ray_cast(Vector((x, y, zmax+margin)), Vector((0, 0, -1)))[0]
    # Project onto the untouched surface; gaps remain gaps, never bridged.
    records, hits, overlay_objects = [], [], []
    for key, entry in selected.items():
        samples = sample_function(entry['kind'], entry['data'], args.sample_spacing)
        runs, run, points = [], [], []
        for pixel in samples:
            hit = front(*xy(pixel)) if 0 <= pixel.x < width and 0 <= pixel.y < height else None
            if hit is None:
                if run:
                    runs.append(run)
                    run = []
                points.append(None)
            else:
                point = list(hit)
                points.append(point)
                run.append(point)
                hits.append((hit.copy(), key))
        if run:
            runs.append(run)
        overlay_objects.append((curve_overlay(overlays, key, runs, entry, scale*0.15), runs))
        records.append({'inputFunctionID': key, 'type': entry['kind'],
                        'references': entry['references'], 'sampleCount': len(samples),
                        'hitCount': sum(x is not None for x in points),
                        'sourcePixels': [list(v) for v in samples],
                        'surfacePointsBeforeRefinement': points})
    if not hits:
        raise ValueError('No functions hit the mesh. Check image framing, collection and scale.')
    tree = KDTree(len(hits))
    for i, (point, _) in enumerate(hits):
        tree.insert((point.x, point.y, 0), i)
    tree.balance()
    radius, depth = args.radius_pixels*scale, args.depth_pixels*scale
    changed, tagged = 0, 0
    for obj in copies:
        own_bvh = make_bvh([obj])
        obj.data.attributes.new('classifier_function_id', 'INT', 'POINT')
        obj.data.attributes.new('classifier_function_weight', 'FLOAT', 'POINT')
        # Adding an attribute can invalidate existing RNA attribute handles.
        ids = obj.data.attributes['classifier_function_id']
        weights = obj.data.attributes['classifier_function_weight']
        for vertex in obj.data.vertices:
            ids.data[vertex.index].value = -2147483648  # -1 is a valid Bezier ID.
            point = vertex.co
            _, nearest, distance = tree.find((point.x, point.y, 0))
            if distance >= radius:
                continue
            visible = front(point.x, point.y)
            if visible is None or abs(visible.z-point.z) > epsilon:
                continue
            weight = (1-(distance/radius)**2)**2
            ids.data[vertex.index].value = hits[nearest][1]
            weights.data[vertex.index].value = weight
            tagged += 1
            delta = depth*weight
            if args.mode == 'groove':
                back = own_bvh.ray_cast(Vector((point.x, point.y, zmin-margin)), Vector((0, 0, 1)))[0]
                # Avoid cutting through this individual slab, including thin front layers.
                delta = -min(delta, max(0, (point.z-back.z)*0.25)) if back is not None else 0
            elif args.mode == 'overlay':
                delta = 0
            if delta:
                vertex.co.z += delta
                changed += 1
        obj.data.update()
    # Refresh overlay heights after deformation so curves follow the saved mesh.
    final_bvh = make_bvh(copies)
    for obj, runs in overlay_objects:
        usable = (r for r in runs if len(r) >= 2)
        for spline, run in zip(obj.data.splines, usable):
            for point, old in zip(spline.points, run):
                hit = final_bvh.ray_cast(Vector((old[0], old[1], zmax+margin+depth)), Vector((0, 0, -1)))[0]
                point.co = (*(list(hit) if hit is not None else old), 1)
    overlays.hide_render = True  # Inspect the overlay in the viewport; render the refined surface.
    for obj in source:
        obj.hide_set(True)
        obj.hide_render = True
    summary = {'mode': args.mode, 'encoded': str(args.encoded.resolve()),
               'classifier': str(args.classifier.resolve()), 'imageSize': [width, height],
               'projection': {'imageWidthWorld': args.image_width, 'centerXY': [args.center_x, args.center_y],
                              'viewDirection': [0, 0, -1], 'pixelCenterOffset': 0.5},
               'depthPixels': args.depth_pixels, 'radiusPixels': args.radius_pixels,
               'functionCount': len(records), 'projectedSamples': len(hits),
               'missedSamples': sum(r['sampleCount']-r['hitCount'] for r in records),
               'taggedVertices': tagged, 'changedVertices': changed, 'functions': records}
    output.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(summary, indent=2, allow_nan=False), encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    print(f'APPLIED {len(records)} functions, {len(hits)} projected samples; '
          f'{tagged} tagged vertices, {changed} changed vertices. Report: {report_path}')


if __name__ == '__main__':
    main()
