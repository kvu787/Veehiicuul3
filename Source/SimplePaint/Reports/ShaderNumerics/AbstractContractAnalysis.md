# SimplePaint: preserving the mathematics while improving evaluation

Date: 2026-09-05. Examined HEAD: 2f0659a.
Scope: interface and mathematical analysis, with proposed implementation directions. Application code and active settings are unchanged.

Historical scope: the working proposal examined here was subsequently replaced by the [implemented specification](../../Specification.md). That proposal used open RGB and Brightness intervals, Shift in [0, 1), Rotation in [0, 360), Dark Point in [0, 1), and Light Point in (0, 1]. At that time, no input margins had been selected, and K12's fixed facing cutoff remained q = 0.01.

## What the new domain resolves

With c the linear base channel and p Brightness, A = c*p and B = (1-c)*(1-p) are positive. For every tone t in [0, 1], the exact color denominator lies between A and B:

~~~text
min(A,B) <= A*t + B*(1-t) <= max(A,B)
~~~

It cannot be zero. The correct color endpoints are therefore unambiguously F(0) = 0 and F(1) = 1. No artistic rule for black/white 0/0 corners is needed inside this domain. Exact RGB endpoints and Brightness endpoints are excluded; Shift = 1 and enormous rotations are also excluded.

Dark Point = 0 and Light Point = 1 remain useful and mathematically safe. There is no need to constrain tone away from zero and one merely to make this color curve well-defined.

An open interval alone does not disable the current CPU clamps: sufficiently small legal positive colors or brightness values are still changed, as are legal shifts sufficiently close to one. The domain resolves the mathematical endpoints, not the current implementation's approximations.

## Changes that preserve the real-number function

| Current representation or behavior                 | Proposed direction                                                                 | What it fixes                                    |
| -------------------------------------------------- | ---------------------------------------------------------------------------------- | ------------------------------------------------ |
| Signed coefficients k2 and k3 nearly cancel        | Evaluate A*t and B*(1-t) as nonnegative contributions                              | Coefficient and denominator cancellation         |
| Fixed 1e-5 color-denominator floor                 | Divide by the actual positive sum within a validated numerical domain              | Artificial dimming and hue changes               |
| Only Light Point minus Dark Point is retained      | Store both endpoints; use d*(1-f) + l*f                                            | Loss of a small endpoint in a reversed range     |
| A single rounded tone is later subtracted from one | Preserve t and its complement through equivalent expressions when needed           | Premature loss of distances from a tone endpoint |
| Shift root sqrt(1-s*s)                             | Compute sqrt((1-s)*(1+s)), preferably during double-precision material preparation | Subtraction of a rounded square near s = 1       |
| Shift denominator r-s*x                            | Use z*z/(r+x) + (1-s)*x for x >= 0; use r-s*x for x < 0                            | Avoidable cancellation in the warp               |
| Every Shift <= 1e-5 is treated as zero             | Use an exact s = 0 shortcut; evaluate all supported positive shifts                | Unspecified dead band and its boundary           |
| Input values are silently pushed inward            | Explicit supported ranges and validation before GPU upload                         | Unspecified material changes                     |
| Float32 parsing and coefficient preparation        | Consider double-precision preparation followed by deliberate GPU representation    | Avoidable loss before shader evaluation          |

These are design directions, not an instruction to remove protections from the current code in isolation. Positive mathematical quantities can still round or underflow to zero; choose a supported domain and representation together.

CPU precomputation and vertex rotation can remain. A per-material orthogonal rotation commutes with normal interpolation under the current per-triangle material and orthographic assumptions. None of these mathematical changes requires moving it back into the pixel shader.

