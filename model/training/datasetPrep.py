
from pathlib import Path
from collections import defaultdict
from collections import Counter
import json

ROOT = Path(r"F:\CARLA set")

MESH_ROOTS = {
    "github": ROOT / "sets" / "github" / "github",
    "sketchfab": ROOT / "sets" / "Sketchfab" / "hf-objaverse-v1" / "glbs",
}

RENDER_ROOTS = {
    "github": ROOT / "renders" / "github",
    "sketchfab": ROOT / "renders" / "sketchfab",
}

MESH_EXTENSIONS = {
    ".glb",
    ".gltf",
    ".obj",
    ".fbx",
    ".stl",
    ".blend",
    ".dae",
    ".ply",
    ".usdz"
}

def discover_meshes(root):
    meshes = defaultdict(list)

    if not root.exists():
        print(f"Missing mesh directory: {root}")
        return meshes

    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in MESH_EXTENSIONS:
            meshes[path.stem.lower()].append(path)

    return meshes

def discover_renders(root):
    renders = {}

    if not root.exists():
        print(f"Missing render directory: {root}")
        return renders

    for folder in root.iterdir():
        if not folder.is_dir():
            continue

        images = sorted(folder.glob("*.png"))
        arrays = sorted(folder.glob("*.npy"))

        if images:
            renders[folder.name.lower()] = {
                "folder": folder,
                "images": images,
                "arrays": arrays,
            }

    return renders


    for mesh_id in sorted(missing)[:10]:
        print(f"  Unmatched: {mesh_id}")


def inspect_mesh_directory(root):
    extensions = Counter()
    total_files = 0
    duplicate_ids = {}

    meshes = discover_meshes(root)

    for path in root.rglob("*"):
        if path.is_file():
            total_files += 1
            extensions[path.suffix.lower()] += 1

    for mesh_id, paths in meshes.items():
        if len(paths) > 1:
            duplicate_ids[mesh_id] = paths

    print("\n--- Mesh Directory Inspection ---")
    print(f"Directory: {root}")
    print(f"Total files: {total_files}")
    print(f"File extensions: {dict(extensions)}")
    print(f"Unique mesh IDs: {len(meshes)}")
    print(f"Duplicate mesh IDs: {len(duplicate_ids)}")

    for mesh_id, paths in list(duplicate_ids.items())[:5]:
        print(f"\nDuplicate: {mesh_id}")
        for path in paths:
            print(f"  {path}")
            


def create_dataset_index():
    dataset = []

    for source in MESH_ROOTS:
        meshes = discover_meshes(MESH_ROOTS[source])
        renders = discover_renders(RENDER_ROOTS[source])

        for mesh_id, render_data in renders.items():
            if mesh_id not in meshes:
                continue

            mesh_paths = meshes[mesh_id]

            # Skip ambiguous IDs rather than selecting arbitrarily
            if len(mesh_paths) != 1:
                print(f"Ambiguous mesh ID: {mesh_id}")
                continue

            mesh_path = mesh_paths[0]
            image_records = []

            for image_path in render_data["images"]:
                npy_path = image_path.with_suffix(".npy")

                image_records.append({
                    "image_path": str(image_path),
                    "npy_path": (
                        str(npy_path) if npy_path.exists() else None
                    )
                })

            dataset.append({
                "source": source,
                "mesh_id": mesh_id,
                "mesh_path": str(mesh_path),
                "views": image_records
            })

    output_path = ROOT / "sets" / "dataset_index.json"

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(dataset, f, indent=2)

    print(f"\nDataset index saved: {output_path}")
    print(f"Indexed meshes: {len(dataset)}")
    print(f"Indexed images: {sum(len(x['views']) for x in dataset)}")


      
            
def main():
    inspect_mesh_directory(MESH_ROOTS["github"])

    for source in MESH_ROOTS:
        meshes = discover_meshes(MESH_ROOTS[source])
        renders = discover_renders(RENDER_ROOTS[source])

        matched = set(meshes) & set(renders)

        print(f"{source}: {len(matched)} matched meshes")

    create_dataset_index()
        
if __name__ == "__main__":
    main()
