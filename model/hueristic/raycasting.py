#print("Testing")
import bpy
import numpy as np
import argparse
import json
import sys

from pathlib import Path
from mathutils import Vector


def get_scene_meshes():
    return [
        obj for obj in bpy.context.scene.objects
        if obj.type == "MESH"
    ]


def normalize_scene():
    meshes = get_scene_meshes()

    if not meshes:
        raise ValueError("No mesh objects found!")

    # Match original renderer's root-parent normalization
    roots = [obj for obj in bpy.context.scene.objects
             if obj.parent is None]

    if len(roots) > 1:
        parent = bpy.data.objects.new("ParentEmpty", None)
        bpy.context.scene.collection.objects.link(parent)

        for obj in roots:
            if obj != parent:
                obj.parent = parent

    bpy.context.view_layer.update()

    def bounds():
        coords = [
            obj.matrix_world @ Vector(corner)
            for obj in meshes
            for corner in obj.bound_box
        ]
        return (
            Vector(tuple(min(p[i] for p in coords) for i in range(3))),
            Vector(tuple(max(p[i] for p in coords) for i in range(3)))
        )

    bbox_min, bbox_max = bounds()

    scale = 1.0 / max(bbox_max - bbox_min)

    for obj in bpy.context.scene.objects:
        if obj.parent is None:
            obj.scale *= scale

    bpy.context.view_layer.update()

    bbox_min, bbox_max = bounds()
    offset = -(bbox_min + bbox_max) / 2.0

    for obj in bpy.context.scene.objects:
        if obj.parent is None:
            obj.matrix_world.translation += offset

    bpy.context.view_layer.update()




def raycastEdges(pixel_coordinates, camera_path, width=512, height=512):
    
    RT = np.load(camera_path)
    print(f"Camera matrix shape: {RT.shape}", flush=True)

    if RT.shape not in [(3, 4), (4, 4)]:
        raise ValueError(f"Unexpected camera matrix shape: {RT.shape}")

    R = RT[:3, :3]
    T = RT[:3, 3]

    camera_origin = -R.T @ T
    
    camera_forward = R.T @ np.array([0.0, 0.0, -1.0])

    print("Camera origin:", camera_origin)
    print("Camera forward:", camera_forward)

    focal_length = 35.0
    sensor_width = 32.0

    fx = width * focal_length / sensor_width
    fy = fx

    cx = width / 2.0
    cy = height / 2.0

    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()
    
    
    origin = Vector(camera_origin.tolist())
    forward = Vector(camera_forward.tolist())

    hit, location, normal, face_index, obj, matrix = scene.ray_cast(
        depsgraph,
        origin,
        forward,
        distance=1000.0
    )

    print("Center ray hit:", hit)

    if hit:
        print("Center intersection:", location)
        print("Hit object:", obj.name)

    grid_hits = 0
    grid_total = 0

    for y in range(0, height, 16):
        for x in range(0, width, 16):
            direction_camera = np.array([
                (x + 0.5 - cx) / fx,
                -(y + 0.5 - cy) / fy,
                -1.0
            ])

            direction_camera /= np.linalg.norm(direction_camera)

            direction_world = R.T @ direction_camera
            direction_world /= np.linalg.norm(direction_world)

            direction = Vector(direction_world.tolist())

            hit, *_ = scene.ray_cast(
                depsgraph,
                origin,
                direction,
                distance=1000.0
            )

            grid_total += 1
            if hit:
                grid_hits += 1

    print(
        f"Image grid coverage: {grid_hits}/{grid_total} "
        f"({100 * grid_hits / grid_total:.2f}%)"
    )


    points = []
    misses = 0
    for x, y in pixel_coordinates:
        direction_camera = np.array([(x + 0.5 - cx) / fx,-(y + 0.5 - cy) / fy,-1.0])
        direction_camera /= np.linalg.norm(direction_camera)

        direction_world = R.T @ direction_camera
        direction_world /= np.linalg.norm(direction_world)

        
        direction = Vector(direction_world.tolist())

        hit, location, normal, face_index, obj, matrix = (scene.ray_cast(depsgraph, origin, direction, distance=1000.0))

        if hit:
            points.append({
                "pixel": [int(x), int(y)],
                "xyz": [float(v) for v in location],
                "object": obj.name,
                "face_index": face_index
            })
        else:
            misses += 1
            
    print(f"Raycast hits: {len(points)}", flush=True)
    print(f"Raycast misses: {misses}", flush=True)
    print(
        f"Hit rate: {100 * len(points) / max(1, len(pixel_coordinates)):.2f}%",
        flush=True
    )
            
    return points



