import os
import subprocess
import hashlib
from pathlib import Path
import numpy as np
import json

def navigator():
    #main directory
    rootdir = 'F:\CARLA set\sets'
    blenderPath = (r"C:\Program Files\Blender Foundation\Blender 3.3\blender.exe")
    blenderScript = (r"F:\CARLA set\rendering\blender_script.py")
    outputPath = (r"F:\CARLA set\renders\test")
    
    #you can also do this with os walk
    subroutes = ['F:\CARLA set\sets\Sketchfab\hf-objaverse-v1\glbs', 'F:\CARLA set\sets\Smithsonian\smithsonian\objects', 'F:\CARLA set\sets\github\github']
    #for sub in subroutes:
        #for files in sub:
         #   continue
         
    for file in os.listdir('F:\CARLA set\sets\github\github'):
        filePath = os.path.join('F:\CARLA set\sets\github\github', file)
        fileName = hashlib.sha256()
        
        with open(filePath, "rb") as file:
            for chunk in iter(lambda: file.read(1024 * 1024), b""):
                fileName.update(chunk)
        id = fileName.hexdigest()
        renderPath = os.path.join(outputPath, id)
        os.makedirs(renderPath, exist_ok= True)
        subprocess.run([blenderPath, "--background", "--python", blenderScript, "--", "--object_path", filePath, "--output_dir", renderPath, "--num_renders", "12"], check = True)
        
        #iterate through directories and pass file names
def feedFile():
    coordinate_git = r"F:\CARLA set\sets\coordinates\github"
    coordinate_sketch = r"F:\CARLA set\sets\coordinates\sketchfab"
    coordinate_smith = r"F:\CARLA set\sets\coordinates\smithsonian"
    blender_path = r"C:\Program Files\Blender Foundation\Blender 3.3\blender.exe"
    decoder_script = r"F:\CARLA set\initialization\coordinateInit.py"
    #stored in independent files
    gitSet = r'F:\CARLA set\sets\github\github'
    #collections in subfolders
    sketchSet = r'F:\CARLA set\sets\Sketchfab\hf-objaverse-v1\glbs'
    #stored in independent files
    smithSet = r'F:\CARLA set\sets\Smithsonian\smithsonian\objects'
    #empty right now
    thingSet = r'F:\CARLA set\sets\Thingiverse\thingiverse'
    
    count = 0
    gitArray = []
    sketchArray = []
    smithArray = []
    #working 10/05
    for file in os.listdir(gitSet):
        #print(file)
        fullPath = os.path.join(gitSet, file)
        gitArray.append(fullPath)
    #working 10/05
    for subdir, dirs, files in os.walk(sketchSet):
        for file in files:
            fullPath = os.path.join(subdir, file)
            sketchArray.append(fullPath)
            #print(file)
        #working 10/05
    for file in os.listdir(smithSet):
        #print(file)
        fullPath = os.path.join(smithSet, file)
        smithArray.append(fullPath)
        
        
    #create a text file to store coordinate adata
    #pass each array to the blender_script
    for mesh in gitArray:
        
        result = subprocess.run([blender_path, "--background", "--python", decoder_script, "--", "--model_path", mesh], capture_output=True, text=True, encoding="utf-8", errors = "replace", check=True)
        vertices = None
        for line in result.stdout.splitlines():
            #segregates the normal blender output from json data
            if line.startswith("VERTEX_DATA:"):
                vertices = json.loads(line[len("VERTEX_DATA:"):])
                break
        if vertices is None:
            print("error handling files")
            continue
        vertices = np.array(vertices)
        #hash from initial encoding script
        id = os.path.splitext(os.path.basename(mesh))[0]
        coordinateFile = os.path.join(coordinate_git,id+".npy")
        np.save(coordinateFile,vertices)
    
    for mesh in smithArray:
        
        result = subprocess.run([blender_path, "--background", "--python", decoder_script, "--", "--model_path", mesh], capture_output=True, text=True, encoding="utf-8", errors = "replace", check=True)
        vertices = None
        for line in result.stdout.splitlines():
            #segregates the normal blender output from json data
            if line.startswith("VERTEX_DATA:"):
                vertices = json.loads(line[len("VERTEX_DATA:"):])
                break
        if vertices is None:
            print("error handling files")
            continue
        vertices = np.array(vertices)
        #hash from initial encoding script
        id = os.path.splitext(os.path.basename(mesh))[0]
        coordinateFile = os.path.join(coordinate_smith,id+".npy")
        np.save(coordinateFile,vertices)
        
    for mesh in sketchArray:
        result = subprocess.run([blender_path, "--background", "--python", decoder_script, "--", "--model_path", mesh], capture_output=True, text=True, encoding="utf-8", errors = "replace", check=True)
        vertices = None
        for line in result.stdout.splitlines():
            #segregates the normal blender output from json data
            if line.startswith("VERTEX_DATA:"):
                vertices = json.loads(line[len("VERTEX_DATA:"):])
                break
        if vertices is None:
            print("error handling files")
            continue
        vertices = np.array(vertices)
        #hash from initial encoding script
        id = os.path.splitext(os.path.basename(mesh))[0]
        coordinateFile = os.path.join(coordinate_sketch,id+".npy")
        np.save(coordinateFile,vertices)
        
        
if __name__ == "__main__":
    #navigator()
    #decoder_script = r"F:\CARLA set\initialization\coordinateInit.blend"
    feedFile()
