import argparse
import subprocess
from pathlib import Path

rootPath = Path(r"F:\CARLA set")
model = rootPath / "model"
encoder = model / "edgeEncoder.exe"
classifier = model / "classifier.exe"
blenderPath = Path(r"C:\Program Files\Blender Foundation\Blender 3.3\blender.exe")
raycast = model / "raycasting.py"
output = model / "output"

def start(command):
    print("\nRunning:", " ".join(map(str, command)))
    subprocess.run([str(arg) for arg in command], cwd = str(model), check = True)
    
def runPipeline(imagePath):
    imagePath = Path(imagePath).resolve()
    if not imagePath.is_file():
        raise FileNotFoundError(imagePath)
    output.mkdir(parents=True, exist_ok=True)
    
    matchesJson = output / "bpy_matches.json"
    pointsJson = output / "points_3d.json"

    print("Encoding and classifying image...")

    start([classifier, "--input", imagePath, "--output", matchesJson])
    
    print("Reconstructing 3D coordinates...")

    start([blenderPath, "--background", "--python", raycast, "--", "--matches", matchesJson, "--output", pointsJson])
    if not pointsJson.is_file():
        raise RuntimeError(
        f"Blender finished without creating: {pointsJson}"
    )
    print("3D point cloud generated successfully!")
    print("Pipeline complete!")
    print("3D coordinates saved to:", pointsJson)
    
if __name__ == "__main__":
    #parser = argparse.ArgumentParser()
    #parser.add_argument("image", help="Path to input image")
    #args = parser.parse_args()
    #runPipeline(args.image)
    imagePath = r"F:\CARLA set\renders\github\b684b5a1e0c38b6c1186c934f5d59fe4ed2afcf92c7d8329f0b837e880e30272\011.png"
    runPipeline(imagePath)
