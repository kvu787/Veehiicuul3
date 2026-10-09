# SimplePaint numerical analysis with strict RGB margins

Date: 2026-09-05. Examined implementation: 2700715.

Historical scope: the working proposal examined here was subsequently replaced by the [implemented specification](../../Specification.md). That proposal required e < R,G,B < 1-e, with e=0.01. Brightness was in [e,1-e], Shift in [0,1-e], Rotation in [0,360], DarkPoint in [0,1-e], and LightPoint in [e,1]. FacingCutoff was fixed at the K12 value 0.01. DarkPoint could exceed LightPoint. All inputs were finite.

Conclusion: the new RGB limits remove the exact-color endpoint singularities and make the CPU color clamps inactive. They also give the intended color denominator a positive lower bound. At e=0.01 that bound is still below the shader's floor, and legal inputs can lose approximately 22.6% of their linear intensity. Cancellation and amplified rounding remain, including cases where the floor is inactive.

The earlier proof that the same RGB=0.0002 example fails for every feasible epsilon no longer applies: that color is outside this revised domain. Increasing the RGB margin can now exclude the floor problem in exact arithmetic. This changes the conclusion about what input restrictions can accomplish, even though the requested e=0.01 remains insufficient.

**Current repository changes included**

This analysis was rebuilt against the current source rather than reusing the previous shader binary:

- d18dfc4 added the sixth material and the runtime UV sphere.
- b4b4b2b gave each material its own BaseColor, Brightness, Shift, RotationDegrees, DarkPoint, and LightPoint in Settings.ini.
- 34ce9a1 moved the material's paint rotation into the vertex shader, before interpolation and pixel normalization.
- b7e4578 specialized the affine orthographic transforms and cached static constants. Pixel normals are interpolated with noperspective.
- 2700715, committed during this analysis, changed sphere settings only. Shader and renderer source are identical to the initially examined 44459fa.

The rational color formula, CPU linear-color clamps, 1e-5 denominator floors, and tiny-shift fast path remain present. Per-material settings change the bindings and configuration, not the scalar color equations. The proof applies to each material independently. The local GPU tests use Body/material index 1 and execute both the current production vertex shader and pixel shader.

Source locations at this revision: shaders/SimplePaint.hlsl lines 64-73 (vertex rotation), 83 (normalization), 88-91 (tiny-shift shortcut), 96-100 (shift denominator), 105-110 (tone and color denominator); src/Renderer.cpp lines 333-338 (sRGB conversion), 1196-1204 (CPU clamps), and 1206-1236 (material coefficients).

**The crucial distinction: sRGB input versus linear shader color**

The repository's BaseColor values are sRGB. For an sRGB channel C in this low range:

~~~text
b = C / 12.92
~~~

The requested interval 0.01 < C < 0.99 becomes:

~~~text
b_min = linear(0.01) = 0.0007739938080495...
b_max = linear(0.99) = 0.9774019338064516...
b_min < b < b_max
~~~

It does not become 0.01 < b < 0.99. The numerical analysis must use these converted bounds. The CPU's [1e-5,1-1e-5] linear-color clamp is inactive throughout this new interval, including ordinary float rounding of its endpoints.

If the proposed numbers instead described linear color, the denominator bound would exceed 0.0001 and the current floor would be inactive in exact arithmetic. That is a different input contract, not the interpretation adopted here.

**Proof of the color-denominator bound**

Let beta be Brightness, b one linear base channel, and t the interpolated tone. Since the facing is in [0,1] mathematically, the independent DarkPoint and LightPoint ranges still give t in [0,1]. Both endpoints remain reachable.

~~~text
U = b*beta*t
V = (1-beta)*(1-b)*(1-t)
color = U/(U+V)

D_color = t*(b*beta) + (1-t)*((1-beta)*(1-b))
~~~

The denominator is an interpolation between two positive values. Let l=linear(e) and u=linear(1-e). For the strict RGB interval and beta in [e,1-e]:

~~~text
b*beta > e*l
(1-beta)*(1-b) > e*(1-u)
D_color > e*min(l, 1-u)
~~~

At e=0.01, the darker bound dominates:

~~~text
D_color > 0.01 * (0.01 / 12.92)
D_color > 0.0000077399380805...
~~~

