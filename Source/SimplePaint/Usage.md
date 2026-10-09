# Using SimplePaint

SimplePaint makes it easy to color 3D models in a way that looks good from any angle.

SimplePaint is an unlit, opaque, orthographic-only surface shader. It colors a surface from its normal relative to the camera. Lights, shadows, distance from the camera, and world position do not enter the paint calculation. Rotating a model changes which surfaces face the camera; moving it sideways does not change its paint.

## Controls

| Parameter   | Abstract domain  | Meaning                                                     |
| ----------- | ---------------- | ----------------------------------------------------------- |
| R, G, B     | Each in (0, 1)   | Base color in the existing user-facing sRGB space           |
| Brightness  | (0, 1)           | Positions the base-color anchor on the tone curve           |
| Shift       | [0, 1)           | Warps the facing lobe in the direction selected by Rotation |
| Rotation    | [0, 360) degrees | Circular orientation of the shift                           |
| Dark Point  | [0, 1)           | Tone selected when the warped facing value is zero          |
| Light Point | (0, 1]           | Tone selected when the warped facing value is one           |

Parentheses exclude an endpoint; brackets include it. These are the mathematical domains. This binary32 GPU implementation accepts the following numerical subset, with `m = 1/1024 = 0.0009765625` and `M = 1-m = 0.9990234375`:

| Input              | Accepted finite C++ value |
| ------------------ | ------------------------- |
| Each sRGB channel  | [m, M]                    |
| Brightness         | [m, M]                    |
| Shift              | [0, M]                    |
| Rotation (degrees) | [0, 360)                  |
| Dark Point         | [0, M]                    |
| Light Point        | [m, 1]                    |

The implementation **throws for out-of-range values, NaN, and infinity**. It does not clamp inputs, wrap rotation, or silently repair a material. Values inside the abstract domains but outside these numerical limits also throw. GPU constants are rounded to binary32 after validation and precomputation in binary64. The [specification](Specification.md) explains the accuracy tradeoff.

Start with Shift = 0, Dark Point = 0, and Light Point = 1. The base color appears at **tone = 1 - Brightness**. Increasing Brightness moves that anchor toward the dark end, so more of the surface becomes bright. Brightness is not a multiplier on the RGB values.

Shift = 0 gives symmetric facing shading and makes Rotation irrelevant. Increasing Shift moves the maximum toward a sideways-facing normal. Rotation = 0 shifts toward screen right; 90 toward screen up; 180 toward screen left; 270 toward screen down. This convention uses view axes +X right, +Y up, +Z toward the camera. Rotation is counterclockwise as viewed on screen.

Dark Point and Light Point choose **tones**, not RGB intensities. Their resulting colors depend on the base color and Brightness. Dark Point may exceed Light Point, giving an inverted ramp. Equal endpoints give a constant color. Exact black and white are available as the tone-curve endpoints even though the base-color channels exclude 0 and 1.

There is no positive facing cutoff. Back-facing normals and exact silhouette normals select Dark Point. A front-facing surface approaches that tone continuously as its facing approaches zero. The shader does not flip back-face normals; an application may choose to cull back faces.

## Embed in a C++/DX12 project

Copy the entire [Source/SimplePaint](README.md) folder into your project's source or vendor directory. It includes every C++ and HLSL dependency, its own CMake target, and a short README that travels with the code:

- [Material.h](Material.h) and [Material.cpp](Material.cpp): validated C++20 parameters and GPU constant compilation, with no DirectX dependencies.
- [Geometry.h](Geometry.h): validation for indexed triangle meshes.
- [SimplePaintCore.hlsli](SimplePaintCore.hlsli): the binding-independent HLSL functions.
- [OrthographicTransforms.h](OrthographicTransforms.h) and [SimplePaint.hlsl](SimplePaint.hlsl): DirectXMath transform helper and configurable DX12 vertex/pixel shader adapter.

Keep the copied folder named `SimplePaint` and use its CMake target:

```cmake
add_subdirectory(vendor/SimplePaint)
target_link_libraries(MyApp PRIVATE SimplePaint)
```

Without CMake, compile the copied `Material.cpp` as C++20 and add the folder's parent directory to your C++ include paths. Only the optional transform helper needs DirectXMath from the Windows SDK. The host owns DXC compilation and GPU resource setup. Compile each material once when its settings change:

```cpp
#include "SimplePaint/Material.h"

const SimplePaint::Parameters parameters{
    .baseColorSrgb = {0.107, 0.223, 0.578},
    .brightness = 0.5,
    .shift = 0.6,
    .rotationDegrees = 45.0,
    .darkPoint = 0.05,
    .lightPoint = 0.95,
};

const auto material = SimplePaint::Material::Compile(parameters);
const SimplePaint::GpuMaterial gpu = material.Constants(); // Own a copy for upload.
// memcpy(mappedMaterialBuffer, &gpu, sizeof(gpu));
```

