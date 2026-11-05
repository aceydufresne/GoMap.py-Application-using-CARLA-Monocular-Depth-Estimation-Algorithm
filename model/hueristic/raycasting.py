#print("Testing")
import bpy
import numpy as np
import argparse
import json
import sys

from pathlib import Path
from mathutils import Vector

def raycastEdges(pixel_coordinates, camera_path, width=512, height=512):
    
    RT = np.load(camera_path)

    R = RT[:, :3]
    T = RT[:, 3]

    camera_origin = -R.T @ T

    focal_length = 35.0
    sensor_width = 32.0

    fx = width * focal_length / sensor_width
    fy = fx

    cx = width / 2.0
    cy = height / 2.0

    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()

    points = []

    for x, y in pixel_coordinates:
        direction_camera = np.array([(x + 0.5 - cx) / fx,-(y + 0.5 - cy) / fy,-1.0])
        direction_camera /= np.linalg.norm(direction_camera)

        direction_world = R.T @ direction_camera
        direction_world /= np.linalg.norm(direction_world)

        origin = Vector(camera_origin.tolist())
        direction = Vector(direction_world.tolist())

        hit, location, normal, face_index, obj, matrix = (scene.ray_cast(depsgraph, origin, direction, distance=1000.0))

        if hit:
            points.append({
                "pixel": [int(x), int(y)],
                "xyz": [float(v) for v in location],
                "object": obj.name,
                "face_index": face_index
            })

    return points

def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    parser = argparse.ArgumentParser()
    parser.add_argument("--matches", required=True)
    parser.add_argument("--output", required=True)

    args = parser.parse_args(argv)

    # Read classifier output
    with open(args.matches, "r", encoding="utf-8") as file:
        data = json.load(file)

    matches = data["matches"]

    print(f"Loaded {len(matches)} matched functions", flush=True)

    # Temporary test to verify JSON export
    result = {
        "matchedFunctions": len(matches),
        "points": []
    }

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as file:
        json.dump(result, file, indent=4)

    print(f"Saved output to: {output_path}", flush=True)


if __name__ == "__main__":
    main()
