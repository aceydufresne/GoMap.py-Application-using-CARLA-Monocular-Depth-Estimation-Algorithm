import open3d as o3d
import numpy as np
import matplotlib.pyplot as plt
import os
import sys
from pathlib import Path




def loadCloud():
    inputs = Path(r"F:\CARLA\model\pointr++\inference_results")
    outputs = inputs / "bpa_meshes"
    #find predicted point clouds
    files = [cloud for cloud in inputs.iterdir()
             if cloud.suffix.lower() in (".npy", ".ply", ".pcd") and cloud.is_file()]
    #loading pointclouds
    for file in files:
        print("\nTesting : {file.name}")
        
        if file.suffix.lower() == ".npy":
            points = np.load(file)
            if points.ndim != 2 or points.shape[1] != 3:
                print("testing skipped")
            pcd = o3d.geometry.PointCloud()
            pcd.points = o3d.utility.Vector3dVector(points.astype(np.float64))
        else:
            pcd = o3d.io.read_point_cloud(str(file))
        if len(pcd.points) < 3:
            print("testing skipped")
            continue
        #create direction in the vectors, simulating a triagular mesh
        pcd.estimate_normals(search_param=o3d.geometry.KDTreeSearchParamKNN(knn=min(30,len(pcd.points))))
        pcd.orient_normals_consistent_tangent_plane(min(30, len(pcd.points) - 1))
        #find the spacing between vectors
        distances = np.asarray(pcd.compute_nearest_neighbor_distance())
        avg = np.mean(distances)
        radii = [avg * 1.5, avg * 2.0, avg * 3.0, avg * 4.0]
        print("points:", len(pcd.points))
        print("average spacing:", avg)
        print("radius:", radii)

        #open3d function using a ball to connect three vectors into 1 face
        mesh = o3d.geometry.TriangleMesh.create_from_point_cloud_ball_pivoting(pcd, o3d.utility.DoubleVector(radii))
        mesh.compute_vertex_normals()
        print("Mesh vertices:", len(mesh.vertices))
        print("Mesh triangles:", len(mesh.triangles))
        print("Watertight:", mesh.is_watertight())
        output_path = outputs / f"{file.stem}_bpa.obj"

        if len(mesh.triangles) > 0:
            o3d.io.write_triangle_mesh(str(output_path), mesh)
            print("saved:", output_path)
            o3d.visualization.draw_geometries([mesh], window_name = f"BPA - {file.stem}", mesh_show_back_face = True)
        else:
            print("no mesh built")

if __name__ == "__main__":
    loadCloud()