Catch `std::invalid_argument` at your UI or loading boundary and display its message. Keep a previously compiled material if an edit fails. Do not mutate GPU constants after compilation. `Constants()` returns a reference owned by its `Material`; copy it before that object goes out of scope.

Each material occupies **80 bytes**, aligned to 16 bytes: five consecutive HLSL `float4` registers. A material array has an 80-byte stride, not a 256-byte stride. Align the start of the containing DX12 constant buffer to 256 bytes; round its allocation/CBV size up to a multiple of 256. Six materials use 480 bytes in a 512-byte allocation. Respect GPU fence ownership when updating an in-flight buffer.

In your vertex shader, transform the normal to the view frame and call `SimplePaintRotateNormal`. Pass its result using `noperspective`, and pass the material index using `nointerpolation`. Call `SimplePaintShade` in the pixel shader:

```hlsl
#include "SimplePaintCore.hlsli"

// In VS, after constructing your view-space normal:
// output.paintNormal = SimplePaintRotateNormal(viewNormal, material);
// In PS, after fetching the same material:
// float3 linearRgb = SimplePaintShade(input.paintNormal, material);
// return float4(linearRgb, 1.0f);
```

The included DX12 adapter uses `b0` for the 96-byte packed object transforms, `b1` for the material array, and vertex attributes POSITION (`float3`), NORMAL (`float3`), MATERIAL (`uint`). Its input layout uses byte offsets 0, 12, and 24, for a 28-byte vertex. Define `SIMPLE_PAINT_MATERIAL_COUNT` when compiling both shader stages and allocate the same number of C++ material records. The shader defaults to one material; this application's CMake configures six and checks the renderer's count with a C++ static assertion.

Use an orthographic projection with affine object/view transforms and clip W = 1. `Orthographic::MakeProjection` constructs a right-handed orthographic projection, and `BuildObjectTransforms` constructs the columns consumed by the example VS. The helper rejects perspective, shear, nonuniform scale, singular scale, nonfinite values, and out-of-range transforms. Its supported uniform scale is [1/1024, 1024]. A different normal-transform adapter is your project's responsibility; the provided adapter only supports those validated transforms.

Before uploading geometry, call `SimplePaint::ValidateMesh<Vertex>(vertices, indices, materialCount)`. It accepts spans of vertices and 32-bit indices. Vertices expose float `positionX/Y/Z`, `normalX/Y/Z`, and an unsigned `materialIndex`; adapt your vertex layout or validate equivalent data before upload. Use normal lengths in [0.5, 2], positions within [-1,000,000, 1,000,000], valid indices, one material per triangle, and the common normal cone described in the specification. The validation throws if interpolation could approach a zero normal under that contract. The GPU core assumes these checks have succeeded.

Compile the HLSL entry points as `vs_6_0` and `ps_6_0`, with DXC `-O3 -Ges -WX`, adding `-D SIMPLE_PAINT_MATERIAL_COUNT=N` for your material count. Track `SimplePaintCore.hlsli` as a shader build dependency. This repository additionally uses `-all_resources_bound`, embeds the compiled DXIL in the executable, and strips debug/reflection data. Bind all resources before drawing.

The shader returns **linear RGB**, with alpha 1 supplied by the adapter. Encode it to sRGB once for display, for example with an `R8G8B8A8_UNORM_SRGB` render-target view. Do not apply a second gamma function in the pixel shader. For HDR or a later composite pass, retain linear values and let that pipeline perform the final display conversion.

## Build and verify

From a Visual Studio x64 developer shell with Windows SDK DXC available, run
these commands from the copied SimplePaint folder (CMake 3.24 or newer):

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Use a separate build directory with `-DCMAKE_BUILD_TYPE=Debug` for Debug checks.
Tests require Windows, DirectXMath, DXC, and a Shader Model 6.0 runtime.
Library-only builds can set `-DBUILD_TESTING=OFF`. When embedded with
`add_subdirectory`, tests default off; enable them with
`-DSIMPLE_PAINT_BUILD_TESTING=ON`.

The tests include 81,940 curve comparisons, 20,000 comparisons with the original K12 construction, invalid-input checks, mesh/transform checks, and 30,208 actual production VS/PS samples on each of the preferred adapter and WARP. The GPU tests include 4,096 interpolated-normal cases and check both linear and sRGB error against a binary64 reference. They also check DX12 debug-layer warnings/errors when the debug layer is installed. The suite's detailed scope and measured results are in [Specification.md](Specification.md).

The `SimplePaintStandalone` test additionally copies just the module into a fresh consumer project, compiles and runs all its C++ interfaces, and compiles both shader stages with the default count of one and an explicit count of three. No application sources are included in that copied project. A zero material count must fail shader compilation.

The copy check also builds and runs the copied module's CPU, transform, and GPU tests. Game-specific car/sphere validation remains in the original host's integration suite.