The shader compiler must preserve numerically significant evaluation choices. Inspect optimized DXIL and use appropriate precision controls where needed; the HLSL precise qualifier constrains reordering but does not add mantissa bits. See [Microsoft's HLSL variable documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax).

## Positive coefficients and endpoint distances

A small positive denominator is not by itself a reason for inaccurate output. The numerator and denominator can be small together, and their ratio is bounded. Avoiding cancellation lets small nonnegative terms retain useful relative accuracy, provided they remain representable.

The common scale of A and B is irrelevant:

~~~text
scale = max(A,B)
a = A/scale
b = B/scale

F(t) = a*t / (a*t + b*(1-t))
~~~

Preparing scaled coefficients in higher precision is an option for extending useful dynamic range. It does not rescue a ratio already lost to underflow, and does not remove the need for input bounds.

The positive denominator alone does not fix all earlier errors. If t has rounded to one, subsequently calculating 1-t gives zero. Preserve the two endpoint distances instead:

~~~text
u = 1-f
t = d*u + l*f
v = (1-d)*u + (1-l)*f
color = A*t / (A*t + B*v)
~~~

In exact arithmetic v = 1-t, so this is the same shader. Merely computing u by subtracting an already rounded f from one can repeat the problem one stage earlier.

For a unit admitted normal at Shift = 0, a useful identity is:

~~~text
1-z = (x*x + y*y)/(1+z)
~~~

For an unnormalized admitted vector (X,Y,Z) with length m, the corresponding identity avoids assuming a rounded normal has exact unit length:

~~~text
1-Z/m = (X*X + Y*Y)/(m*(m+Z))
~~~

For nonzero shift the remapped horizontal coordinate w satisfies:

~~~text
w = r*(x-s*r)/(r-s*x)
w*w + y*y + f*f = 1
1-f = (w*w + y*y)/(1+f)
~~~

These identities can preserve small highlight distances without changing the lobe or curve. Their actual implementation cost and floating-point accuracy require measurement. They do not recover geometric information already lost in vertex storage, transforms, or interpolation.

## Stronger bound for the shifted-facing denominator

The earlier reports used the valid but sometimes loose bound r-s*x >= q*sqrt(1-s*s). Its approach to zero as s approaches one does not prove that the actual denominator approaches zero under the fixed cutoff.

For any unit normal admitted by the cutoff, z >= q, r <= 1, and x <= sqrt(1-q*q). If x >= 0:

~~~text
r-s*x >= r-x
      = z*z/(r+x)
      >= q*q/(1+sqrt(1-q*q))
~~~

For x < 0, r-s*x >= r >= q, which is a stronger bound. Since s < 1, the infimum for the complete domain is approached rather than attained:

~~~text
r-s*x > q*q/(1+sqrt(1-q*q))
      = 1-sqrt(1-q*q)
      = approximately 0.0000500012500625039   when q = 0.01
~~~

This is above the current 1e-5 floor for every allowed shift in exact arithmetic, without choosing a Shift margin. The denominator floor is mathematically unnecessary here. The bound assumes the specified cutoff and an exactly normalized real vector; it is not a complete floating-point error proof.

For completeness, the sharp minimum over admitted normals at a fixed s is:

~~~text
q*sqrt(1-s*s)                 if s <= sqrt(1-q*q)
1-s*sqrt(1-q*q)               otherwise
~~~

At s = 0.99 this minimum is 0.0014106735979665884. At s = 0.99999 it is 0.00006000075005000328.

A Shift margin may still be useful for representability, sensitivity, and visual usability. As s approaches one, h = sqrt(1-s*s) becomes sensitive to s even though this denominator has a positive lower bound. Once sqrt(1-s*s) < q, the original full-height peak at normal (s,0,sqrt(1-s*s)) falls below the cutoff and is rejected. This is inherited behavior, not a denominator defect.

## Fundamental limits that remain

**There is no uniform conditioning bound over the entire open color domain.** The derivative with respect to tone is:

~~~text
F'(t) = A*B / (A*t + B*(1-t))^2
F'(0) = A/B
F'(1) = B/A
max over 0 <= t <= 1 of F'(t) = max(A/B, B/A)
~~~

Those ratios can become arbitrarily large as allowed c and p approach excluded endpoints. Algebraic reformulation does not change these derivatives. It can, however, substantially improve how the small endpoint distances are represented, so steepness alone does not prove a particular visible error is inevitable.

For user-facing sRGB C = 0.01 and p = 0.01, B/A = 127809. With exact tone t = 0.99999999, the exact linear output is approximately 0.9987235414. Rounding only that tone to binary32 gives 1, and then even an exact color calculation returns 1. These controls and this tone are within the new abstract domain; for example Shift = 0, Dark Point = 0, Light Point = 1, and a unit normal with z = t realize it. This is a CPU arithmetic example, not a new GPU measurement.

There is no unique continuous extension at every excluded color corner. With p = 0.5, c approaching zero, and 1-t = k*c, the limit of F is 1/(1+k). Different paths give different answers. Excluding the corner removes the undefined input but does not remove the increasingly narrow transition near it.

**Finite input and intermediate representations have limits.** A decimal strictly below one can round to one. Tiny positive parameters can become zero, and even representable positive parameters can have an unrepresentable product. For example, positive normal-size float32 values near c = 1e-20 and p = 1e-20 give A near 1e-40; at t = 1 the mathematical output is one, but an unscaled A can flush to zero and produce 0/0. An sRGB input near 12.92e-20 supplies that linear color. Thus underflow cannot always be dismissed as a visually negligible dark-output error.

Direct3D documents denormal flushing and finite instruction accuracy; [Microsoft's floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules) describe these limits. Stable formulas, scaling, endpoint evaluation, and stronger representations can extend the useful range; no fixed finite-precision representation covers every real input in the open domain with arbitrary accuracy.

**The hard facing cutoff is an existing discontinuity.** With linear c = 0.5, p = 0.5, d = 0, l = 1, s = 0, color jumps from zero just below z = 0.01 to 0.01 at the cutoff. With s = 0.99, Rotation = 0, and normal (sqrt(1-z*z),0,z), the right-hand color at the cutoff is approximately 0.1403724986. The cutoff can therefore have a substantial jump for entirely interior controls. Smoothing it would change the specified function.

Branch classification near the cutoff is sensitive to normal rounding. Positive margins on artistic inputs cannot guarantee a small uniform normal-to-color error across a discontinuity.

**Normal validity is a renderer input requirement.** The renderer supplies a finite, unit-length surface normal in the required coordinate frame. Producing that normal belongs to the geometry and rendering pipeline, not to SimplePaint's artistic input constraints or epsilon selection.

## Accepted non-issue: image sampling

The user has explicitly accepted finite image sampling and undersampling of narrow highlights as a non-issue. Sampling is excluded from the remaining SimplePaint issues and epsilon-selection criteria; it does not create a requirement to change the shader mathematics or add antialiasing/filtering work. Do not revisit this settled topic unless the user explicitly reopens it. See the [implemented specification's numerical rationale](../../Specification.md#accepted-machine-inputs).

## Choosing margins after the numerical design

Let the eventual supported linear-color bounds be c_min <= c <= c_max, and brightness bounds p_min <= p <= p_max, all strictly inside (0,1). Then:

~~~text
color denominator >= min(c_min*p_min, (1-c_max)*(1-p_max))

maximum tone slope <= max(
    c_max*p_max / ((1-c_max)*(1-p_max)),
    (1-c_min)*(1-p_min) / (c_min*p_min))
~~~

The second bound, combined with upstream absolute tone error, bounds the resulting color error from that source by the mean value theorem. Coefficient error, input conversion, geometry error, and cutoff branch changes need their own treatment. Preserving complementary distances can yield a much better error model than treating all upstream error as an error in one rounded scalar tone.

First choose what accuracy means: absolute linear-channel error, displayed sRGB error, or another stated visual criterion. Distinguish error against exact requested inputs from error against the stored material and geometric inputs. Then select margins and representations to meet that criterion, with tests near boundaries and on the optimized GPU shader. Separate margins may retain more usable range than a single shared epsilon.

The previous shared-margin threshold near 0.01137 was a workaround for the current denominator floor. It is not a reason to keep that floor or a lower limit for a correctly reformulated shader.

## Evidence and limitations

- Read the supplied prior conversation, local K12 source, working specification, current HLSL, CPU settings conversion, and historical numerical reports.
- Current production hashes match those in the previous RGB-margin report: HLSL 9123276C3E91AF8B75B7CB59BFACFD39A376078D0FF77D11854F746DDEA81C37; Renderer.cpp 8FEA266630AD0F2455E863C0BCEF9EF6499B0048B74E1F4EE19DA70D686AFBBF.
- The denominator and derivative bounds above are algebraic arguments. Decimal arithmetic at 70-digit precision checked the numerical values.
- Checked 1,000 rational-color identity samples with 70-digit Decimal arithmetic; the two expressions agreed to the available precision.
- Checked 5,000 ordinary admitted-normal/shift samples in CPU double precision. The original slice/Schlick/remap and stabilized facing expressions differed by at most approximately 5.07e-14; the complementary-facing identity differed by at most approximately 1.43e-14.
- These samples are sanity checks, not a proof of a floating-point error bound or an exhaustive domain test.
- No new GPU build, GPU accuracy sweep, or performance benchmark was performed for this specification revision. Earlier GPU results remain evidence about the unchanged production shader and their stated input proposals.
