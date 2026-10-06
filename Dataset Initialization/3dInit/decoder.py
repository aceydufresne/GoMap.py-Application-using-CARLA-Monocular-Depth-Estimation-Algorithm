import struct
import json

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


if __name__ == "__main__":
    path = r"F:\CARLA set\sets\Smithsonian\smithsonian\objects\0c671175-1a78-5620-aff7-5d7d3c6af643.glb"
    text = openJson(path)
    print(text)
