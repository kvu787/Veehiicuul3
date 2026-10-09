# SimplePaint numerical stability analysis

Date: 2026-09-05. Application source examined: commit 29ecdef (the shader and renderer are unchanged from the reviewed ba374bb source).

The denominator floor causes large color errors for valid interior parameters. It is not the only problem: input clamping changes the requested curve, and cancellation damages several intermediate calculations. Removing the floor alone does not solve cancellation.

This report distinguishes:
- **Arithmetic errors:** the GPU differs from a stable evaluation of the same stored parameters.
- **Parameter changes:** the CPU or a tolerance branch replaces the user's requested values.
- **Inherent sensitivity and undefined endpoints:** a formula can be sensitive or undefined even when implemented accurately.

All reported color outputs are linear red-channel values before sRGB encoding and 8-bit output quantization. They are not percentages of perceived brightness. Other channels obey the same equations.

**How to reproduce the settings-only examples**

Use the listed Brightness, set both DarkPoint and LightPoint to T, and set Body to the sRGB triple C, C, C in Assets/CarPaint.ini. Leave Shift=0, RotationDegrees=0, and FacingCutoff=0.01. Equal tone endpoints make the result independent of surface orientation. Relaunch using Run.cmd.

The [Presets](Presets) directory contains ten complete INI files. They were generated as examples; the application's source settings file has not been changed. [Examples.csv](Examples.csv) contains 42 named cases, exact parameter values, supplied normals, measured outputs, and reference outputs. CSV columns B/S/R/D/L/F mean Brightness/Shift/RotationDegrees/DarkPoint/LightPoint/FacingCutoff; r/g/b are requested sRGB channels and nx/ny/nz are supplied view-space normal components before normalization. gpu_r is the production result; requested_r is the mathematical reference before CPU clamps; safe_r uses the stored parameters. NoFloor_r, Positive_r, and Stable_r identify the three experiments. Blank references mark an undefined formula. Rows with names beginning floor_white were negative controls and do not actually activate the denominator floor; use the demonstrated rows below.

The reference column below uses the same stored float32 parameters and CPU color conversion/clamps as the application, with stable arithmetic. This isolates the shader arithmetic from input quantization. References to the original requested parameters are separately recorded in the CSV.

| Example             | Brightness | C, sRGB | T        | Current GPU | Stable reference |
| ------------------- | ---------: | ------: | -------: | ----------: | ---------------: |
| Previously reported | 0.01       | 0.001   | 1        | 0.07739938  | 1                |
| Dark A              | 0.1        | 0.0002  | 1        | 0.15479876  | 1                |
| Dark B              | 0.5        | 0.0002  | 1        | 0.77399379  | 1                |
| Dark C              | 0.01       | 0.01    | 1        | 0.77399373  | 1                |
| Near-white A        | 0.999      | 0.9999  | 0.000001 | 0.09987726  | 0.81443371       |
| Near-white B        | 0.9999     | 0.9999  | 0.000001 | 0.09996725  | 0.97773859       |
| Near-white C        | 0.99       | 0.99999 | 0.000001 | 0.09899775  | 0.81301181       |
| Interior tone       | 0.0001     | 0.001   | 0.999999 | 0.000773993 | 0.007581929      |

None of these examples depends on a mathematically undefined color endpoint: their brightness and base color are strictly between zero and one. The final row also uses an interior tone.

**1. Color-denominator flooring - large, confirmed arithmetic error**

Location at the examined revision: SimplePaint.hlsl, lines 81-84. The current shader is [here](../../SimplePaint.hlsl).

Let B be brightness, b a linear base-color component after CPU conversion, and t the remapped tone. In exact arithmetic the existing curve can be written:

~~~text
U = b * B * t
V = (1 - B) * (1 - b) * (1 - t)
color = U / (U + V)
~~~

