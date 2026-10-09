# SimplePaint analysis under the constrained input specification

Date: 2026-09-05. Production implementation unchanged from the preceding numerical review.

Historical scope: this report analyzes the earlier proposal with RGB in [0,1]. The later [RGB-constrained analysis](RgbConstrainedAnalysis.md) superseded it for strict RGB margins; the [implemented specification](../../Specification.md) defines the current contract. This report's original examples and conclusions remain a record of that earlier domain.

The previous input specification used e=0.01, RGB in [0,1], Brightness in [e,1-e], Shift in [0,1-e], Rotation in [0,360], DarkPoint in [0,1-e], and LightPoint in [e,1]. All bounds are inclusive. DarkPoint may exceed LightPoint.

The user's local K12.gdshader was read directly. It fixes the normal-facing threshold at 0.01 on line 107. This analysis therefore holds the DirectX port's FacingCutoff at 0.01. It does not count the previous FacingCutoff=0 examples as failures under the new contract.

Conclusion: the constraints substantially simplify the shifted-highlight calculation and remove several input-clamp/rotation cases. They do not eliminate the color-denominator error, base-color perturbations, or all sensitivity to rounding. No feasible choice of e alone can eliminate the original color-denominator error.

**Meaning of the fixed facing threshold**

K12 compares the corrected normal's camera-facing component against 0.01. A smaller value sets the paint facing to zero, making the tone DarkPoint. The fragment remains present. This fixed K12 constant q=0.01 is independent of the proposed input margin e=0.01 and the existing DirectX numerical floor delta=0.00001.

**Measured examples that remain legal**

All examples use Rotation=0. Unless a different Shift is specified, Shift=0. C is applied to each Body sRGB channel. N is the view-space surface normal supplied to the pixel shader, before normalization.

| Example                                                | Brightness | C        | DarkPoint | LightPoint | N or behavior                                          | Current linear output | Reference  |
| ------------------------------------------------------ | ---------: | -------: | --------: | ---------: | ------------------------------------------------------ | --------------------: | ---------: |
| Original dark-channel failure, adapted to the contract | 0.01       | 0.001    | 0         | 1          | N=(0,0,1)                                              | 0.07739938            | 1          |
| Stronger dark-channel case                             | 0.01       | 0.0002   | 0         | 1          | N=(0,0,1)                                              | 0.01547988            | 1          |
| Counterexample valid for every feasible e              | 0.5        | 0.0002   | 0         | 1          | N=(0,0,1)                                              | 0.77399379            | 1          |
| Denominator cancellation with floor inactive           | 0.5        | 0.000259 | 0         | 1          | N=(0,0,1)                                              | 0.99799234            | 1          |
| Black input altered by CPU clamp                       | 0.99       | 0        | 0.99      | 0.99       | Any orientation                                        | 0.08926324            | 0          |
| White input altered by CPU clamp                       | 0.5        | 1        | 0.0001    | 0.01       | N=(0,0,-1), selects DarkPoint                          | 0.90898609            | 1          |
| Near-white denominator-floor case                      | 0.99       | 0.99999  | 0.000001  | 0.01       | N=(0,0,-1), selects DarkPoint                          | 0.09899775            | 0.81301181 |
| Front-facing near-white floor case                     | 0.99       | 0.99999  | 0         | 0.01       | Shift=0.99; N approximately (-0.9999499887,0,0.010001) | 0.70186341            | 0.96857884 |

References for denominator/cancellation cases preserve the stored float32 parameters and existing CPU color conversion while evaluating the curve stably. References for black/white input-clamping cases preserve the requested exact channel endpoint. Those black/white examples have well-defined formulas: their tones are strictly between zero and one.

The original earlier reproduction used DarkPoint=LightPoint=1, which the new specification excludes. Its adapted version uses DarkPoint=0, LightPoint=1, and a front-facing normal. That reaches the same tone=1 through legal inputs.

The fixed cutoff is meaningful for the surface-dependent examples, but does not protect the dark-channel bright endpoint. Normal=(0,0,1) is admitted, and Shift=0 yields facing=1.

These controlled normals are numerical tests of the shader input domain, not assertions that every one occurs on a visible pixel of the shipped car.

**Why the new tone constraints do not keep tone away from zero and one**

Let f be the computed facing in [0,1]. The mathematical tone is:

~~~text
t = (1-f)*DarkPoint + f*LightPoint
~~~

DarkPoint=0 remains allowed, and f=0 occurs on normals below the fixed cutoff, so t=0 remains reachable. LightPoint=1 remains allowed, and f=1 occurs for an exactly front-facing normal with Shift=0, so t=1 remains reachable.

One-sided bounds on DarkPoint and LightPoint are therefore different from constraining the resulting tone to [e,1-e]. Making e smaller does not change that distinction.

**Proof that changing e alone cannot eliminate the color-floor error**

Write beta for Brightness and b for one linear base-color channel. In exact arithmetic:

~~~text
U = b*beta*t
V = (1-beta)*(1-b)*(1-t)
color = U/(U+V)
~~~

At t=1 and b>0, beta>0, the intended result is U/U=1.

