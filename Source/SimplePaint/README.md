# SimplePaint shader

This folder contains the complete reusable C++20/HLSL implementation. Copy the **entire `SimplePaint` folder** into another project's source or vendor directory. It has no dependency on this application's renderer, generated meshes, settings loader, background shader, tests, or build scripts.

## Build the C++ library

With CMake, keep the folder name `SimplePaint` and add:

```cmake
add_subdirectory(vendor/SimplePaint)
target_link_libraries(MyApp PRIVATE SimplePaint)
```

Include headers as `#include "SimplePaint/Material.h"`. The target provides the parent include directory and requires C++20. Without CMake, compile `Material.cpp` and add the copied folder's parent to your include paths. `Material.h`, `Material.cpp`, and `Geometry.h` use only the standard library. `OrthographicTransforms.h` additionally requires DirectXMath, available in the Windows SDK for DX12 projects. The CMake target does not compile shaders automatically; the host owns shader compilation, bindings, and GPU resources.

## Compile a material

```cpp
#include "SimplePaint/Material.h"

const SimplePaint::Parameters p{
    .baseColorSrgb = {0.107, 0.223, 0.578},
    .brightness = 0.5,
    .shift = 0.6,
    .rotationDegrees = 45,
    .darkPoint = 0.05,
    .lightPoint = 0.95,
};
const auto material = SimplePaint::Material::Compile(p);
const SimplePaint::GpuMaterial gpu = material.Constants(); // Copy before material dies.
```

All values must be finite. With `m = 1/1024` and `M = 1-m`, RGB and Brightness accept [m,M], Shift and Dark Point accept [0,M], Light Point accepts [m,1], and Rotation accepts [0,360) degrees. Invalid inputs throw `std::invalid_argument`; no values are clamped or wrapped. Reversed and equal tone endpoints are valid. Base RGB is sRGB; the shader output is linear RGB. The base color appears at tone `1-Brightness`.

## Compile and bind the shaders

The supplied `SimplePaint.hlsl` contains complete `VSMain` and `PSMain` entry points. From a Windows SDK developer shell, for a host with three materials:

```text
dxc -E VSMain -T vs_6_0 -O3 -Ges -WX -D SIMPLE_PAINT_MATERIAL_COUNT=3 -Fo PaintVS.dxil vendor/SimplePaint/SimplePaint.hlsl
dxc -E PSMain -T ps_6_0 -O3 -Ges -WX -D SIMPLE_PAINT_MATERIAL_COUNT=3 -Fo PaintPS.dxil vendor/SimplePaint/SimplePaint.hlsl
```

Define the same positive material count for both stages and upload exactly that many material records. The default is one. Track both `SimplePaint.hlsl` and `SimplePaintCore.hlsli` as build dependencies. You can also include `SimplePaintCore.hlsli` in your own entry points; it has no fixed bindings or material count.

- `b0`: `Orthographic::ObjectTransforms`, 96 bytes, built with `MakeProjection` and `BuildObjectTransforms` from `OrthographicTransforms.h`.
- `b1`: a tightly packed array of `SimplePaint::GpuMaterial`, 80 bytes per material. Align each buffer's start to 256 bytes and round CBV allocation sizes up to 256 bytes; do not add padding between array elements.
- Vertex input: POSITION (`float3`, offset 0), NORMAL (`float3`, offset 12), MATERIAL (`uint`, offset 24); stride 28 bytes. Every triangle has one material index, in range.
- Use orthographic projection, with +X right, +Y up, and +Z toward the camera. Rotation 0 shifts right, 90 up. Normals interpolate with `noperspective`; the material index uses `nointerpolation`. If writing your own VS, rotate each normal once with `SimplePaintRotateNormal`, then call `SimplePaintShade` on the interpolated result in PS.
- Output is opaque linear RGB. Encode to sRGB exactly once for display, such as through an sRGB render-target view. No lights, textures, or samplers are required.

## Validate geometry and transforms

Call `SimplePaint::ValidateMesh<Vertex>` before upload. It accepts spans of vertices and 32-bit indices. The host vertex exposes `positionX/Y/Z`, `normalX/Y/Z`, and `materialIndex`. Positions must be finite with component magnitudes <= 1e6; normal lengths must be in [0.5,2]. Indices must describe a nonempty triangle list, with one valid material per triangle. Every triangle normal must project at least 0.125 onto the normalized sum of its three normals, ensuring interpolation never reaches zero. Validation throws instead of repairing a mesh.

The provided transform helper accepts finite orthographic dimensions in [1e-4,1e6], `0 <= near < far <= 1e6`, and a depth span >= 1e-4. Object/view transforms must be affine, bounded, and use uniform scale in [1/1024,1024]. Perspective, shear, singular scale, and nonuniform scale are rejected. A host supplying its own transforms must enforce equivalent nonzero, bounded normal conditions. Back-facing and exact silhouette normals select Dark Point; there is no positive facing cutoff.

## Files

| File                      | Purpose                                            |
| ------------------------- | -------------------------------------------------- |
| Material.h / Material.cpp | Parameters, validation, coefficients, GPU ABI      |
| Geometry.h                | Host-independent mesh validation                   |
| OrthographicTransforms.h  | Validated DirectXMath transforms for the sample VS |
| SimplePaintCore.hlsli     | Reusable paint math without bindings               |
| SimplePaint.hlsl          | Configurable DX12 VS/PS adapter                    |
| CMakeLists.txt            | C++ library target, usable after copying           |

The paint mathematics reimplement Kevin Vu's [K12 Simple Paint shader](https://github.com/kvu787/SimplePaintShaders/blob/793126205e028f06f635f23e87a9bac856bf669a/Godot/ShaderTest/Shaders/K12.gdshader). The included [usage guide](Usage.md), [specification](Specification.md), and [historical numerical reports](Reports/README.md) travel with the code. [Build and verification instructions](Usage.md#build-and-verify) run the included tests without application sources or build scripts.

`ValidateParameters(parameters)` checks the material domain without computing GPU
constants. `Material::Compile` also calls it before computing constants. Both APIs
are plain C++ and independent of JSON or application settings.
