import objaverse
import struct
import json

def loadFile(path):
    #decoder logic retrieval
    with open(path, "rb") as file:
        magic = file.read(4)
        version = struct.unpack("<I", file.read(4))[0]
        total_length = struct.unpack("<I", file.read(4))[0]
        
        if magic != b"glTF":
            #entire set is in glb
            print("error")
        json_length = struct.unpack("<I", file.read(4))[0]
        json_type = file.read(4)
        #we need the decoder from the json txt to decode the binary code
        json_bytes = file.read(json_length)
        json_text = json_bytes.decode("utf-8").rstrip("\x00 ")
        gltf = json.loads(json_text)
        
        bin_length = struct.unpack("<I", file.read(4))[0]
        bin_type = file.read(4)
        binary = file.read(bin_length)

    return gltf, binary

#decode elements based on the json elements
def decode(elementType):
    formats = {
        5120: ("b", 1),
        5121: ("B", 1),
        5122: ("h", 2),
        5123: ("H", 2),
        5125: ("I", 4),
        5126: ("f", 4),
    }
    if elementType not in formats:
        raise ValueError(f"Unsupported component type: {elementType}")

    return formats[elementType]

#get the binary type of each element from the json portion
def getType(type):
    type_counts = {
        "SCALAR": 1,
        "VEC2": 2,
        "VEC3": 3,
        "VEC4": 4,
        "MAT2": 4,
        "MAT3": 9,
        "MAT4": 16,
    }
    if type not in type_counts:
        raise ValueError(f"Unsupported accessor type: {type}")

    return type_counts[type]

def accessorMap(gltf, binary, accessorIndx):
    accessor = gltf["accessors"][accessorIndx]
    bufferViewIndx = accessor["bufferView"]
    bufferView = gltf["bufferViews"][bufferViewIndx]
    componentType = accessor["componentType"]
    structFormat, componentSize = decode(componentType)
    componentCount = getType(accessor["type"])
    
    elementSize = componentSize * componentCount
    bufferViewOffset = bufferView.get("byteOffset", 0)
    accessorOffset = accessor.get("byteOffset", 0)
    startOffset = (bufferViewOffset+accessorOffset)
    stride = bufferView.get("byteStride", elementSize)
    count = accessor["count"]
    
    values = []
    
    formatString = ("<"+structFormat*componentCount)
    for i in range(count):
        offset = (startOffset+ i * stride)
        value = struct.unpack_from(formatString, binary, offset)
        values.append(value)
        
    return values

def getVertices(gltf, binary):
    allVerts = []
    tot = 0
    
    for meshIdx, mesh in enumerate(gltf.get("meshes", [])):
        meshName = mesh.get("name", f"Mesh_{meshIdx}")
        primitives = mesh.get("primitives", [])
        
        for primitiveIdx, primitive in enumerate(primitives):
            attributes = primitive.get("attributes", {})
            
            if "POSITION" not in attributes:
                print("error missing values")
                continue
            positionAccessorIdx = (attributes["POSITION"])
            vertices = accessorMap(gltf, binary, positionAccessorIdx)
            
            for vertexIdx, vertex in enumerate(vertices):
                x,y,z = vertex
                allVerts.append({
                    "mesh": meshIdx,
                    "meshName": meshName,
                    "primitive": primitiveIdx,
                    "vertex": vertexIdx,
                    "coordinates": (x,y,z)
                })
            tot += len(vertices)
    return allVerts

def getFaces(gltf, binary):
    allFaces = []
    
    for meshIdx, mesh in enumerate(gltf.get("meshes", [])):
        
        for primitiveIdx, primitive in enumerate(mesh.get("primitives", [])):
            mode = primitive.get("mode", 4)
            idxAccessor = primitive["indices"]
            indices = accessorMap(gltf, binary, idxAccessor)
            
            indices = [value[0] for value in indices]
            for i in range(0, len(indices), 3):
                if i + 2 >= len(indices):
                    break
                face = (indices[i], indices[i+1], indices[i+2])
                allFaces.append({
                    "mesh": meshIdx,
                    "primitive": primitiveIdx,
                    "face": face
                })
    return allFaces