Consider these fixed inputs:
- Brightness=0.5;
- Body=(0.0002,0.0002,0.0002), in sRGB;
- DarkPoint=0, LightPoint=1;
- Shift=0, Rotation=0;
- N=(0,0,1).

They satisfy the proposed constraints for every e with 0<e<=0.5. For e>0.5, the Brightness interval is empty.

The base channel converts to approximately b=0.000015479876. It is above the existing CPU's 0.00001 color clamp, so this counterexample does not depend on that clamp. The correct denominator at t=1 is approximately 0.000007739938, which is smaller than the shader floor of 0.00001. The implemented result is approximately 0.7739938 rather than 1, as confirmed on the GPU.

Thus there is no feasible e that, by itself, removes this particular error while retaining the specified RGB range and the allowed LightPoint endpoint. This is a constructive mathematical counterexample, not an inference from a parameter sweep.

For any positive brightness, the unconstrained distance of RGB from black also prevents a uniform positive lower bound on the unregularized color denominator at tone=1.

**What the constraints eliminate or improve**

| Previous issue                                                     | Effect of the new contract                                                                         |
| ------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------- |
| Brightness clamped up from zero or tiny values                     | Excluded at e=0.01; every allowed brightness is well inside the implementation's 1e-5 protections. |
| Brightness clamped down from one                                   | Excluded.                                                                                          |
| Shift clamped down from one or 0.999999                            | Excluded; maximum Shift is 0.99.                                                                   |
| Mathematical Shift=1 pole                                          | Excluded.                                                                                          |
| Large rotation losing whole degrees before reduction               | The former huge-angle cases are excluded by Rotation<=360. Ordinary float rounding still exists.   |
| Tiny LightPoint lost in a reversed tone range                      | The earlier LightPoint=1e-8 case is excluded.                                                      |
| Shift-denominator floor at grazing normals                         | Inactive under the fixed cutoff and e=0.01, with a large mathematical margin shown below.          |
| Strong cancellation in the shift denominator and root near Shift=1 | Greatly reduced by Shift<=0.99; not a claim of bit-exact arithmetic.                               |
| Color-denominator floor                                            | Remains; counterexamples above.                                                                    |
| Cancellation in the color denominator                              | Remains, including cases where the floor is inactive.                                              |
| Black/white CPU color clamps                                       | Remain; RGB still includes endpoints and nearby colors.                                            |
| Small nonzero shifts treated as zero                               | Remains, since arbitrarily small positive Shift is still allowed.                                  |
| Tiny values flushing to zero                                       | Tiny DarkPoint is still allowed; only the tiny-LightPoint reproduction was excluded.               |
| Undefined color endpoints                                          | Two intersections remain: b=0 with t=1, and b=1 with t=0.                                          |
| Near-endpoint sensitivity to ordinary rounding                     | Remains because b and 1-t, or 1-b and t, may still approach zero.                                  |

**A provable improvement for the shifted-highlight denominator**

Let s=Shift, x be the rotated horizontal normal component, z the camera-facing component, and m=sqrt(x*x+z*z). The mathematical denominator is D=m-s*x.

For 0<=s<1:

~~~text
D >= 0
D*D - z*z*(1-s*s) = (x-s*m)*(x-s*m) >= 0
D >= abs(z)*sqrt(1-s*s)
~~~

The shader enters the facing branch only when z>=q, with q=0.01. Since s<=1-e:

~~~text
sqrt(1-s*s) >= sqrt(e*(2-e))
D >= q*sqrt(e*(2-e))
~~~

At e=0.01:
- The square-root factor is at least approximately 0.14106736.
- The shifted denominator is at least approximately 0.001410674.
- That is approximately 141 times the current denominator floor.

This is an exact-arithmetic bound for the normalized input model. It is not a formal certificate for every compiler, GPU intrinsic, or floating-point rounding mode, but the margin at e=0.01 is substantial. The measured fixed-cutoff warp cases were consistent with it.

The lower bound remains useful for smaller e:

| Input margin e | Shift upper bound | Shift-denominator lower bound | Multiple of current 1e-5 floor |
| -------------- | ----------------: | ----------------------------: | -----------------------------: |
| 0.01           | 0.99              | 0.001410674                   | 141.1                          |
| 0.005          | 0.995             | 0.000998749                   | 99.9                           |
| 0.001          | 0.999             | 0.000447102                   | 44.7                           |
| 0.0001         | 0.9999            | 0.000141418                   | 14.1                           |
| 0.00001        | 0.99999           | 0.000044721                   | 4.47                           |
| 0.000001       | 0.999999          | 0.000014142                   | 1.41                           |

The final row is a bound on the requested mathematical warp. With the current implementation, values above its own maximum shift are clamped, so an e below 1e-5 also reintroduces a separate input-policy mismatch.

These bounds address the shifted denominator only. They do not establish that a smaller e is safe for the whole shader. Approximately 5.00000125e-7 is the exact-arithmetic threshold where this lower bound equals 1e-5; operating near that threshold would leave essentially no margin for arithmetic error.

**Remaining precision effects, with legal examples**