The existing denominator k2*t+k3 is algebraically U+V. Replacing it with max(denominator, 0.00001) changes legitimate divisions whenever the positive denominator is smaller than that threshold. At t=1, V=0, so any positive U should produce color=1, regardless of how small U is. Near-white materials at small positive tones produce the opposite-end failures shown above.

Direction: evaluate the nonnegative terms directly, preserve positive denominators, and define true zero-denominator cases explicitly. Do not merely substitute a smaller arbitrary floor.

**2. Cancellation in precomputed coefficients and the final denominator - confirmed independently of the floor**

Locations at the examined revision: Renderer.cpp, lines 1157-1183; SimplePaint.hlsl, lines 81-84. Renderer.cpp belongs to the original host application, outside this module.

For dark colors, k2 is negative and k3 is positive. Near the bright endpoint they nearly cancel. Rounding each coefficient to float32 loses information before the pixel shader runs. A fused multiply-add cannot restore already-lost coefficient bits.

Examples, with C used in all Body channels and DarkPoint=LightPoint=1:

| Brightness | C         | Current     | Experimental floor removed | Nonnegative formula |
| ---------- | --------: | ----------: | -------------------------: | ------------------: |
| 0.5        | 0.000259  | 0.99799234  | 0.99799234                 | 1                   |
| 0.00001    | 0.04      | 0.003095975 | 0.51941842                 | 1                   |
| 0.00001    | 0.0001292 | 0.000010000 | 1 after saturation         | 1                   |

In the first row both the computed and stable denominators exceed 0.00001, so the denominator floor is inactive. It is a direct cancellation reproduction.

In the last row the stored k2+k3 is exactly zero even though the stable denominator is positive. Removing the floor creates an intermediate division by zero; the final saturation happened to return 1 on this GPU. A finite final pixel does not prove its intermediate arithmetic was valid.

Direction: reuse the existing positive k1 and k3 coefficients as numerator=k1*t and denominator=numerator+k3*(1-t). With the existing CPU clamps and valid tones, this experimental change reduced the maximum absolute color-grid error to 1.239e-7. Broader removal of CPU clamps requires separate endpoint and underflow handling.

**3. Base-color clamping changes black, white, and nearby colors - confirmed parameter distortion**

Location: Renderer.cpp, lines 1161-1164.

The CPU clamps each linear channel into [0.00001, 0.99999]. This does not preserve exact black or white. It also collapses a band of near-black/near-white inputs onto the same value. The changed endpoints can be strongly amplified by the paint curve.

| Brightness | C       | T      | Current GPU | Requested-parameter reference |
| ---------- | ------: | -----: | ----------: | ----------------------------: |
| 0.9999     | 0       | 0.8    | 0.28566208  | 0                             |
| 0.9999     | 0.00001 | 0.8    | 0.28566208  | 0.03002714                    |
| 0.5        | 1       | 0.0001 | 0.90898609  | 1                             |

These cases have nonsingular reference formulas. Their color denominators exceed the shader floor. They isolate input-clamp behavior, rather than attributing every error to the pixel denominator.

Direction: preserve black/white where the equation is well-defined; specify the remaining endpoint intersections rather than perturbing every endpoint.

**4. Brightness clamping changes valid curves - confirmed parameter distortion**

Location: Renderer.cpp, line 1140.

Brightness is silently constrained to [0.00001, 0.99999]. Use C=0.7353569830524495, which is approximately linear 0.5.

| Requested Brightness | T       | Current GPU | Requested-parameter reference |
| -------------------- | ------: | ----------: | ----------------------------: |
| 0.0000001            | 0.999   | 0.009891283 | 0.000099890                   |
| 0.9999999            | 0.001   | 0.99009538  | 0.99990011                    |
| 0                    | 0.99999 | 0.49932212  | 0                             |
| 1                    | 0.00001 | 0.49966082  | 1                             |

