# NULLPOINT: research before the expansion

Research completed on 13 October 2026 against the existing 95,232-byte Windows x64 build, commit `a589e6b`. The executable budget remains strictly **less than 96,000 bytes**. Measurements below are local experiments, not estimates from articles.

## Findings and decisions

| Area | Evidence | Decision for this engine |
| --- | --- | --- |
| Procedural assets | Farbrausch's original tools build assets from reusable operations. | Extend compact model descriptions and reuse generated meshes; keep assets inside the executable. |
| Compiler size | GCC documents `-Oz` as a size-first alternative to `-Os`. Six local combinations were measured. | Use the smaller measured setting, not assumptions about flags. Retain the normal PE loader and runtime. |
| Function calls | GCC supports separate internal System V and Windows calling conventions. | An isolated internal-ABI probe saved 2,048 bytes and passed all existing tests. Windows callbacks and imported APIs retain their Windows ABI. |
| Embedded text | Windows provides buffer compression/decompression through Cabinet.dll. | Compress shader data at build time and expand it into memory. This does not pack or replace the native executable's machine code. Validate decompression size. |
| Material lighting | Filament describes roughness, Fresnel reflectance, microfacet highlights, and energy limits. | Use a compact roughness-aware material model and restrained exposure. Preserve detail instead of making everything glow. |
| Reflections | Screen-space tracing can reuse depth and color, but cannot recover off-screen or occluded objects. | Add bounded floor reflections with thickness checks and edge fading. Retain a subdued fallback when rays miss. |
| Physics | Fixed updates and semi-implicit integration improve reproducibility; swept/substepped collision avoids tunneling. | Use fixed simulation steps, acceleration/friction, vertical support surfaces, and bounded grenade substeps. |
| Game expansion | The existing campaign has three relays, three weapons, one boss, and a direct extraction. Its automated run takes 54.3 seconds. | Add tactical interactions and optional exploration: grenades, chain-reacting barrels, two side wings, weapon capacitors, and a final extraction defense. |

## Measured size probes

All probes used the unchanged game source, GCC 13.2.0, and the existing Windows system-library set.

| Compiler configuration | Executable bytes |
| --- | ---: |
| Existing `-Os` | 95,232 |
| `-Oz` | 94,720 |
| `-Oz -fno-inline-functions-called-once` | 95,232 |
| `-Os -fno-inline-functions-called-once` | 95,744 |
| `-Oz -fmerge-all-constants` | 94,720 |
| `-Oz` plus both options above | 94,720 |
| `-Os` with both measured inlining options disabled | 97,792 |
| `-Oz`, internal System V ABI, Windows API boundaries unchanged | 93,184 |

The ABI probe passed gameplay, full campaign, camera-direction and native-rendering checks. Its isolated rendering timing is not a speed comparison. The final build must be benchmarked and revalidated separately.

A preliminary 4,864-byte shader-text sample compressed to 2,186 bytes with MSZIP, 3,149 with XPRESS, 2,614 with XPRESS-Huffman, and 2,610 with LZMS. Every sample passed a byte-for-byte decompression test. MSZIP is the measured choice for this small text sample; the complete shader package will be measured again during implementation.

## Why these limits matter

Crinkler is a purpose-built compressing linker and is useful size-coding research. Its documentation also explains the PE-layout techniques and compatibility tradeoffs involved. This update keeps an ordinary uncompressed native PE executable; only embedded shader text is compressed. No third-party game runtime or external asset download is introduced.

Physics changes require testing across render rates, on support surfaces, at wall corners, and through grenade bounces. Input edges must survive render frames that produce no simulation step. Catch-up work must be bounded without turning a pause into a large physics step.

Screen-space effects remain approximations. Reflections must fade at missing data and screen boundaries. Contact shading must use reconstructed view-space positions and avoid large halos. More geometry should be generated once, then reused, rather than rebuilt every frame.

The development test runner can be larger than the shipping game. Tests exercise the same game source; the exact shipping executable retains native rendering verification. Both build variants must be checked, and the playable artifact alone must pass the byte limit. Source size and executable size are reported separately.

## Primary sources