1. Color subtraction: Brightness=0.5, C=0.000259, DarkPoint=0, LightPoint=1, N=(0,0,1) gives 0.99799234 rather than 1. Both the stable and computed denominators exceed the floor. The nonnegative color experiment gives 1.

2. Tone/normal rounding can still be amplified near the bright endpoint. Brightness=0.5, C=0.000259, DarkPoint=0.99, LightPoint=1, N approximately (0.0141417821,0,0.9999) gives 0.95006704 versus a stable stored-input reference of 0.95248693. The experimental positive color formula gives 0.95188630: it fixes the final subtraction but does not remove upstream tone/normal rounding. The new ranges do not bound tone away from 1.

3. Reversed tone range: neutral gray C=0.7353569830524495, Brightness=0.5, DarkPoint=0.99, LightPoint=0.01, N=(0,0,1) gives about 0.009999996. The desired decimal tone is 0.01. The former catastrophic loss of a 1e-8 LightPoint is excluded, but small rounding remains.

4. Tiny shift: neutral gray, Brightness=0.5, Shift=0.00001, DarkPoint=0, LightPoint=1, N=(0.6,0,0.8) gives 0.80000007 versus a stored-input reference of 0.80000487. This is the unchanged approximate fast path.

5. Underflow: neutral gray, Brightness=0.5, DarkPoint=1e-40, LightPoint=0.01, N=(0,0,-1) produces 0 rather than approximately 1e-40. It is still legal because DarkPoint has no positive lower margin. This has no meaningful visible effect.

6. Normals below the cutoff still select DarkPoint abruptly. That discontinuity is inherited K12 behavior, not a newly demonstrated arithmetic defect. Valid nonzero normals and the current nondegenerate uniform transform remain assumptions; the new scalar ranges alone cannot validate mesh normals.

**What is still unspecified at RGB endpoints**

Because e<=beta<=1-e, both beta and 1-beta are positive. The color denominator can vanish only at:
- b=0 and t=1;
- b=1 and t=0.

Both are still reachable through allowed parameters. The current port's color perturbations/floors choose particular outputs, but the unconstrained rational formula gives 0/0 there. Those points need an explicit desired behavior if the specification is to define every accepted input.

The nonsingular counterexamples in the main table avoid these intersections. They remain errors even after the undefined endpoints are set aside.

**Can input restrictions alone provide a stronger color guarantee?**

Changing only e in the proposed contract cannot, as proved above. Additional restrictions could establish useful bounds, but are not adopted here:

- If both DarkPoint and LightPoint were constrained to [e,1-e], then t would also remain in that interval. With beta in [e,1-e], the unregularized color denominator would be at least e*e for every b in [0,1]. At e=0.01, this bound is 0.0001, ten times the floor. This would remove the two undefined color corners and prevent the denominator floor in exact arithmetic. It would still not undo the existing CPU black/white perturbations: the legal black constant-tone example above uses t=0.99 and already demonstrates that remaining issue.
- Alternatively, bounding each linear base channel to [c,1-c] gives denominator >= e*c even if t reaches zero or one. At e=0.01, c must exceed approximately 0.001 to put that lower bound strictly above 1e-5, before adding a rounding margin. Such a restriction removes exact black/white and part of the dark-color range. It is a linear-color bound, not the same numeric bound on user-facing sRGB.
- Neither lower-bound argument alone proves an arbitrarily tight output-error tolerance. Cancellation, input perturbation, and rounding still need to be measured or bounded against a defined criterion.

These are possible specification changes for discussion, not changes made to the user's requested contract.

**Verification**

The unchanged production pixel shader was evaluated using the existing isolated GPU harness and production CPU parameter loading. The final fixed-cutoff analysis contains 4,973 contract-compliant test cases and 40 additional epsilon-comparison cases. Preliminary adjustable-cutoff diagnostic rows were excluded after the local K12 behavior was confirmed.

All final production outputs in the analyzed set were finite. This is not an exhaustive proof over all float inputs. The current sample's maximum difference from a stable same-stored-parameter reference was approximately 0.99 in the color grid. In the structured neutral-material warp set it was about 1.053e-6. These errors concern different stages; a good warp result does not imply a good final color result.

A pre-existing experimental positive-color shader was used only to distinguish final-denominator errors from upstream rounding; no new implementation was installed. Exact values for named examples and epsilon comparisons are in [ConstrainedExamples.csv](ConstrainedExamples.csv). Full local inputs/results remain in the ignored Build/shader-numerics directory.

The contract, this analysis, and the example data are documentation changes. The DirectX renderer, shader, active CarPaint.ini, and external Godot K12 file remain unchanged.

**Decision supported by the evidence**

Retain e=0.01 as the working input margin while deciding the remaining specification details. It removes several problematic domains and gives a strong shift-denominator margin. Do not label it, or a smaller value, globally error-free for the current implementation.

There is no smallest globally safe epsilon to find under these exact constraints: the same nonzero-color counterexample remains legal at every feasible epsilon. Selecting a smaller value becomes a meaningful accuracy exercise only after the surviving color behavior and the acceptable error criterion are resolved.