This is a strict lower bound, approached as C approaches 0.01 from above, with Brightness=0.01 and tone=1. The implementation floor is 0.00001, which is larger. The strict inequality does not prevent legal inputs from approaching the lower bound arbitrarily closely.

The color formula is nevertheless mathematically well-defined for every allowed color and tone: both denominator endpoint terms are positive. At t=0, color=0; at t=1, color=1. The previous black-at-white-tone and white-at-black-tone 0/0 cases are excluded.

The dark endpoint is much better separated from the floor:

~~~text
D_color at t=0 > 0.01*(1-linear(0.99))
D_color at t=0 > 0.000225980662
~~~

Consequently the former near-white shadow-floor cases are excluded. The surviving floor cases are concentrated near the bright endpoint for dark colors and low Brightness.

**Verified examples: floor and color distortion**

All examples in this table use Brightness=0.01, DarkPoint=0, LightPoint=1, Shift=0, Rotation=0, and N=(0,0,1). C is the same sRGB value in all three channels. Every RGB value satisfies the strict bounds. Every expected linear channel is 1.

| C        | Current linear output | Main mechanism                           |
| -------: | --------------------: | ---------------------------------------- |
| 0.010001 | 0.77407122            | Denominator floor                        |
| 0.0101   | 0.78173369            | Denominator floor                        |
| 0.011    | 0.85139316            | Denominator floor                        |
| 0.012    | 0.92879254            | Denominator floor                        |
| 0.013    | 0.99888158            | Coefficient cancellation; floor inactive |
| 0.020    | 0.99888152            | Coefficient cancellation; floor inactive |

For C=0.010001 the true stored-input denominator is about 7.740712e-6. The current output is about 0.225929 below the intended result. As C approaches the lower bound, the floor-only mathematical error approaches 0.2260062. This is a bound on that mechanism, not a global bound on all shader errors.

For a non-gray example with the same controls:

~~~text
BaseColor = (0.0101, 0.0200, 0.5000)
expected linear output = (1, 1, 1)
current linear output = (0.78173369, 0.99888152, 1)
~~~

Thus the effect can change hue as well as intensity. These are controlled-normal shader measurements, not claims that the shipped car displays every tested normal on a visible pixel.

Ignoring rounding, at Brightness=0.01 and tone=1 the denominator is below the floor whenever C<0.01292. The newly legal interval includes 0.01<C<0.01292. The actual switch is affected by coefficient rounding; for example, C=0.0129 already produces a rounded coefficient denominator slightly above the floor while remaining inaccurate.

**Other remaining issues, with legal examples**

1. Cancellation remains without the floor. With C=0.011867604444444443, Brightness=0.011, DarkPoint=0, LightPoint=1, Shift=0, Rotation=0, and N=(0,0,1), output is 0.99715859 instead of 1. The stable denominator is 1.01039975e-5, and the shader's coefficient denominator is 1.01327896e-5: both exceed the floor. CPU coefficients subtract and combine terms near one to recover a value near 1e-5. A separate experimental nonnegative color formula gives 1 for this case.

2. Tone/normal rounding is still amplified near highlights. With C=0.0129, Brightness=0.01, DarkPoint=0.99, LightPoint=1, Shift=0, Rotation=0, and N approximately (0.004472124773269218,0,0.9999900000000062), output is 0.98549485 versus a stable stored-input reference of 0.99019171. Both denominators exceed the floor. The GPU tone is 0.9999998807907104 versus an ideal stored-input tone of approximately 0.999999900000089. The experimental nonnegative color formula gives 0.98832959, isolating the upstream rounding that remains after fixing the final subtraction. The intended curve's maximum limiting slope at tone=1 is approximately 127809 under the requested bounds; small tone errors can therefore matter despite a positive denominator bound.

3. The tiny-shift approximation can be amplified by dark colors. For C=0.013, Brightness=0.01, DarkPoint=0, LightPoint=1, Rotation=0, and N=(0.0026043264761565874,0,0.9999966087360525), both Shift=0 and Shift=0.00001 produce exactly the same current output, 0.74902797. Their stable stored-input references are 0.74999936 and 0.75143926 respectively. The shortcut omits a desired change of approximately 0.00143991; the total error also includes ordinary rounding and color cancellation. The floor is inactive. A neutral-color version with N=(0.6,0,0.8) has the much smaller previously observed error: 0.80000007 instead of 0.80000487. A small shift error is not necessarily the same-sized final color error.

