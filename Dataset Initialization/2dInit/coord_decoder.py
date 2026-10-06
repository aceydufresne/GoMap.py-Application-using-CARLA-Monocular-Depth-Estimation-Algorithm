import bpy
import sys
import os
import argparse
import json


def get_arguments():
    parser = argparse.ArgumentParser()

    parser.add_argument("-mp","--model_path",required=True)
    argv = sys.argv

    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []

    return parser.parse_args(argv)


def import_mesh(path):

    extension = os.path.splitext(path)[1].lower()

    if extension in [".glb", ".gltf"]:
        bpy.ops.import_scene.gltf(filepath=path)

    elif extension == ".fbx":
        bpy.ops.import_scene.fbx(filepath=path)

    elif extension == ".obj":
        bpy.ops.import_scene.obj(filepath=path)
    
    elif extension == ".stl":
        bpy.ops.import_mesh.stl(filepath=path)

    else:
        raise ValueError(f"Unsupported file type: {extension}")


def find_data(path):
    vertices = []
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()

    import_mesh(path)

    for obj in bpy.context.scene.objects:

        if obj.type != "MESH":
            continue

        print("Object:", obj.name)

        for i, vertex in enumerate(obj.data.vertices):

            v_local = vertex.co

            v_world = obj.matrix_world @ v_local

            vertices.append([
                i,
                v_world.x,
                v_world.y,
                v_world.z
            ])
    return vertices


if __name__ == "__main__":

    args = get_arguments()
    vertices = find_data(args.model_path)
    print("VERTEX_DATA:" + json.dumps(vertices))
