# DXBC patcher audit guide

The implementation is [DxbcPatcher.cpp](BlackRestoration/DxbcPatcher.cpp), with its public API in [DxbcPatcher.hpp](BlackRestoration/DxbcPatcher.hpp). This guide describes the existing code; it does not propose a new shader design.

`patch_shader` edits parameters in an **already-restored modern Black Restoration shader**. It does not find a vanilla shader and insert the restoration effect. The checked-in M3GS files supply the existing instruction sequence. The offline generator and the optional ASI use the same implementation. The main DLC package needs no ASI.

## What the hexadecimal words mean

The arrays are serialized D3D instructions, not process-memory addresses or arbitrary file offsets. `pack_words` writes each DWORD as four little-endian bytes. Float constants are converted from `double` to IEEE-754 `float` and then bit-cast by `f32_bits`; a word such as `0x3F800000` represents `1.0f`.

The relevant token fields are defined in Microsoft's [DirectX tokenized program format header](https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/Support/d3d12TokenizedProgramFormat.hpp). For the tokens used here:

| Word | Meaning |
| --- | --- |
| `0x0A000038` | `mul`, 10 DWORDs including opcode and operands |
| `0x07000038` | `mul`, 7 DWORDs |
| `0x0C000032` / `0x09000032` | `mad`, 12 / 9 DWORDs |
| Low opcode `0x00`, `0x10`, `0x33`, `0x34` | `add`, `dp3`, `min`, `max` |
| `0x00100072` / `0x00100082` | Temporary-register destination, `.xyz` / `.w` mask |
| `0x00100246` / `0x0010003A` | Temporary-register source, `.xyzx` swizzle / `.w` selection |
| DWORD after a temporary operand token | Register index (`0`, `3`, or `4`) |
| `0x00004001` / `0x00004002` | Scalar / four-component immediate operand |

For example, `07000038 00100082 00000004 0010003A 00000003 00004001 3F800000` means `mul r4.w, r3.w, l(1.0)`. It occupies the same space as the version with `r3.w` as its last source. This is how the fade exponent changes without adding instructions.

## Container parsing and recognition

The inputs in this release are complete DXBC containers despite their `.m3gs` extension. The container begins with `DXBC` at bytes 0–3, a 16-byte checksum at bytes 4–19, total size at byte 24, chunk count at byte 28, and the chunk-offset table at byte 32. Each chunk has an eight-byte tag/size header.

`parse_dxbc_info` checks the declared size, table and chunk bounds, requires one `SHEX` or `SHDR` program, and checks its token count. That program begins with two DWORDs for version and length; instructions therefore start at `chunk_offset + 16`. The walker counts instructions using their encoded lengths and requires exactly one `DCL_TEMPS` declaration. `find_modern_blocks` additionally requires its value to be five.

The matcher searches this instruction stream on DWORD boundaries for **one adjacent luma block followed immediately by one near-black block**. No per-file absolute patch address is used. All non-wildcard words must equal the built-in templates, including instruction lengths, opcodes, registers, masks, luma weights and smoothstep constants. Wildcards allow an already-tuned modern preset to be recognized; their values and encodings are then checked by the two parameter-word validators. Zero or multiple valid block-pair matches throw `PatchError`.

The `kBase*` values only construct the recognition templates. They are not the recommended release settings, and their parameter words are wildcarded. Authoritative offline settings come from the package's `Preset_*.ini`; ASI settings come from its configuration snapshot.

Recognition is a specific local topology check, not a cryptographic identity check on the entire shader or a filename/index check. The parser is a structural walker for these release shaders, not a complete decoder for every DXBC instruction/custom-data form. `parse_dxbc_info` alone does not check the checksum or require modern topology.

## Luminance shadow block: 61 DWORDs, 8 instructions

Here `rgb = r0.xyz`, `T = ShadowRange`, `B = ShadowBoost`, and `p = ShadowFade`. Luminance weights decode to approximately `(0.212671, 0.715160, 0.072169)`. The table gives zero-based DWORD offsets **relative to this block**, not to the file. `l(...)` denotes immediate constants; swizzles are abbreviated where only XYZ is used.

