# SimplePaint specification and rationale

Status: implemented. Date: 2026-09-05. This document supersedes the removed working specification and the earlier numerical proposals in [Historical reports](Reports/README.md).

## Purpose and abstract interface

SimplePaint makes it easy to color 3D models in a way that looks good from any angle.

| Parameter   | Abstract domain  | Meaning                                                     |
| ----------- | ---------------- | ----------------------------------------------------------- |
| R, G, B     | Each in (0, 1)   | Base color in the existing user-facing sRGB space           |
| Brightness  | (0, 1)           | Positions the base-color anchor on the tone curve           |
| Shift       | [0, 1)           | Warps the facing lobe in the direction selected by Rotation |
| Rotation    | [0, 360) degrees | Circular orientation of the shift                           |
| Dark Point  | [0, 1)           | Tone selected when the warped facing value is zero          |
| Light Point | (0, 1]           | Tone selected when the warped facing value is one           |

All parameters are finite. There is no ordering constraint on Dark Point and Light Point. Reversed ranges are valid; equal endpoints inside (0, 1) produce constant color. Rotation has a canonical interval: 360 degrees is rejected even though its direction is mathematically equivalent to zero.

The surface is opaque and unlit. The output is linear RGB, with alpha 1 in the application adapter. Paint depends only on the material and the oriented surface normal in the orthographic camera frame. It has no light, camera-position, perspective, texture, or distance inputs.

## Normals and the camera frame

The frame is right-handed view space: +X points right, +Y up, and +Z toward the camera. The input normal is rotated by **negative Rotation** about Z. Equivalently, the lobe direction rotates counterclockwise on screen as the user increases Rotation.

For the equations below, let `(x,y,z)` denote that rotated, unit-length normal. The implementation can use an unnormalized vector because its length cancels from the homogeneous expressions described later. It must still be finite and nonzero.

Projection is exclusively orthographic. The example VS computes clip XYZ with three four-component dot products and sets clip W to one. Normals interpolate with `noperspective`. Each triangle has one material, and its material index interpolates with `nointerpolation`. A constant material rotation is linear and commutes with interpolation, so it is applied in VS instead of PS.

## Real-number reference mathematics

All formulas in this section use exact real-number arithmetic. They define appearance independently of the finite-precision implementation.

### Decode the base color

For each user-facing sRGB channel `C`, decode the linear channel `c`:

```text
c = C / 12.92                         when C <= 0.04045
c = ((C + 0.055) / 1.055)^2.4        otherwise
```

This preserves the original Godot `source_color` interpretation and the existing DX12 application's display pipeline. The output is encoded to sRGB once at presentation, by the application's sRGB render-target view.

### Warp the facing lobe

Let `s = Shift`:

```text
r = sqrt(x*x + z*z)
h = sqrt((1-s)*(1+s))

f = 0                              when z <= 0
f = r*z*h / (r-s*x)                 when z > 0
```

On the front hemisphere, `r > 0`, and `r-s*x >= (1-s)*r > 0`. Thus there is no singular denominator in this branch. The facing satisfies `0 < f <= r <= 1`.

This is the algebraic reduction of the original K12 sequence:

```text
sliceStart = -r
sliceEnd   =  r
u1 = (1+s)/2
u2 = (1-s)/2
v  = (x+r)/(2*r)
w  = u2*v / (u1*(1-v) + u2*v)       Schlick bias
X  = -r + 2*r*w
f  = sqrt(r*r-X*X)                  front hemisphere
```