The final two formulas are well-defined because the tone is not at the problematic opposing endpoint. Small rounding errors contribute to the exact measured values, but changing brightness explains the large discrepancy.

Direction: implement the well-defined endpoint limits and explicitly decide the undefined corner cases.

**5. The shifted-highlight calculation has its own denominator floor - large arithmetic error at grazing normals**

Location: SimplePaint.hlsl, lines 69-75.

The warp denominator is sliceExtent - shift*rotatedX. It is also floored at 0.00001.

Use Brightness=0.5, Body=(0.7353569830524495, 0.7353569830524495, 0.7353569830524495), DarkPoint=0, LightPoint=1, RotationDegrees=0, Shift=0.99999, FacingCutoff=0. Supply the view-space normal:

~~~text
(0.099999, 0.99498743710662, 0.00044721247746346156)
~~~

Current output: 0.02001354. Stable reference for the stored parameters: 0.10000002. An experimental stable warp returned 0.10000002.

This is a surface-dependent example, not a promise that the shipped car contains that exact normal at a visible pixel. It requires reducing the default cutoff; the specified normal is rejected by FacingCutoff=0.01. More generally, the default cutoff keeps this particular denominator floor inactive for ordinary unit normals and the existing maximum shift.

Direction: use the non-subtractive warp denominator described below and give its true pole a deliberate result.

**6. Shifted-highlight cancellation and square-root precision - smaller arithmetic errors**

Locations: SimplePaint.hlsl, lines 69-75; Renderer.cpp, line 1148.

Two subtractions lose precision near Shift=1:
- sliceExtent - shift*rotatedX on the GPU;
- 1 - shift*shift when computing the square-root factor on the CPU.

With the neutral material used in issue 5, Shift=0.99999, FacingCutoff=0, and normal approximately (0.99999, 0, 0.0044721247746), current output is 0.99932426 versus a stable stored-parameter reference of 0.99999977. The stable warp experiment returned 0.99999988.

The default cutoff does not eliminate all cancellation: with normal approximately (0.9999499887, 0, 0.010001) and Shift=0.99999, the current output is 0.74567103 versus 0.74563238 for the stored parameters.

To isolate the square root, use Shift=0.99983, FacingCutoff=0, normal=(0,0,1). Current output is 0.01843869 versus 0.01843790 for the stored parameters. The CPU root has approximately 0.00425% relative error.

For nonnegative rotated X, write m=sqrt(x*x+z*z) and s=shift:

~~~text
m - s*x = z*z/(m+x) + (1-s)*x
sqrt(1-s*s) = sqrt((1-s)*(1+s))
~~~

The first identity avoids cancellation when x>=0 and m+x>0. For negative x, m-s*x already adds positive magnitudes. The zero-length slice still needs an explicit branch. Using these identities with positive color terms reduced the worst error across the 1,500 random warp cases from 0.13527 to 3.173e-7. The old small-shift tolerance branch remained in the experiment.

**7. Shift clamping changes the requested warp near one - parameter distortion**

Location: Renderer.cpp, line 1141.

With the neutral material, DarkPoint=0, LightPoint=1, FacingCutoff=0, and normal=(0,0,1):

| Requested Shift | Current GPU | Requested-parameter reference |
| --------------- | ----------: | ----------------------------: |
| 0.999999        | 0.00447517  | 0.00141421                    |
| 1               | 0.00447517  | 0                             |

Both requested values are replaced by the same stored shift. The reference at Shift=1 for this particular normal is well-defined; it is not the warp's 0/0 pole.

Direction: preserve well-defined values/limits, and define the pole separately.

**8. Tone interpolation can erase a small light endpoint - arithmetic/coefficient loss**

Locations: Renderer.cpp, line 1151; SimplePaint.hlsl, line 79.

Use the neutral material, Shift=0, normal=(0,0,1), DarkPoint=1, LightPoint=0.00000001. The requested output is approximately 1e-8; the GPU produces 0. The CPU stores LightPoint-DarkPoint as -1 in float32, losing LightPoint before the shader runs. LightPoint=0.0001 produces 0.00010001664 instead of approximately 0.0001.