| Start | Decoded operation | Purpose |
| ---: | --- | --- |
| 0 | `dp3 r3.w, rgb, l(weights)` | `L = dot(rgb, weights)` |
| 10 | `add r3.w, r3.w, l(-T)` | `L - T` |
| 17 | `min r3.w, r3.w, l(0)` | `a = min(L-T, 0)` |
| 24 | `mul r4.w, r3.w, source1` | Start with `a` or `a²` |
| 31 | `mul r4.w, r4.w, source2` | Optional third power |
| 38 | `mul r4.w, r4.w, source3` | Optional fourth power |
| 45 | `mul r4.w, r4.w, l((-1)^p * B/T^p)` | Normalize the shadow gain |
| 52 | `mad rgb, rgb, r4.w, rgb` | Apply `rgb * (1 + gain)` |

Each source is `r3.w` when active, otherwise `l(1)`. Allowed active-source sequences are `(off,off,off)`, `(on,off,off)`, `(on,on,off)`, `(on,on,on)` for exponents 1–4. Other sequences are rejected. For nonnegative luminance, this yields `gain = B * max(1-L/T, 0)^p`. Odd powers need the negative coefficient because the stored value is `L-T`.

All four public modern presets use `B=0`, disabling this gain. Even when enabled, it multiplies RGB, so zero RGB remains zero.

## Per-channel near-black block: 136 DWORDs, 14 instructions

This stage uses RGB after the luma stage. Let `T = NearBlackRange`, `D = NearBlackDetail`, `R = NearBlackRecovery`, `P = PureBlackProtection`, and `F = BlackFloorLift`. Operations are componentwise:

| Start | Decoded operation | Purpose |
| ---: | --- | --- |
| 0 | `mul r3.xyz, rgb, l(1/P)` | Protection ramp input |
| 10 | `max r3.xyz, r3.xyz, l(0)` | Lower clamp |
| 20 | `min r3.xyz, r3.xyz, l(1)` | `t = clamp(rgb/P, 0, 1)` |
| 30 | `mul r4.xyz, r3.xyz, r3.xyz` | Save `t²` |
| 37 | `mul r3.xyz, r3.xyz, r4.xyz` | Form `t³` |
| 44 | `mad r3.xyz, r3.xyz, l(-2), r4.xyz` | `-2t³ + t²` |
| 56 | `mad r3.xyz, r4.xyz, l(2), r3.xyz` | `S = 3t² - 2t³` |
| 68 | `mul r3.xyz, r3.xyz, l(R/T²)` | Protected recovery |
| 78 | `mad r3.xyz, rgb, l(D/T²), r3.xyz` | Add detail term |
| 90 | `add r3.xyz, r3.xyz, l(F/T²)` | Add optional floor lift |
| 100 | `add r4.xyz, rgb, l(-T)` | `rgb-T` |
| 110 | `min r4.xyz, r4.xyz, l(0)` | Zero the effect above the range |
| 120 | `mul r4.xyz, r4.xyz, r4.xyz` | `max(T-rgb, 0)²` |
| 127 | `mad rgb, r4.xyz, r3.xyz, rgb` | Add the restoration |

For `0 <= x < T`, the result is:

```text
t  = clamp(x/P, 0, 1)
S  = t*t*(3 - 2*t)
x' = x + (1 - x/T)^2 * (x*D + R*S + F)
```

For `x >= T`, the added term is zero. Computing `(x-T)²` and pre-dividing the coefficients by `T²` avoids introducing a division instruction. Registers `r3.xyz` and `r4.xyz` are reused for intermediate values; the `.w` luma scratch values are separate. XYZ destination masks leave `r0.w` unchanged.

At `x=0`, `t=0`, `S=0`, and the detail term is zero. With `F=0`, the output stays zero. The API permits a nonzero floor lift for advanced runtime use; that intentionally removes this guarantee. Public modern presets keep it zero. The downstream Filmic LUT remains outside the two rewritten blocks; these equations describe the restoration stage, not the entire final display transform.

## Exact allowed changes

These are the only wildcard DWORDs, indexed from the start of their respective blocks:

| Block | Word indices | Encoding |
| --- | --- | --- |
| Luma | 16 | `-ShadowRange` |
| Luma | 29–30, 36–37, 43–44 | Three source-token/value pairs: `r3.w` or scalar `1.0` |
| Luma | 51 | `(-1)^ShadowFade * ShadowBoost / ShadowRange^ShadowFade` |
| Near-black | 6–9 | Four equal float32 values, `1/P` |
| Near-black | 74–77 | Four equal float32 values, `R/T²` |
| Near-black | 84–87 | Four equal float32 values, `D/T²` |
| Near-black | 96–99 | Four equal float32 values, `F/T²` |
| Near-black | 106–108 | Three equal float32 values, `-T`; word 109 stays zero |

