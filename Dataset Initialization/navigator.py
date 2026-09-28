import os
import subprocess
import hashlib

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
        
if __name__ == "__main__":
    navigator()