def createRaycastMask(camera_path, width=512, height=512):
    RT = np.load(camera_path)

    R = RT[:3, :3]
    T = RT[:3, 3]

    camera_origin = -R.T @ T

    focal_length = 35.0
    sensor_width = 32.0

    fx = width * focal_length / sensor_width
    fy = fx

    cx = width / 2.0
    cy = height / 2.0

    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()

    origin = Vector(camera_origin.tolist())

    mask = np.zeros((height, width), dtype=np.uint8)

    for y in range(height):
        for x in range(width):
            direction_camera = np.array([
                (x + 0.5 - cx) / fx,
                -(y + 0.5 - cy) / fy,
                -1.0
            ])

            direction_camera /= np.linalg.norm(direction_camera)

            direction_world = R.T @ direction_camera
            direction_world /= np.linalg.norm(direction_world)

            direction = Vector(direction_world.tolist())

            hit, *_ = scene.ray_cast(
                depsgraph,
                origin,
                direction,
                distance=1000.0
            )

            if hit:
                mask[y, x] = 255

    return mask




def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    parser = argparse.ArgumentParser()
    parser.add_argument("--matches", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--camera", required=True)
    parser.add_argument("--mesh", required=True)

    args = parser.parse_args(argv)

    # 1. Load edgeEncoder JSON
    with open(args.matches, "r", encoding="utf-8") as file:
        data = json.load(file)

    graphs = data["graphs"]

    pixel_coordinates = []
    pixel_components = {}

    for graph in graphs:
        component_id = graph["componentID"]

        for node in graph["nodes"]:
            pixel = tuple(node["pixel"])
            pixel_coordinates.append(pixel)
            pixel_components[pixel] = component_id

    pixel_coordinates = list(dict.fromkeys(pixel_coordinates))
    if not pixel_coordinates:
        raise ValueError("No edge pixels found in JSON!")

    print(f"Loaded {len(graphs)} components")
    print(f"Collected {len(pixel_coordinates)} unique edge pixels")

    # 2. Load original mesh
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    if args.mesh.lower().endswith(".fbx"):
        bpy.ops.import_scene.fbx(filepath=args.mesh)
    elif args.mesh.lower().endswith((".glb", ".gltf")):
        bpy.ops.import_scene.gltf(filepath=args.mesh, merge_vertices=True)
    else:
        raise ValueError(f"Unsupported mesh format: {args.mesh}")

    normalize_scene()
    
    
    mask = createRaycastMask(args.camera)

    mask_path = r"F:\CARLA set\model\raycast_mask.png"

    # Save the mask using Blender's image API
    image = bpy.data.images.new(
        "RaycastMask",
        width=512,
        height=512,
        alpha=True
    )

    rgba = np.ones((512, 512, 4), dtype=np.float32)
    rgba[:, :, :3] = mask[:, :, None] / 255.0
    rgba[:, :, 3] = 1.0

    # Blender's image buffer is bottom-up
    rgba = np.flipud(rgba)

    image.pixels.foreach_set(rgba.ravel())
    image.filepath_raw = mask_path
    image.file_format = "PNG"
    image.save()

    print(f"Saved raycast mask to {mask_path}")
    

    print("Original mesh imported and normalized.", flush=True)

    # 3. Project pixels onto mesh
    points = raycastEdges(
        pixel_coordinates,
        args.camera
    )
    for point in points:
        pixel = tuple(point["pixel"])
        point["componentID"] = pixel_components.get(pixel, -1)

    # 4. Save results
    result = {
        "components": len(graphs),
        "pixelsProcessed": len(pixel_coordinates),
        "pointsFound": len(points),
        "points": points
    }

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as file:
        json.dump(result, file, indent=4)

    print(f"Saved {len(points)} 3D points to {output_path}", flush=True)


if __name__ == "__main__":
    main()