The validators require finite values, valid coefficient signs and supported recovered parameter ranges (with `1e-6` tolerance for encoded float32 values). Repeated vector lanes must agree. Fade source-token changes are the explicit exception to “only immediates change”; opcodes, destination registers and instruction lengths never change.

`patch_shader` validates the requested settings, verifies the input checksum, locates the pair, builds same-size replacement blocks, and copies them into a new buffer. Although it copies all 788 bytes of the pair, fixed words are identical by construction and matching. Bytes outside the pair and the checksum field remain copied from the input. The caller's input is never modified.

After rewriting the checksum, `compare_container_after_in_place_patch` checks total size, chunk offsets/tags/sizes, program version/size/token count, instruction count, temporary count, and the recorded STAT chunk bytes. These checks supplement recognition and the limited write locations; they do not constitute general semantic verification of arbitrary shaders.

## Parameter and checksum safeguards

`validate_parameters` enforces the ranges listed below, and `P <= T`:

| Setting | Allowed range |
| --- | --- |
| ShadowBoost | 0–4 |
| ShadowRange / NearBlackRange | 0.005–0.100 |
| ShadowFade | Integer 1–4 |
| NearBlackDetail | 0–2.5 |
| NearBlackRecovery / BlackFloorLift | 0–0.020 |
| PureBlackProtection | 0.00005–0.005 |

It also samples the near-black curve at 2,048 intervals over `[0,T]`, rejecting any step with output increase below `0.02 * input_step`. This conservative check guards against strong tonal compression/reversal. It is a sampled double-precision check of the requested near-black settings, not an analytic monotonicity proof, a check of the combined luma stage, or GPU/LUT simulation. The input wildcard validators check encodings/ranges; they do not rerun this sampling on recovered baseline settings.

`calculate_dxbc_checksum` uses MD5 compression rounds but **DXBC-specific padding**, not ordinary `MD5(file)` or `MD5(payload)`. The hashed payload is `[20,end)`, excluding magic and the stored digest. Complete 64-byte blocks use `md5_transform`. The final block uses payload bit length at DWORD 0 and `(bit_count >> 2) | 1` at DWORD 15. If the tail has fewer than 56 bytes it is copied starting at byte 4, followed by `0x80`; otherwise the tail and `0x80` occupy a separate block before the final length block. The four result words are written little-endian to bytes 4–19. Clearing that field before recalculation does not affect the digest because it is outside the hashed payload.

A checksum is container integrity data, not a signature or proof of provenance. Output verification and release reproduction check this implementation against the checked-in assets. `verify_dxbc_checksum` returns false for inputs shorter than 20 bytes; invalid magic on a longer input can throw `PatchError`.

## Offline and runtime behavior; Legacy

The [shader-tool wrapper](../scripts/shader-tool/main.cpp) reads preset INIs, generates files only into an absent/empty output directory, and reports errors with a nonzero exit status. `inspect --modern` exercises `patch_shader` using defaults and discards the result; plain `inspect` performs checksum/structural inspection only.

The [ASI hook](BlackRestoration/D3D11Hooks.cpp) first creates the original D3D11 pixel shader, then tries recognition and replacement. A `PatchError` leaves the original shader in use. It stores baseline bytecode for rebuilding replacements; runtime changes do not write the installed M3GS files.

Legacy Emulation deliberately uses a different instruction topology for a static `+0.004` before the unchanged Filmic LUT scale. It must fail the modern matcher and cannot be generated or tuned through this parameter model. Its visual equivalence to the historical original-ME2 fix is not proved.

## Reproduce the release evidence

From the repository root on Windows with the documented build tools:

```powershell
./scripts/Validate-Release.ps1
```

This builds the canonical patcher, inspects all 160 shaders, checks installer assets/settings, and regenerates the 128 modern files under an ignored `_tools/validation-*` directory. `shader-hashes.csv` records SHA-256 hashes; `reproduction.csv` must show zero mismatches for every modern preset. Release files are not overwritten.

The script compares reported per-index structural summaries across presets; full chunk-layout/STAT comparison happens inside each modern patch operation. It does not by itself prove Legacy rejection/disassembly, validate every item of the release checklist, or perform a Mod Manager install/game launch. See [AGENTS.md](../AGENTS.md) for the remaining required release checks. Byte-for-byte reproduction is evidence for these assets, not a substitute for gameplay testing.
