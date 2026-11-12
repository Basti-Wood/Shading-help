# Rendering model

## Units and color

One scene unit is the sphere's radius. UI colors are sRGB numbers in [0,1]. The exact piecewise sRGB transfer is used, including the linear segment near zero. Surface values are decoded to linear reflectance. Calculations use float RGB, not spectral wavelengths. This cannot reproduce wavelength-dependent metamerism or subsurface skin scattering.

All surfaces use `f_r = rho / pi`. There is no specular lobe or emission from meshes. Triangle normals are flat geometric normals. Geometry is opaque and double-sided; the hit normal is oriented toward the incoming ray. Spheres use exact surface normals. Ray origins are offset 0.0003 scene units along the oriented normal to avoid numerical self-intersection. This fixed tolerance is suitable for the built-in scene scale; extremely tiny geometry may lose contact detail.

## Direct lighting

For a point source, the estimate at a surface point is:

```text
L_direct = (rho / pi) * I_rgb * max(0, dot(N, L)) * visibility / distance^2
I_rgb = linear(lightColor) * intensity
```

`visibility` is zero if the shadow ray meets any opaque geometry before the source, otherwise one. Geometry behind the source does not block it. There is no artistic distance attenuation polynomial. Point intensity is relative radiant intensity, not labeled as lumens or lux.

The spot is a point emitter at radius zero. For a positive radius, a point is sampled uniformly on a disk centered on the light and perpendicular to its direction. The disk has constant one-sided radiance:

```text
disk_area = pi * radius^2
emitted_radiance_rgb = I_rgb / disk_area
area_sample_pdf = 1 / disk_area
L_direct_sample = (rho / pi) * I_rgb * cos_receiver * cos_emitter
                  * cone_factor * visibility / distance^2
```

The area and PDF cancel. Increasing radius preserves the on-axis far-field intensity, not the emitter radiance. The disk's angular cone factor uses a smooth transition between 80% and 100% of the configured cone half-angle, evaluated per sample. Radius zero omits the disk emission cosine, giving an ideal spot point source. Soft shadows result from different disk samples being occluded. The emitter is not itself added to the visible/occluding scene geometry. This is a deliberate analytic-light model; camera rays do not see a glowing disk.

The directional light has constant incident irradiance, sampled along `-lightDirection`, with no distance falloff and a shadow ray to infinity. It currently has hard shadows. Point and directional light intensities have different dimensions; switching between their modes does not preserve brightness.

## Diffuse path transport

At each surface vertex the renderer explicitly samples the analytic light above. It then samples a cosine-weighted outgoing direction on the normal's hemisphere:

```text
pdf(direction) = max(0, dot(N, direction)) / pi
throughput *= f_r * cosine / pdf = rho
```

If that ray hits another surface and the surface-bounce budget remains, the path continues and includes that surface's direct illumination. If it misses, it contributes the constant environment radiance multiplied by path throughput. Thus floor color bleed, indirect occlusion and self-shadows come from actual scene intersections.

The environment is counted by these hemisphere escape rays only; it is not added a second time as an ambient constant. The analytic light is counted by explicit sampling only; it is absent from geometry, so there is no second emissive-hit contribution or need to combine the two estimators with MIS. The random estimator is correct for the truncated diffuse transport model, apart from floating-point tolerances. The finite bounce limit intentionally omits longer light paths; it is not a claim of unbiased infinite-bounce global illumination.

At zero bounces, the first surface still samples environment visibility. If that environment ray is blocked, it contributes zero. Direct + environment includes only illumination arriving at the first surface without a preceding surface reflection. Bounce only includes paths that have at least one additional surface reflection. Their sum equals the combined pass. The background appears only in the direct pass.

The 0–8 bounce setting counts additional surface reflections beyond the visible surface. There is no ambient-occlusion multiplier, painted terminator gradient, shadow hue overlay, or bounce-light ramp. Constant environment radiance is a lighting assumption, not a screen-space color trick.

## Display and sampling

Radiance samples are averaged in linear space. Exposure multiplies the average by `2^exposure`. Grayscale uses linear luminance coefficients 0.2126, 0.7152, 0.0722. The result is clamped to [0,1] and encoded to sRGB for an 8-bit display image. Albedo and normal inspection passes bypass exposure. High values clip deliberately; reducing exposure reveals them. There is no hidden ACES/Reinhard/contrast curve.

There is no denoiser. Shadow edges, area lights and bounces converge as samples accumulate. Dragging starts a lower-resolution job; releasing starts a new full-resolution accumulation. Every settings change discards the old accumulation. Samples have deterministic per-pixel seeds, independent of worker order. A BVH accelerates triangle intersection. Spheres and the infinite ground are intersected analytically.

## GPU backend

The Windows app uses a Direct3D 11 compute shader (`cs_5_0`) for camera rays, BVH traversal, analytic sphere/ground intersections, direct-light shadow rays, diffuse hemisphere bounces, progressive accumulation, exposure, grayscale and sRGB conversion. It implements the same estimator as the CPU reference. This is general compute ray tracing; it does not require DXR or an RTX GPU.

Geometry is uploaded as structured buffers. The renderer reuses BVH geometry when only the camera, light or material changes. Each dispatch covers a strip of at most 64 rows, with cancellation checks between strips. Completed passes are synchronized to bound queued work and interaction latency. Readback supplies the native Win32 viewport, image export and the numeric pixel inspector. UI drawing, import, scene preparation and picking remain CPU work.

Hardware selection excludes software adapters and prefers compatible adapters with more dedicated memory. Direct3D feature level 11_0 is required. If initialization, shader compilation or device execution fails, the app switches to the CPU renderer and exposes the reason in View. The CPU option can also be selected explicitly.

The build tests HLSL on Microsoft's WARP software device, comparing linear radiance and display conversion with CPU results across lights, models, views, bounce settings and awkward image dimensions. A constant-environment test checks absolute normalization, and a reset test checks that previous accumulation cannot leak into a changed scene. Floating-point differences can change individual stochastic paths, so image comparisons use a numeric tolerance. WARP is a shader correctness check, not a physical GPU benchmark; a physical adapter is tested separately if available on the runner.

## References

- [Physically Based Rendering, 4th edition: A Better Path Tracer](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer) — rendering-equation and path-sampling reference. This app implements a smaller diffuse-only analytic-light estimator, not PBRT's full integrator.
- [Microsoft: C++ command-line build tools](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line) — Windows compiler setup.

Numerical checks in `tests/tests.cpp` test externally meaningful invariants (distance, angle, energy and visibility), not just screenshots or implementation snapshots.
