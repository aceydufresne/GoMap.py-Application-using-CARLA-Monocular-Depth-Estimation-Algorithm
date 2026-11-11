#print("Testing")
import importlib.util
import json
from pathlib import Path
import bpy
from mathutils import Vector
import bpy


#recreate the blender virtual scene from the dataset
def recreate_reference_scene(edge_json_path, carla_root=r"F:\CARLA"):
    root = Path(carla_root)
    edge = json.loads(Path(edge_json_path).read_text(encoding="utf-8-sig"))

    mesh_id = edge["meshID"]
    view = int(edge["imageID"])
    width, height = edge["width"], edge["height"]
    if width <= 0 or width != height:
        raise ValueError("The CARLA multiview renderer expects a square image.")

    manifest_path = root / "sets" / "pointr_multiview" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    view_count = int(manifest["configuration"]["views"])
    if not 0 <= view < view_count:
        raise ValueError(f"View {view} is outside the dataset's view range.")

    source = edge.get("source")
    matches = [
        row for row in manifest["examples"]
        if row["mesh_id"] == mesh_id
        and int(row["view"]) == view
        and (source is None or row["source"] == source)
    ]
    if len(matches) != 1:
        raise ValueError(
            f"Expected one manifest entry for mesh {mesh_id}, view {view}. "
            f"Found {len(matches)}."
        )

    scripts = root / "model" / "pointr++"

    def load_module(filename):
        path = scripts / filename
        spec = importlib.util.spec_from_file_location(path.stem, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    generator = load_module("carla_build_multiview.py")
    renderer = load_module("render_mobilenet_views.py")

    mesh_path = next(
        (path for _, path in generator.discover(root, matches[0]["source"])
         if path.stem == mesh_id),
        None,
    )
    if mesh_path is None:
        raise FileNotFoundError(f"Source mesh {mesh_id} was not found under {root}.")

    generator.reset_scene()
    generator.import_mesh(mesh_path)
    generator.normalize_scene()

    renderer.configure_render(width)
    direction = generator.view_direction(view, view_count)
    renderer.make_camera(direction, width)
    bpy.context.view_layer.update()

    return bpy.context.scene, edge


#ray from edge map pixel directly to mesh to find distance
def raycast_function_pixels(scene, edge):
    """Raycast the saved edge pixels and return per-function depth and XYZ."""
    if scene != bpy.context.scene:
        raise ValueError("The recreated scene must be Blender's active scene.")

    camera = scene.camera
    if camera is None or camera.data.type != "ORTHO":
        raise ValueError("This function requires an orthographic camera.")

    width, height = edge["width"], edge["height"]
    if width <= 0 or height <= 0:
        raise ValueError("Image width and height must be positive.")

    # Camera-local bounds of the image plane.
    frame = camera.data.view_frame(scene=scene)
    left = min(corner.x for corner in frame)
    right = max(corner.x for corner in frame)
    bottom = min(corner.y for corner in frame)
    top = max(corner.y for corner in frame)

    transform = camera.matrix_world.copy()
    camera_position = transform.translation
    forward = (transform.to_3x3() @ Vector((0, 0, -1))).normalized()
    near = camera.data.clip_start
    max_distance = camera.data.clip_end - near
    depsgraph = bpy.context.evaluated_depsgraph_get()

    # Cache successful hits and misses across all segments in this view.
    pixel_hits = {}

    def cast_pixel(x, y):
        if not (0 <= x < width and 0 <= y < height):
            raise ValueError(f"Pixel {(x, y)} is outside the {width}x{height} image.")

        pixel = (x, y)
        if pixel in pixel_hits:
            return pixel_hits[pixel]

        # Edge pixels use a top-left origin. Sample the center of each pixel.
        u = (x + 0.5) / width
        v = (y + 0.5) / height
        origin = transform @ Vector((
            left + u * (right - left),
            top - v * (top - bottom),
            -near,
        ))

        hit, location, normal, face_index, obj, _ = scene.ray_cast(
            depsgraph, origin, forward, distance=max_distance
        )

        result = None
        if hit and obj is not None:
            result = {
                "pixel": [x, y],
                "depth": float((location - camera_position).dot(forward)),
                "xyz": list(location),
                "normal": list(normal),
                "object": obj.name,
                "faceIndex": face_index,
            }

        pixel_hits[pixel] = result
        return result

    functions = []
    for path in edge["piecewisePaths"]:
        for sequence_index, segment in enumerate(path["segments"]):
            data = segment["data"]
            pixels = data["pixels"]
            samples = [cast_pixel(x, y) for x, y in pixels]
            points = [sample for sample in samples if sample is not None]

            functions.append({
                "meshID": edge["meshID"],
                "imageID": edge["imageID"],
                "componentID": path["edgeID"],
                "pathID": path["pathID"],
                "sequenceIndex": sequence_index,
                "functionID": data["id"],
                "type": segment["type"],
                "pixelCount": len(pixels),
                "hitCount": len(points),
                "pixels": pixels,
                "depths": [
                    sample["depth"] if sample is not None else None
                    for sample in samples
                ],
                "points": points,
            })

    return functions