This is generally small in absolute output, but demonstrates that the stored representation cannot preserve both endpoints.

Direction: store both endpoints and compute dark*(1-facing)+light*facing. Bound facing to [0,1] for both shader paths. The current symmetric, zero-shift path does not explicitly saturate facing.

**9. Large rotation values lose degrees before range reduction - input precision issue**

Locations: Renderer.cpp, ParseFloat at lines 285-299 and rotation reduction at lines 1142-1143.

RotationDegrees=1000000000000 should reduce to 280 degrees. Parsing it into float32 first produces 999999995904, which reduces to 144 degrees. Reducing an already-rounded number cannot recover the missing degrees.

With the neutral material, Shift=0.8, normal=(0.6,0,0.8), and the usual zero-to-one tone range:
- RotationDegrees=1000000000000 produced 0.33922452.
- RotationDegrees=280 produced 0.53530598.
- The requested-parameter reference is 0.53530587.

Direction: parse/reduce rotation using double precision, then convert the bounded angle to float. This extends the useful range; arbitrary-magnitude decimal angles would still require an explicit range/precision policy.

**10. The small-shift fast path introduces a dead band - deliberate approximation**

Location: SimplePaint.hlsl, line 61.

All stored shifts up to 0.00001 are treated as zero. With neutral material, Shift=0.00001, and normal=(0.6,0,0.8), the GPU returns 0.80000007 versus a stable stored-parameter reference of 0.80000487.

This is a small approximation rather than a large instability. It means the comment calling the path "exact" only applies to Shift=0.

Direction: test exactly for zero if all nonzero shift values should retain their effect, or document/justify the tolerance.

**11. Very small tones underflow or flush to zero - extreme, visually negligible case**

Use the neutral material and DarkPoint=LightPoint=1e-40. The parser accepts a finite subnormal float, but the GPU produces 0 rather than approximately 1e-40. This has no meaningful visible effect at the current output precision.

Direction: set realistic input precision/ranges if needed. Do not promise preservation of arbitrary positive real numbers in float32 shader arithmetic. Removing the CPU clamps would also require analyzing underflow in very small coefficient products.

**12. Some exact endpoints have no unique mathematical answer - design decision required**

In the positive formulation, the curve is undefined whenever both U and V are zero. Examples include:
- Brightness=0, tone=1, with an interior base color;
- base color=0, tone=1, with interior brightness;
- Brightness=1, tone=0;
- base color=1, tone=0;
- Brightness=1 with base color=0, or Brightness=0 with base color=1.

For Brightness=0 and tone=1, approaching brightness first can give a different limit than approaching tone first. No rearrangement alone can determine the intended artistic behavior at every such intersection.

Likewise, Shift=1 at normal=(1,0,0) is a warp pole. Approaching it along the moving highlight differs from holding the normal fixed. The existing clamps select particular results incidentally.

Direction: choose an explicit endpoint policy before removing input clamps. The nonsingular examples above do not depend on that policy.

**13. Normalization and other guarded cases - limits of the findings**

The HLSL normalize function has an indefinite result for a zero-length vector. The diagnostic zero-normal test produced black on this GPU, not a NaN output; that result is not a portable input contract. [Microsoft normalize documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-normalize).

This is not a confirmed problem with the shipped mesh. All same-triangle normal pairs have dot product at least 0.92387894, giving an object-space lower bound of approximately 0.961186 for convexly interpolated normal length. The fixed, nonzero uniform scale and view rotation preserve separation from zero. A future mesh with opposing smooth normals or a degenerate transform would need a guard.

The front/back cutoff intentionally creates a discontinuity: with the neutral material, normal.z=0.009999 gives 0 and normal.z=0.010001 gives approximately 0.010001. That jump is the configured cutoff behavior, not an arithmetic defect. Tiny floating-point changes near the threshold can choose different branches.