4. Ordinary shifted-facing rounding remains small in the structured neutral-color tests. With C=0.7353569830524495, Brightness=0.5, DarkPoint=0, LightPoint=1, Shift=0.99, Rotation=0, and N=(0.99498743710662,0,0.1), output is 0.94280916 versus 0.94281022. The shift denominator has a large margin from its floor. Moving rotation to vertices preserves the exact mathematics under the current affine orthographic assumptions, but still permits floating-point differences before normalization. Nonzero rotations, 0 versus 360 degrees, and both sides of the cutoff were included in the current GPU tests.

5. Reversed-tone interpolation still rounds. With neutral C=0.7353569830524495, Brightness=0.5, DarkPoint=0.99, LightPoint=0.01, Shift=0, Rotation=0, N=(0,0,1), the GPU tone is 0.00999999046 and output is 0.00999999605, versus a stable stored-input color of 0.01000000450. The earlier catastrophic loss of LightPoint=1e-8 remains excluded. This example's absolute error is tiny.

6. Subnormal dark tones can still disappear. With neutral C, Brightness=0.5, DarkPoint=1e-40, LightPoint=0.01, Shift=0, Rotation=0, and N=(0,0,-1), the current output is 0 instead of approximately 1e-40. DarkPoint still has no positive lower margin. This is visually negligible. Microsoft documents denormal flushing in the [Direct3D floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules); the result was also measured here.

7. The facing cutoff remains a deliberate discontinuity. Normals below q select DarkPoint, while admitted normals can yield a different tone immediately above q. Input color margins do not remove it. It is inherited K12 behavior, not a newly identified arithmetic defect. The 270 dedicated near-cutoff cases did not change branch relative to the stored-input reference on the tested adapter; that is not a guarantee for every near-threshold normal, interpolation path, or GPU.

