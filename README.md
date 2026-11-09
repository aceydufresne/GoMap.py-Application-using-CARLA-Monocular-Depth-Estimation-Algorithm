# GoMap.py-Application-using-CARLA-Monocular-Depth-Estimation-Algorithm

## Overview

GoMap.py is a computer vision and 3D reconstruction research project built around **CARLA (Compositing And Relighting Algorithm)**.

The project aims to reconstruct 3D geometry from monocular images and video without requiring LiDAR sensors.

The system combines Python-based deep learning models with C++ computer vision algorithms to extract image features, estimate 3D structure, and generate or complete point clouds.

## System Requirements

- **Operating System:** Windows 10/11 or Ubuntu 24.04
- **GPU:** NVIDIA CUDA-compatible GPU recommended
- **Python:** Python 3.12 (model training environment)
- **C++:** C++17 or newer
- **Build Tools:** GCC/G++ or MSYS2 UCRT64
- **GPU Acceleration:** CUDA-compatible PyTorch installation

## Python Dependencies

The Python components support dataset preparation, neural network training, model evaluation, image processing, and 3D reconstruction.

### Core Libraries

- `numpy` — Numerical operations and array processing
- `scipy` — Scientific computing and spatial algorithms
- `torch` — Neural network training and inference
- `torchvision` — Image transformations and pretrained vision encoders
- `matplotlib` — Training visualization and evaluation
- `Pillow` — Image loading and manipulation
- `tqdm` — Training and processing progress indicators

### 3D Processing and Machine Learning

- `open3d` — Point cloud processing and visualization
- `trimesh` — Mesh loading and geometric processing
- `transformers` — Transformer-based model components, where applicable
- `diffusers` — Diffusion-model utilities, where applicable
- `sentence-transformers` — Embedding-based similarity workflows, where applicable

### Point Cloud Training Dependencies

Depending on the model configuration:

- `torch-geometric`
- `torch-cluster`
- `torch-scatter`
- `torch-sparse`

These packages may require builds compatible with the installed PyTorch and CUDA versions.

### Additional Tools

- **Blender** — Mesh rendering, camera simulation, and raycasting
- **bpy** — Blender's Python API, normally provided by Blender
- **CUDA Toolkit** — GPU compilation support where required by the selected model

## C++ Dependencies

The C++ pipeline performs edge detection, graph construction, geometric function fitting, and feature matching.

### Required Libraries

- **OpenCV 5.0** — Image processing and computer vision
- **OpenCV ximgproc** — Skeletonization and thinning
- **nlohmann/json** — JSON serialization and parsing
- **C++17 Standard Library** — Filesystem operations, vectors, variants, algorithms, and other utilities

### OpenCV Modules

- `opencv_core`
- `opencv_imgproc`
- `opencv_imgcodecs`
- `opencv_highgui`
- `opencv_ximgproc`

### Development Tools

- MSYS2 UCRT64
- GCC/G++ compiler
- pkg-config
- GDB (optional debugging)

## C++ Installation (Windows / MSYS2)

Open the MSYS2 UCRT64 terminal.

Install the compiler and required libraries:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-opencv \
  mingw-w64-ucrt-x86_64-nlohmann-json \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-gdb
```

Verify the OpenCV installation:

```bash
pkg-config --modversion opencv5
```

### Compile the Edge Encoder

Navigate to the C++ source directory:

```bash
cd "/f/CARLA set/model"
```

Compile:

```bash
g++ -std=c++17 edgeEncoder.cpp \
  -o edgeEncoder.exe \
  $(pkg-config --cflags --libs opencv5)
```

Run:

```bash
./edgeEncoder.exe
```

For applications with multiple source files, compile and link all required `.cpp` files together.

## Python Environment Setup

Create a virtual environment:

```bash
python3.12 -m venv hunyuan-env
```

Activate it on Ubuntu:

```bash
source hunyuan-env/bin/activate
```

Activate it on Windows PowerShell:

```powershell
.\hunyuan-env\Scripts\Activate.ps1
```

Install a PyTorch build compatible with your NVIDIA driver and CUDA environment, then install the remaining project dependencies:

```bash
python -m pip install numpy scipy matplotlib pillow tqdm
python -m pip install torchvision open3d trimesh
```

Install additional machine learning packages only for the scripts that require them. PyTorch Geometric extensions must match the installed PyTorch/CUDA build.

## Processing Pipeline

1. **Image Input:** Load monocular image or video frames.
2. **Feature Extraction:** Detect image edges using Sobel filtering and skeletonization.
3. **Graph Construction:** Convert connected edge pixels into graph structures and paths.
4. **Geometric Encoding:** Approximate contours using linear functions and Bézier curves.
5. **Feature Matching:** Compare encoded features with indexed dataset patterns.
6. **3D Coordinate Recovery:** Use reference mesh geometry, camera projection, and raycasting during dataset preparation.
7. **Point Cloud Generation:** Predict or reconstruct partial 3D geometry.
8. **Point Cloud Completion:** Use learned reconstruction models to infer missing geometry.
9. **Texture Reconstruction:** Project visible textures and estimate occluded surface appearance.

## Research Status

This project is under active development. Individual model components, training scripts, and dependencies may change as experiments progress.

The goal is to develop a modular monocular 3D reconstruction pipeline that combines geometric feature encoding with neural point cloud reconstruction.