The shader's bounded parameters and existing clamps prevented NaN/infinity in every measured final output. Square-root arguments are guarded; finite rotation is range-reduced before trigonometry; matrix magnitudes and normals in the shipped scene are ordinary. Overflow or widespread NaN output was not demonstrated. The background shader has no comparable sensitive paint arithmetic; it performs bounded UV mapping and texture sampling.

**Inherent conditioning remains even after arithmetic is stabilized**

Near singular endpoints, small input changes can cause comparatively large output changes. For the default-cutoff warp example in issue 6, literal Shift=0.99999 gives approximately 0.74529512 in the requested-parameter reference. Storing it as float32 changes shift to approximately 0.99998998642; the stable reference then becomes 0.74563238.

The stable GPU result is approximately 0.74563253. The remaining difference from the decimal-input result is chiefly input quantization, not failure of the stabilized denominator. This is why the report has separate requested-parameter and stored-parameter references.

**Verification and experimental changes**

- 6,593 distinct cases, including 4,275 constant-tone grid cases, 576 structured warp cases, 1,500 seeded random warp cases, 200 sampled default-body normals, and 42 named cases.
- Four shader versions: production, color floor removed, nonnegative color formula, and nonnegative color plus stable warp. Total: 26,372 distinct case/version combinations.
- CPU constants came from the production LoadPaintSettings method.
- The original compiled pixel shader was rendered into a float32 RGBA target. A test vertex shader supplied controlled view-space normals and material index 1. This avoids display gamma and 8-bit quantization masking small errors.
- References used stable double-precision formulas. Constant-tone references were independently checked using 70-digit Decimal arithmetic; maximum difference was 1.11e-16.
- Production outputs were finite for every case. This is a finite sample, not an exhaustive proof over every possible float bit pattern.
- The maximum constant-tone grid error versus stored-parameter references was about 0.99999 in production, 0.48058 after merely removing the color floor, and 1.239e-7 with the nonnegative color formula.
- In 200 default-body samples, the largest differences versus the unclamped requested-color formula were approximately 3.93e-5 red, 7.13e-8 green, and 8.61e-4 blue. These are much smaller than the stress cases. The dramatic floor failures should not be read as a claim that every default pixel is visibly wrong.
- GPU checks used the local NVIDIA adapter selected by the renderer; the prior adapter enumeration identified an RTX 5070 Ti Laptop GPU. Other GPU/driver arithmetic may differ slightly. HLSL mad is permitted to use fused or non-fused operations. [Microsoft mad documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/mad).
- The experimental shaders remained under Build/shader-numerics. No production shader, renderer, or source paint settings were changed. No performance comparison was performed.

The complete local experiment inputs, raw outputs, alternate shaders, and harness are in Build/shader-numerics; that directory is ignored by Git. The checked-in CSV and presets preserve the named reproductions.

The original K12 source was consulted to distinguish the intended rational curve from the added endpoint protections. Its own exact endpoint and pole behavior is not a reliable reference for undefined cases. [Original K12 source](https://github.com/kvu787/SimplePaintShaders/blob/793126205e028f06f635f23e87a9bac856bf669a/Godot/ShaderTest/Shaders/K12.gdshader).

**Recommended order of work**

1. Replace the color subtraction/floor with the verified nonnegative formulation while initially preserving the existing CPU parameter policy.
2. Stabilize the warp denominator and root; separately address its zero-length slice.
3. Store both tone endpoints and preserve their interpolation endpoints.
4. Decide black/white, brightness, and shift endpoint behavior before removing their clamps.
5. Preserve meaningful angle precision before reduction; decide whether the tiny-shift approximation is intentional.
6. Keep these examples as numerical regression cases. Benchmark any proposed change separately before making performance claims.