8. Normal validity remains a separate precondition. The new scalar bounds cannot validate geometry or prevent a zero interpolated normal. [Microsoft's normalize documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-normalize) leaves zero-length normalization indefinite. This analysis assumes finite nonzero interpolated normals, uniform object scaling, and one material per triangle as required by the current renderer. No new defect in the shipped geometry was established here.

**Shift-denominator guarantee is unchanged**

Let s=Shift, x the normalized rotated horizontal component, z the normalized camera-facing component, and m=sqrt(x*x+z*z). For the mathematical denominator W=m-s*x:

~~~text
W*W - z*z*(1-s*s) = (x-s*m)*(x-s*m) >= 0
W >= abs(z)*sqrt(1-s*s)
W >= q*sqrt(e*(2-e)) on the admitted branch
~~~

At q=e=0.01, W is at least approximately 0.001410674, about 141 times the 1e-5 floor. The square-root factor is at least 0.14106736. This exact-arithmetic bound is unchanged by moving an orthogonal XY rotation to vertices; floating-point normalization and trig still introduce rounding. It is a domain bound with substantial margin, not a formal all-GPU certificate.

**Issue status compared with the previous proposal**

| Issue                                                              | Strict RGB proposal at e=0.01                              |
| ------------------------------------------------------------------ | ---------------------------------------------------------- |
| CPU black/white color perturbations                                | Excluded for compliant channels                            |
| Undefined rational color endpoints                                 | Excluded                                                   |
| Former extreme dark and near-white examples outside the RGB margin | Excluded; they must not be reused as legal counterexamples |
| Positive lower bound for intended color denominator                | Now available, but only about 7.74e-6                      |
| Color denominator floor                                            | Still active for legal dark highlight cases                |
| Coefficient cancellation                                           | Still present even with floor inactive                     |
| Amplification of tone/normal rounding                              | Bounded but still measurable                               |
| Tiny-shift shortcut                                                | Still present; can be amplified by the color curve         |
| Brightness endpoint clamps                                         | Excluded at e=0.01                                         |
| Shift=1 pole and high-shift clamp                                  | Excluded at e=0.01                                         |
| Shift denominator floor with fixed cutoff                          | Strong margin; no floor failure found                      |
| Huge-angle input loss                                              | Excluded by Rotation<=360; ordinary rounding remains       |
| Tiny DarkPoint underflow                                           | Still allowed; negligible absolute effect                  |
| Invalid normals / inherited cutoff discontinuity                   | Not governed by these scalar bounds                        |

**What changing epsilon would accomplish now**

Near the proposed value, the limiting bound is e*linear(e)=e*e/12.92. To put that bound above the current 1e-5 floor in exact arithmetic:

~~~text
e > sqrt(12.92 * 0.00001)
e > approximately 0.01136661779
~~~

At the exact threshold the strict RGB inequality also makes each exact denominator greater than the floor, but leaves no uniform positive safety margin beyond it. Rounding can invalidate that reasoning for the implemented coefficients. A rounded suggestion such as 0.01137 is therefore not an accuracy certification.

| e     | Mathematical color-denominator lower bound | Relation to 1e-5 floor |
| ----: | -----------------------------------------: | ---------------------- |
| 0.005 | 0.000001934985                             | Below                  |
| 0.010 | 0.000007739938                             | Below                  |
| 0.011 | 0.000009365325                             | Below                  |
| 0.012 | 0.000011145511                             | Above, modest margin   |
| 0.020 | 0.000030959752                             | Above, larger margin   |

Smaller e worsens this bound. Increasing e can remove this floor mechanism in the ideal model, but does not eliminate cancellation. For example, inputs also satisfying e=0.012 with C=0.012034265555555555, Brightness=0.012, DarkPoint=0, LightPoint=1, Shift=0, Rotation=0, and N=(0,0,1) produce 0.99747115 instead of 1, with the floor inactive.

If separate margins were permitted, keeping Brightness>=0.01 while raising only the sRGB lower bound above approximately 0.01292 would address the bright-end floor in exact arithmetic. That is another contract, not a change adopted in this report. Neither threshold is a promise of a desired output-error tolerance.

**Verification and reproducibility**

- Recompiled the actual current VSMain and PSMain with the repository's DXC flags: -O3, -Ges, -WX, -all_resources_bound, -Qstrip_debug, -Qstrip_reflect; profiles vs_6_0 and ps_6_0.
- An isolated local harness calls the current Renderer::LoadPaintSettings and binds the current object and six-material constant-buffer layouts. Each controlled triangle has an identity object transform and a specified constant normal. The current vertex shader performs the rotation; the current pixel shader normalizes and shades it.
- Read back floating-point RGBA render targets, so measurements describe linear shader output before the app's display encoding/8-bit quantization. This does not test every interpolation pattern in the actual scene.
- Evaluated 10,368 cases satisfying the new e=0.01 contract, including strict-boundary-adjacent colors, exact and near highlight tones, 5,406 front-facing coefficient cases, 1,200 random cases, and 270 cutoff-boundary probes. Eight additional epsilon comparisons make 10,376 total cases. All measured production RGB outputs were finite.
- The production coefficient denominator was below its floor in 287 primary cases. The largest primary sampled color error was 0.2259287834. This is a sampled maximum, not a proof of the global maximum.
- A diagnostic pixel shader exposed tone, raw coefficient denominator, facing, and normalized Z. A separate experimental pixel shader evaluated the nonnegative color denominator. Both are confined to the ignored test directory and were not installed into the application.
- Stable references preserve the stored float32 color conversion, brightness, shift, input angles, tone endpoints, and vertex normals while evaluating normalization, warp, tone interpolation, and the rational curve in double precision. Requested-input references are also recorded. The stable reference includes arbitrarily small allowed shifts; it intentionally does not apply the implementation's tiny-shift shortcut.
- [RgbConstrainedExamples.csv](RgbConstrainedExamples.csv) contains 44 named examples/comparisons, including exact normals and diagnostic denominators. Complete inputs, GPU outputs, the harness, and the analysis script are retained locally under Build/rgb-margin-numerics.

Production source hashes used by the harness:

~~~text
shaders/SimplePaint.hlsl SHA256
9123276C3E91AF8B75B7CB59BFACFD39A376078D0FF77D11854F746DDEA81C37
src/Renderer.cpp SHA256
8FEA266630AD0F2455E863C0BCEF9EF6499B0048B74E1F4EE19DA70D686AFBBF
~~~

Only the working specification, reports, links, and required conversation record were changed for this request. Runtime enforcement, application shaders, and the user's Settings.ini were not modified by this analysis.
