import struct
import json
import sys
import bpy
from pathlib import Path
import subprocess
import os

blender_path = r"C:\Program Files\Blender Foundation\Blender 3.3\blender.exe"
decoder_script = r"F:\CARLA set\rendering\mesh_decoder.py"
coordinates = r"F:\CARLA set\sets\coordinates"

def openJson(path):
    text = " "
    with open(path, "rb") as file:
        magic = file.read(4)
        version = struct.unpack("<I", file.read(4))[0]
        totalLength = struct.unpack("<I", file.read(4))[0]
        
        if magic != b"glTF":
            #if this isn't the header
            raise ValueError("Error sorting")
        else:
            jsonLength = struct.unpack("<I", file.read(4))[0]
            jsonType = file.read(4)
            #how long are the isntructions
            jsonBytes = file.read(jsonLength)
            #encoder informationL
            jsonText = jsonBytes.decode("utf-8").rstrip("\x00")
            gltf = json.loads(jsonText)
            text = jsonText
    return text

def import_model(path):
    ext = path.suffix
    vertices = subprocess.run([
        blender_path,
        "--background",
        "--python",
        decoder_script,
        "--",
        "-mp",
        path], check = True)
    export(path, vertices)

def export(mesh_path):
    mesh_name = os.path.splitext(os.path.basename(mesh_path))[0]
    output_path = os.path.join(coordinates,mesh_name + ".npz")
    
    
    return output_path
    




if __name__ == "__main__":
    path = r"F:\CARLA set\sets\Smithsonian\smithsonian\objects\0c671175-1a78-5620-aff7-5d7d3c6af643.glb"
    #text = openJson(path)
    import_model(path)