Both the circular slice and the Schlick bias are retained. For `s=0`, `f=z` on the front hemisphere. In the rotated frame the maximum is attained at `(s,0,h)`, where `f=1`. Rotation therefore selects the direction of the sideways shift. The original source was read from the local `SimplePaintShaders` repository at commit `abdf14856696f6437224abf3e20230333a58aae0`, in `Godot/ShaderTest/Shaders/K12.gdshader` and `Inc/Schlick.gdshaderinc`. The [published K12 source](https://github.com/kvu787/SimplePaintShaders/blob/793126205e028f06f635f23e87a9bac856bf669a/Godot/ShaderTest/Shaders/K12.gdshader) identifies the original construction and authorship (Kevin Vu).

### Cutoff decision

The former rule `z < 0.01` is removed. For every fixed `s < 1`, the front-facing formula approaches zero continuously as `z` approaches zero. The bound `f <= r` also handles the two Y-axis slice poles. Define the entire back hemisphere, including `z=0`, as `f=0`; evaluate that branch before any slice arithmetic.

A positive cutoff was an artistic discontinuity and did not resolve the underlying numerical problems near the shifted peak. Those problems are addressed by the evaluation method, not by discarding a band of front-facing normals. There is no configurable cutoff or shader epsilon.

The complete mathematical paint function is continuous in the normal, including the silhouette, for a fixed interior material. It is generally **not differentiable across the front/back boundary**, since the back hemisphere is constant. The core color curve is smooth; the complete shader must not be described as globally smooth.

### Remap tone and evaluate the color curve

Let `p = Brightness`, `d = Dark Point`, and `l = Light Point`. For each linear base-color channel `c`:

```text
t = d*(1-f) + l*f
A = c*p
B = (1-c)*(1-p)
F(t) = A*t / (A*t + B*(1-t))
```

`A` and `B` are strictly positive. The denominator is positive for the full closed tone interval [0,1]. This is the original anchored Schlick curve because its denominator expands to `(c-(1-p))*t + (1-p)*(1-c)`.

```text
F(0)   = 0
F(1)   = 1
F(1-p) = c
F'(t)  = A*B / (A*t+B*(1-t))^2 > 0
```

The base color anchors the curve at tone `1-p`. Increasing Brightness moves that anchor toward zero; it does not multiply the output. Tone remapping preserves endpoint colors `F(d)` and `F(l)`, including inverted or constant ranges. Dark Point and Light Point are input tones, not displayed luminance values.

## Accepted machine inputs

The reference's open domains permit values arbitrarily close to singular corners in their closure. A fixed binary32 implementation needs a separate numerical contract. This implementation chooses the exactly representable margin `m = 2^-10 = 0.0009765625` and `M = 1-m = 0.9990234375`.

| Parameter   | Numerical domain |
| ----------- | ---------------- |
| R, G, B     | Each in [m, M]   |
| Brightness  | [m, M]           |
| Shift       | [0, M]           |
| Rotation    | [0, 360) degrees |
| Dark Point  | [0, M]           |
| Light Point | [m, 1]           |

This is a deliberate supported subset, not a modification of the abstract equations. The margin is an engineering choice that bounds both the lobe's concentration and the extreme color curves, gives simple identical UI limits, and meets the tested error criterion below. It is **not claimed to be the smallest possible margin**. No bound was chosen to keep an old denominator floor inactive; no such floor exists now. Narrow smooth highlights and finite image sampling are not used to justify these limits or to introduce filtering requirements.

The smallest linear base color is approximately `7.56e-5`; the smallest `A` is approximately `7.38e-8`. Merely using positive curve coefficients is insufficient: a rounded facing value near one can still erase the tiny quantity `1-f` and significantly alter such a curve. The implementation retains that quantity independently.

`Parameters` uses C++ `double`. `Material::Compile` checks every field, including finite status, **before conversion or coefficient computation**, and throws `std::invalid_argument` naming the offending parameter. Values just outside a limit must not round into the accepted interval. Negative zero is accepted where zero is allowed. Equal and reversed tone endpoints are accepted.

Serialization and user-interface parsing belong to the host. Validate parsed binary64 parameters before conversion to GPU constants; SimplePaint has no JSON dependency.

Coefficients, trig, square roots, and sRGB conversion are calculated in binary64 and then stored in binary32. This ordinary rounding is part of the implementation. Extremely small positive Shift or Dark Point can have a coefficient contribution below binary32 resolution, which does not imply a material-validation failure. Abstractly excluded endpoints are never admitted, and no requested input is clamped or angle wrapped.

## Stable GPU evaluation

### Homogeneous facing weights

Avoid computing `f` and then subtracting it from one. Instead construct nonnegative weights `(P,Q)` with `f=P/(P+Q)` and `1-f=Q/(P+Q)`. Let `(x,y,z)` now be the unnormalized, rotated normal and `L` its length.

For `z<=0`, use `(P,Q)=(0,1)`. For exact zero Shift on the front hemisphere:

```text
L = sqrt(x*x+y*y+z*z)
P = z
Q = (x*x+y*y)/(L+z)
```

Since `Q=L-z`, this is the same facing ratio, with a stable complement at the maximum. No normal normalization or rotation is needed on the zero-shift path.

For nonzero Shift, the CPU supplies `k = sqrt((1-s)/(1+s))`. Scale the XZ slice before squaring to avoid underflow near the Y poles:

```text
v = max(abs(x), z)                  positive because z>0
X = x/v
Z = z/v
R = sqrt(X*X+Z*Z)
r = v*R

a = k*(R+X), b = Z                when x>=0
a = k*Z,     b = R-X              when x<0

S = a*a+b*b
P = r*(2*a*b)
Q = (y*y/(L+r))*S + r*(a-b)*(a-b)
```

The two half-angle charts describe the same warped circle, using `R+abs(X)` rather than a cancellation-prone subtraction. In either chart its facing within the slice is `2ab/S`. Also:

```text
P+Q = (L-r)*S + r*((a-b)^2+2ab) = L*S
P/(P+Q) = (r/L)*(2ab/S)
```

The shifted Schlick lobe is therefore unchanged. `Q` uses `L-r = y*y/(L+r)` and `S-2ab = (a-b)^2`, keeping both contributions nonnegative. Scaling the slice leaves `a,b` well conditioned even when the slice radius is tiny. This costs more arithmetic than the old cancellation-prone denominator but preserves the intended peak and removes the need for a denominator epsilon.

### Compose tone and color once on the CPU

For each color channel calculate:

```text
ND = A*d             NL = A*l
CD = B*(1-d)         CL = B*(1-l)
scale = max(ND,NL,CD,CL)
```

Divide all four coefficients by `scale` before storing them. Their common scale cancels from the result; at least one stored coefficient per channel is exactly one. `NL` and `CD` stay strictly positive and comfortably normal within the accepted material ranges. `ND` and `CL` may be zero at allowed tone endpoints. Contributions that underflow from negligible tiny positive inputs do not remove denominator positivity.

The pixel shader evaluates:

```text
numerator  = ND*Q + NL*P
complement = CD*Q + CL*P
color = numerator/(numerator+complement)
```

Substitution of `t=d*(1-f)+l*f` proves equivalence to `F(t)`. No division is needed to recover facing, and no per-pixel tone interpolation is needed. There is no subtraction of nearly equal color denominator terms and no positive-denominator floor.

Final RGB saturation removes binary32 output overshoot only. A production NVIDIA test returned `1.0000001192092896` (one ULP above one) before this final saturation. It is not a material clamp or a change to a valid real-number denominator. The adapter's sRGB RTV would also constrain the display output, but the reusable core promises RGB in [0,1].

Use full `float` arithmetic, not `half`/`min16float`. Direct3D's [floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules) permit flushing denormal inputs/results; the core does not rely on denormal preservation. The slice chart and the validated normal magnitude avoid a zero denominator; vanishing geometric contributions can round to zero without creating a finite cutoff parameter. DXC's [denormal-mode documentation](https://github.com/microsoft/DirectXShaderCompiler/wiki/Denorm-Mode) describes optional control in later shader models; this implementation targets SM 6.0 and does not require it.

## Geometry and transform validation

`SimplePaint::ValidateMesh` runs before upload. It checks nonempty indexed triangle data, index bounds, finite bounded positions, normal lengths in [0.5,2], valid material indices, and one material per triangle. Position components must have magnitude at most one million. Indices are unsigned 32-bit values in the reusable validator.

For each triangle, let `S` be the sum of its three vertex normals. Each vertex normal must satisfy `dot(N,S) >= 0.125*length(S)`, with `S` nonzero. Thus every barycentric interpolation has projection at least 0.125 on the same unit direction. Its length cannot approach zero. This is a conservative sufficient condition: some mathematically usable meshes are rejected so the GPU core can rely on a simple validated contract. Normal validation does not repair data or flip normals. Degenerate positional triangles need not shade any pixels; winding and culling are application choices.

`Orthographic::MakeProjection` validates finite width/height in [1e-4,1e6], `0<=near<far<=1e6`, and a depth span of at least 1e-4. `BuildObjectTransforms` validates its packed projection, finite matrix entries of magnitude at most 1e6, affine W entries `(0,0,0,1)`, orthogonal basis vectors, and uniform scale in [1/1024,1024]. Relative tolerance 1e-5 permits floating-point rotation-matrix roundoff; it is not support for artistic shear/nonuniform scaling.

Combined with the mesh cone condition, these transforms keep interpolated normal lengths approximately between 1/8192 and 2048. Squared full lengths are far from binary32 underflow and overflow. XZ slices can still be arbitrarily small, which is why the shifted evaluation scales them separately. A host using different geometry or transform routines must enforce equivalent conditions before invoking the core. The reusable shader cannot validate corrupted GPU buffers or unvalidated host data.

## Encapsulation, layout, and DX12 integration

`Source/SimplePaint` is the complete copyable module, with its own CMake target and integration README. `Material.*` owns parameters, validation, sRGB conversion, coefficient construction, and the GPU ABI. It is a C++20 library independent of DirectX types and scene settings. `Geometry.h` owns reusable mesh validation. `OrthographicTransforms.h` owns the optional DirectXMath transform adapter. `SimplePaintCore.hlsli` owns rotation, facing, and color evaluation and declares no bindings or material counts. `SimplePaint.hlsl` provides a reusable DX12 adapter whose `SIMPLE_PAINT_MATERIAL_COUNT` defaults to one; the host configures the count for both stages. JSON parsing belongs to the application's Settings module, and GPU resource ownership belongs to its renderer; both remain outside SimplePaint.

| Byte offset | C++ / HLSL field | Meaning                                   |
| ----------- | ---------------- | ----------------------------------------- |
| 0           | warp             | cos(-angle), sin(-angle), k, nonzero flag |
| 16          | numeratorDark    | ND for RGB, zero padding                  |
| 32          | numeratorLight   | NL for RGB, zero padding                  |
| 48          | complementDark   | CD for RGB, zero padding                  |
| 64          | complementLight  | CL for RGB, zero padding                  |

C++ asserts size 80, alignment 16, trivial copyability, and every field offset. The GPU tests exercise all six material indices, so the constant-buffer array stride and bindings are tested with the production shaders. `Material` has no mutating setters; `Compile` returns a complete material only after all validation succeeds. `Constants()` exposes read-only owned data for copying to an upload buffer.

The application uses 96-byte object blocks at 256-byte-aligned addresses in `b0`. Its CMake build defines a material count of six for both shader stages and C++; a renderer static assertion checks agreement. `b1` points at six tightly packed 80-byte materials in a 512-byte allocation. The array is uploaded once at startup and shared across objects and frame slots. Only the moving car's transform block changes every frame; sphere transforms refresh when the camera viewport changes. Fence synchronization remains owned by the renderer.

Original exact-zero/one base-color settings were explicitly edited to `m`/`M` in the shipped asset. They are not converted by a compatibility path. The old global cutoff section and in-shader denominator epsilon have been removed. The obsolete working specification was removed; earlier numerical proposals remain as historical reports.

## Optimization and verification

The implementation precomputes sRGB conversion, angle trig, shift square root, curve coefficients, tone remapping, and per-channel coefficient scaling. The shader loads five float4 material registers, as did the previous implementation. The orthographic adapter omits view-vector construction, perspective correction, and normal normalization. Material rotation runs per vertex. Exact zero Shift selects a smaller path independently of Rotation.

The PS source has one square root on the zero-shift path and two on the shifted path. It has no pow, log, exponent, sine, cosine, or normal rsqrt. DXC `-O3 -Ges -WX` compilation and inspection of the generated PS DXIL confirmed the two square-root call sites and absence of those transcendental operations. The extra shifted slice arithmetic protects near-pole and near-peak behavior. Optimizations are chosen subject to the numerical contract; no claim is made that this is the fastest possible shader on every GPU.

Reproduce module checks using [the standalone build commands](Usage.md#build-and-verify). The application checks below record historical verification in the original host repository. The suite includes the following numerical and integration checks:

| Check                    | Coverage / result                                             |
| ------------------------ | ------------------------------------------------------------- |
| SimplePaintContract      | 81,940 curve samples, anchor/endpoints/monotonicity           |
| Original K12 equivalence | 20,000 independent slice/Schlick/remap comparisons            |
| Input validation         | Every scalar, adjacent binary64 bounds, NaN, infinities       |
| Geometry / transforms    | Actual car, sphere, bad indices/normals/materials/projections |
| SimplePaintGpuHardware   | 30,208 production VS/PS samples; RTX 5070 Ti Laptop GPU       |
| SimplePaintGpuWarp       | Same 30,208 samples; Microsoft Basic Render Driver            |
| Interpolation            | 4,096 of each GPU run use distinct triangle vertex normals    |
| Existing geometry tests  | UV sphere checks and 4,000 orthographic transform samples     |
| DX12 debug validation    | No warnings or errors in either GPU correctness run           |

The rebuilt Release application also passed a hidden-window smoke check: normal initialization, two seconds in its render loop, and clean exit after `WM_CLOSE`. `Build.cmd` builds the application, and `Run.cmd` launches the existing build. `Build.ps1 -Test` builds and runs the verification suite.

The module owns five CTest entries: SimplePaintContract, OrthographicTransforms,
SimplePaintGpuHardware, SimplePaintGpuWarp, and SimplePaintStandalone.
SimplePaintStandalone copies the module's source, documentation, reports, and tests
into a fresh consumer project. It builds and runs every public C++ interface
using host-owned geometry, compiles VS/PS with counts one and three, and checks
that zero is rejected. It also builds and runs the copied module's full CPU/GPU
suite, excluding the recursive copy check. The original game's car/sphere mesh
checks remain host integration tests; they are not dependencies of the module.

The independent binary64 GPU reference starts from the requested binary64 material parameters and the stored binary32 interpolated normal. It computes rotation, normalized facing, explicit tone remapping, and the uncomposed color curve. Thus it includes CPU coefficient conversion, VS rotation, GPU interpolation, and shader evaluation error. It does not measure errors from an external mesh exporter or an arbitrary host's world/view calculation; the transform tests cover this application's adapter separately.

The regression acceptance criterion is finite output in [0,1], alpha exactly one, and **maximum absolute per-channel error <= 0.001 in both linear RGB and sRGB**. The sRGB criterion is measured before display quantization. Results from the Release run:

| Backend                       | Maximum linear error | Maximum sRGB error |
| ----------------------------- | -------------------- | ------------------ |
| NVIDIA RTX 5070 Ti Laptop GPU | 0.000214660          | 0.000112257        |
| Microsoft Basic Render Driver | 0.000201116          | 0.000105149        |

These are measured regression bounds on deterministic boundary, peak, pole, inverted-range, constant-range, scaled-normal, interpolation, and random cases. They are not an exhaustive proof over all binary64 parameter combinations or a promise of bitwise identity across GPU drivers. The abstract equivalence and positive-denominator arguments are analytic; the stated finite-precision error criterion is tested. Re-run the GPU tests when changing the compiler, arithmetic, accepted limits, or target hardware.

An optional GPU-timestamp microbenchmark is available as `MyBuildOutput/Release/Source/SimplePaint/Tests/SimplePaintGpuTests.exe --benchmark`. On the same NVIDIA GPU, 128 warmed 1024x1024 production draws measured **0.0204 ms/draw for Shift=0** and **0.0216 ms/draw for Shift=0.6**. This uses constant normals and a float4 render target, excludes CPU submission/setup/readback, and is not a whole-scene FPS prediction or a speedup comparison against the old shader.