- [Farbrausch's original procedural tools and source](https://github.com/farbrausch/fr_public) — generative asset architecture; no source or artwork is copied into NULLPOINT.
- [GCC 13.2 optimization options](https://gcc.gnu.org/onlinedocs/gcc-13.2.0/gcc/Optimize-Options.html) — size optimization and measured compiler alternatives.
- [GCC x86 function attributes](https://gcc.gnu.org/onlinedocs/gcc-13.2.0/gcc/x86-Function-Attributes.html) — internal ABI attributes and Windows boundaries.
- [Crinkler manual](https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt) — executable compression and compatibility context.
- [Microsoft: using the Compression API](https://learn.microsoft.com/en-us/windows/win32/cmpapi/using-the-compression-api) — algorithms, buffer mode, and stored sizes.
- [Microsoft: CreateDecompressor](https://learn.microsoft.com/en-us/windows/win32/api/compressapi/nf-compressapi-createdecompressor) — supported native decompression API.
- [Filament rendering reference](https://google.github.io/filament/main/filament.html) — roughness, Fresnel response, and material energy behavior.
- [McGuire and Mara: screen-space ray tracing](https://casual-effects.blogspot.com/2014/08/screen-space-ray-tracing.html) — depth-buffer ray tracing and its limitations; NULLPOINT uses its own bounded implementation.
- [NVIDIA: high-quality ambient occlusion](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-12-high-quality-ambient-occlusion) — local occlusion, sampling, and distance falloff.
- [Glenn Fiedler: Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/) — fixed updates, accumulation, and bounded catch-up.
- [Glenn Fiedler: Integration Basics](https://gafferongames.com/post/integration_basics/) — integration order and numerical behavior.
- [Box2D simulation documentation](https://box2d.org/documentation/md_simulation.html) — timestep, restitution, friction, and tunneling principles; Box2D itself is not a dependency.
- [Khronos OpenGL registry](https://github.com/KhronosGroup/OpenGL-Registry) — framebuffer, shader, derivative, and texture contracts.

These references guide the implementation; final benefits must be supported by the actual build and runtime checks, not by the references alone.

## Implemented outcome

The final shipping executable is **93,696 bytes**, compared with 95,232 before this work. The initial compaction stage reached 84,992 bytes. The expanded game therefore remains 1,536 bytes smaller than the previous release and leaves **2,304 bytes** below the strict limit, three times the previous headroom.

The final shader package is 5,973 bytes before MSZIP and 2,684 after it. The build verifies a byte-for-byte compression round trip. The normal PE loader, standard runtime, symbolic system imports, and 64-bit target are retained. No executable packer, downloader, or dropped helper is used. The ABI probe's 2,048-byte saving was relative to the original build; `-Oz` alone accounted for 512 of those bytes.

The implementation uses table-driven map/entity generation, one shared OpenGL entry-point list, cached uniform locations, reusable chamfered model meshes, and correct back-face winding/culling. Development-only simulation tests are built separately; the shipping executable retains native renderer and input verification.

The initial screen-space reflection prototype visibly broke up thin reflected details. The final renderer instead mirrors the scene across the horizontal floor plane, clips geometry below the plane, and renders into a half-resolution color/depth target. This matches the actual level geometry better and includes objects absent from the main camera image. It remains a floor-specific approximation, with filtered half-resolution reflections rather than arbitrary-surface ray tracing.

A lighting defect produced thousands of black pixels when rounding let a Fresnel power base become slightly negative. Clamping normalized dot products to their valid range removed the defect. The final shader also avoids differentiating an already derivative-derived normal. These choices follow the [Khronos shader contracts](https://github.com/KhronosGroup/OpenGL-Registry): invalid power inputs and higher-order derivatives have undefined results. An image check now catches the observed black-pixel regression in the deliberately lit capture views.

Gameplay now has two optional wings, three damage upgrades, bouncing grenades, ten exploding barrels, wall-aware chain reactions, and a six-second extraction defense. The full route contains 39 enemies. Physics uses 120 Hz updates, acceleration/friction, air control, buffered/coyote jumps, support-surface landings, bounded grenade movement steps, restitution, and blast knockback. Weapon sway, aligned aiming, hit reactions, and grenade-throw motion accompany the existing procedural animations.

## Final verification

- 26 gameplay/physics checks passed, including 30/60/144 FPS movement consistency, crate landings, input-edge retention, grenade bounce, chain reactions, and walls blocking blast propagation.
- Scripted Recruit, Operative, and Veteran campaigns each collected 3/3 upgrades, threw three grenades, defeated all 39 enemies, and extracted in 89.9 simulated seconds. These are perfect-aim functional runs, not human balance assessments.
- The exact shipping executable passed 20 rendered-camera direction checks plus ordinary, animated-combat, and expanded-wing/grenade OpenGL checks.
- All eight native captures in both rendering modes passed completeness checks; the invalid-lighting black-pixel count was zero. The release captures were also visually inspected.
- The final short 90-frame renderer sample on the GTX 1060 6GB averaged **5.73 ms/frame in Full** and **5.01 ms/frame in Performance**. These hidden-window samples are sensitive to driver clocks and background activity; the GPU was observed idling in its P8 power state between tests. They are not sustained gameplay-FPS guarantees. The extra mirrored scene and material lighting add rendering work, so this release does not claim higher graphics performance than the simpler previous renderer.

The shipping executable's SHA-256 is `1320a9a63c79eacda71b76c97eb57c6ab5ef901fec19e83b742bb4c738c20cc9`. The separate developer executable is 100,352 bytes and is not the challenge entry. Run `verify.ps1` to reproduce the development checks and the shipping-binary rendering checks.
