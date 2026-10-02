# The dark eyes: how the bug was found and fixed

From 0.0.4 until 1.0.0, the eyes of Sonic, Amy, Tails and Chip on the Switch were far too dark. It showed in daylight,
at night, in stages and in cutscenes. 0.0.3 drew them correctly. The cause was a bug in this port's shader
translator, in an optimisation pass added for 0.0.4. It took four test rounds to find, because for most of them the
driver looked like the obvious suspect.

This page tells the whole story: what was tried, what each test showed, how the cause was finally found, what it
was, how it was fixed, and how the fix was checked. The short version:

- **Cause:** the translator's alpha-test sinking pass renamed the values of a shadow-map filter, then printed the
  filter's *original* text instead of the renamed one. The shadow tests read values nothing wrote, and in the eye
  shaders the filter also overwrote the eye's world position.
- **Affected:** 69 pixel shaders. Besides the eyes: Sonic's enamel and metal materials, Super Sonic, and the
  shadow-receiving Glass, Metal, Ice and Common materials of the stages.
- **Fix:** one change in `tools/XenosRecomp/XenosRecomp/shader_recompiler.cpp` (in `patches/XenosRecomp-switch-perf.patch`).
- **Check:** every one of the game's 1,385 shaders was run on the PC against the 0.0.3 translator's version of it,
  in every specialization the renderer can use: no difference. On the console the eyes look right again.

## Background: what the translator does

The Xbox 360 shaders are not run directly. At build time, XenosRecomp translates each one into HLSL, DXC compiles
that HLSL to SPIR-V, and the result goes into the game's shader cache. At run time the Switch's Vulkan driver (NVK,
Mesa) compiles that SPIR-V for the GPU.

The Switch port adds optimisations to the translator (`patches/XenosRecomp-switch-perf.patch`). The one that
matters here is **sinking into the alpha-test early-out** (rounds 8 to 10, all in 0.0.4):
- A pixel shader with an alpha test computes its alpha first. Pixels that fail the test are discarded at the end.
- The pass moves the statements that only feed the colour of kept pixels behind the early-out, so discarded pixels
  skip them.
- To move statements freely, it first gives every value its own name (`_t0`, `_t1`... written once), so that only
  the real flow of values ties statements together. Values the rest of the shader reads from the registers get
  copied back.
- A shadow-map filter is special. It exists as two halves (gathers, or point fetches as a fallback), and a
  specialization constant picks one. When both halves write the same register components, the pass renames their
  outputs to shared temporaries too (`RewriteConstruct`).

## Symptoms

- The eyes of every character using the `SonicEye` material (pixel shader `4860E71D54AE300F` in play,
  `006D72F1488C2F60` for the cutscene models) looked dark and olive instead of white and shiny.
- In every build from 0.0.4 on, with every runtime switch on or off, on two different driver versions.
- 0.0.3 (Mesa 26.1.5, before the optimisation work) drew them correctly.

## The investigation

### Round 13: the driver's operand reuse

The eyes were first noticed in round 13. Between 0.0.3 and 0.0.4 two big things had changed: the game-side
optimisation work, and the driver (Mesa 26.1.5 to 26.2.2 with this fork's own compiler changes). The driver looked
likelier, for two reasons:
- its compiler had a change whose correctness rests on undocumented hardware behaviour: Maxwell **operand reuse**
  (NAK reuses a register operand from the previous instruction's cache);
- a translator check already said the eye shader's translation was equivalent to the plain translation in every
  specialization. That check, it turned out, compared the new translator's variants with each other, and they all
  carried the same bug.

What round 13 did:
- Operand reuse became opt-in in the driver (NAK revision 5).
- Configs `eyes-a` to `eyes-e` turned groups of changes off one at a time: reuse back on, varying linking, the ZCULL
  direction, and every shader and render-state switch.

Result: **dark in all of them.** Operand reuse was not the cause.

### Round 14: a different driver, and every drawing switch off

- `eyes-f` ran the same game against stock **Mesa 26.2.3**, built without any of this fork's driver patches.
- `eyes-g` turned off every drawing switch of the renderer.

Result: **dark in both.** That cleared this fork's driver patches. It still left "something between Mesa 26.1.5 and
26.2.x", since 26.2.3 had the same upstream changes.

### Round 15: texture uploads

From Mesa 26.2.0, NVK uploads uncompressed textures through a compute shader, and the eye textures are the only
uncompressed mipmapped character textures. The small mips have a padded row length, a classic source of upload bugs.
`eyes-h` forced the copy engine instead (`NVK_COPY_ENGINE=1`).

Result: **dark.** Not the upload path.

### Round 16, part 1: taking the driver's compiler apart

Round 16 instrumented the driver to see exactly what it made of the eye shaders.

**NAK debug flags**, each undoing one compiler change made between Mesa 26.1.5 and 26.2:
- `rfpmath`: no `nir_opt_fp_math_ctrl`;
- `rdistribute`: no `nir_opt_algebraic_distribute_src_mods`;
- `runordered`: `has_fneo_fcmpu` and `has_ford_funord` off;
- `rifmerge`: `nir_opt_if` does not merge ifs with inverted conditions.

**Shader dumps:** `NAK_DEBUG=dump` made NVK write, for the shaders named by their SPIR-V hash, the NIR at each stage
(after SPIR-V translation, after preprocessing, lowered, final) and NAK's Maxwell assembly.

**Tests on the console:**
- E1 dumped the eye shaders.
- E2 compiled them serially (no scheduler interaction).
- E3 applied all four reverts at once.

Result: **dark in all three.**

**Offline, on the PC**, two interpreters were written:
- one for NIR text;
- one for NAK's SM50 assembly.

Each compile stage of the eye pixel shader and its vertex shader ran on the same random inputs. Every stage gave
the same outputs, from SPIR-V to the final machine code. **The driver compiled these shaders correctly.** Whatever
was wrong was already in the SPIR-V it was given, or in the shader's inputs.

**Debug views:** Mesa can replace a shader's SPIR-V at run time (`MESA_SPIRV_READ_PATH`). The eye shader's HLSL was
modified and recompiled with the translator's exact DXC arguments (byte-identical SPIR-V for the unmodified source).
The replacements:
- E5: only the eye texture, unlit;
- E6: the full shader with a white texture;
- E7: only the environment map;
- E8: only the normal map;
- E9, E10, E11: the eye texture at fixed mip levels 0, 2 and 3.

The textures and mip levels were all fine. A first reading of E5 suggested broken small mips, but E9-E11 showed
identical mips; the difference had been the game's auto-exposure between two screenshots.

### Round 16, part 2: comparing with 0.0.3

The decisive hint came from the user: the bug had existed **at least since the 0.0.4 build**, not just since a
driver change. 0.0.3 and 0.0.4 used the same submodule commits; the differences were all in this port's own code and
patches. The biggest one for shaders was the translator patch (3,600 lines).

1. **The 0.0.3 translator was rebuilt.** That is the same XenosRecomp commit with only the MinGW DXC patch, which was
   all 0.0.3's build script applied. A small hook made it dump the HLSL of chosen shaders, as the current translator
   already could.
2. **Both translations of the 20 eye shaders were dumped.** The new HLSL looked nothing like the old: values renamed
   to `_tN`, statements split per component, code moved behind the alpha test. So a text diff was useless.
3. **An HLSL interpreter was written** (`tools/switch-shader-audit/hlsleval.py`). It runs the C preprocessor output of
   a translation, including the shader header's own helper functions, in float32. The constants are memory blocks
   that both the old pointer path and the new uniform-buffer path read. Textures are smooth functions of the
   coordinate (or texel grids for the gather paths).
4. **Both translations ran on the same random inputs.** 14 of the 20 eye shaders gave the same colours. 6 did not: up
   to 1 % apart on random inputs, and on real ones the whole eye-light term.
5. **The first diverging statement was found** (`tools/switch-shader-audit/localize.py`). The same operations on the
   same operands give the same float32 bits. So the script records every value each translation computes and prints
   the first statement of the new one whose value never appears in the old run. For `SonicEye_dpne@c@_NoLight`
   (`5A15BADB6D79D028`) that was:

   ```hlsl
   _t4.x = (r7.x + (-g_EyePosition.x));   // the eye vector: world position minus the eye position
   ```

   In 0.0.3 the same statement read `r7.x` while it still held the world position (`TEXCOORD7.x`). In the new
   translation, something had overwritten `r7.x` before this statement ran.

## The cause

Here is the shadow filter of that shader as the 0.0.4 translator printed it (shortened):

```hlsl
	_t105.x = (_t83 * r9.x);    // the shadow-map coordinate, renamed by the sinking pass
	_t105.y = (_t83 * r9.y);
#ifdef __spirv__
	[branch] if (/* gathers usable */)
	{
		float4 gather0_0 = tfetch2DGather(..., r16.xy, ...);  // reads r16: never written any more
		...
		r21.x = gather0_0.z;   // writes registers that nothing reads any more
		...
		r7.x = gather0_0.y;    // overwrites the world position
	}
	else
#endif
	{
		r21.x = tfetch2D(..., r16.xy, float2(0, -1), ...).x;
		...
		r7.x = tfetch2D(..., r16.xy, float2(0, 0), ...).x;
	}
	...
	[branch] if ((g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST_SINK) == 0)
	{
		_t4.x = (r7.x + (-g_EyePosition.x));   // moved here, after the filter: reads the shadow depth
		...
	}
	...
	_t178.w = (_t109 >= _t169.y);   // the shadow test reads _t109, which nothing ever writes (0.0)
```

The sinking pass had renamed everything around the filter:
- the coordinate the filter should read became `_t105`;
- the nine shadow samples it produces became `_t106` to `_t114`, which the shadow tests read.

`RewriteConstruct` had renamed the filter's two halves to match. Every analysis afterwards (which statement reads
or writes what, which ones may move) used those renamed halves, and by them the moves were correct.

The output stage did not. When both halves of a filter stay in place, it prints the filter as one block, and that
block came from `constructs[]`: **the filter's original lines**, saved before renaming. So the shader that was
compiled:

1. fetched the shadow map at `r16.xy`, a register whose writer had been renamed away (`r16` still held its initial
   0.0);
2. stored the samples in `r21`, `r24` and `r7.x`, which nothing read any more;
3. compared the shadow depths `_t106`... against the pixel, but those temporaries were only declared (`= 0.0`), so
   the shadow tests saw 0;
4. in the eye shaders, ran the moved eye-vector statement after the filter had replaced `r7.x` (the world position)
   with a shadow depth.

Points 3 and 4 together removed the eye-light and the sun's light from the eyes. Point 3 alone made every other
affected surface behave as if it were always in shadow.

The bug only struck where three conditions met:
- a shadow filter rewritten as gathers;
- both halves renamed by `RewriteConstruct`;
- both halves kept before the early-out.

That happened in 69 of the game's 1,096 pixel shaders: every shadow-receiving variant (`@c@`, `@cv@`) of SonicEye,
SonicEnamel, SonicMetal, SuperSonic, Glass, Metal, Ice and Common. Nothing could switch it off at run time: without
the sink bit, the moved statements run before the branch, but the same broken filter text is printed either way.
That is why every configuration of rounds 13 to 15 stayed dark.

## The fix

The output stage now prints a renamed filter from its renamed halves. It rebuilds the same
`#ifdef __spirv__ / if (gathers) {...} else #endif {...}` shape from the gather half's lines and the point-fetch
half's lines. A filter that was not renamed is still printed from its original lines, as before. In
`sinkIntoAlphaTestEarlyOut` (`shader_recompiler.cpp`):

```cpp
if (constructRenamed[unit.construct])
{
    // The construct with its renamed halves: their reads of renamed values and their writes of temporaries
    // are what the statements around them were analysed and renamed with.
    ...
    result.push_back(original[0]);                                                     // #ifdef __spirv__
    result.insert(result.end(), gatherHalf->lines.begin() + 1, gatherHalf->lines.end() - 1); // if (gathers) { ... }
    result.push_back(original[elseLine]);                                              // else
    result.push_back(original[elseLine + 1]);                                          // #endif
    result.insert(result.end(), fallbackHalf->lines.begin() + 3, fallbackHalf->lines.end()); // { point fetches }
}
```

After the fix, the same filter reads `float2(_t105.x, _t105.y)` and writes `_t106` to `_t114`. `r7.x` keeps the
world position.

## How the fix was checked

- **Eye shaders:** all 20 eye shaders matched their 0.0.3 translation in every specialization set, including the
  shadow-gather paths (with texel-grid textures), the sink variants and the alpha test.
- **Every shader** (`tools/switch-shader-audit/audit.py`): the 0.0.3 and fixed translators dumped all 1,385 shaders,
  and each was run on 16 random inputs in every specialization the renderer can use with it (12,089 sets):
  - the renderer's mask is applied as it applies it, so a bit the shader never tests stays off;
  - a pixel the blend-skip optimisation discards counts as equal where the blend would have left the target
    unchanged;
  - components declared unused count as equal;
  - result: **no difference anywhere.**
- **SPIR-V validation:** all 1,385 modules pass `spirv-val` (Vulkan 1.1).
- **On the console:** the user confirmed the eyes look right.
- **Not modelled by the evaluator:** levels of detail, derivatives and helper pixels. The sinking pass has its own
  rules for those (fetches with an implicit level of detail never move into per-pixel branches), and the console
  test covers them.

## Afterwards

- **The driver is as before.** Round 16's instrumentation was removed; the Mesa fork keeps it as
  `nfsmw/round16-instrumented-worktree.diff` for the next time. Operand reuse stays opt-in in the driver and asked for
  by the game. The note that blamed it for the eyes now says what really happened.
- **A new check for every translator change.** Valid SPIR-V and a readable diff were not enough: the eyes passed both.
  The translator audit (`tools/switch-shader-audit/`, see `docs/SWITCH-PERFORMANCE-AUDIT.md`, **Translator audit**)
  runs every shader against 0.0.3's translation. `localize.py` points at the first statement that differs. It takes
  minutes and would have caught this bug before 0.0.4 shipped.
- **Translator debugging aids:**
  - `XENOS_RECOMP_ONLY=<hash>,...` (with `XENOS_RECOMP_DUMP_DIR`) translates a few shaders in seconds;
  - `XENOS_SINK_DEBUG_SETS=1` prints, per statement, what the sinking pass thinks it reads and writes.

### Running the audit

In the devkitPro MSYS2 shell, from the repository (`A` is any scratch folder):

```bash
A=/c/temp/audit
bash tools/switch-shader-audit/build-003-translator.sh $A/xenos003
export PATH="/c/devkitPro/msys2/clang64/bin:$PWD/tools/XenosRecomp/thirdparty/dxc-bin/bin/x64:$PATH"
XENOS_RECOMP_DUMP_DIR=$(cygpath -w $A/old) $A/xenos003/build/XenosRecomp/XenosRecomp.exe UnleashedRecompLib/private $(cygpath -w $A/old.cpp) $(cygpath -w $A/xenos003/XenosRecomp/shader_common.h)
XENOS_RECOMP_DUMP_DIR=$(cygpath -w $A/new) build/host-tools-x64/tools/XenosRecomp/XenosRecomp/XenosRecomp.exe UnleashedRecompLib/private $(cygpath -w $A/new.cpp) tools/XenosRecomp/XenosRecomp/shader_common.h
python tools/switch-shader-audit/audit.py $A/old $A/new $A/audit.txt
python tools/switch-shader-audit/localize.py $A/old $A/new <hash>
```

The last two need a Windows Python with numpy. Run `localize.py` for each shader `audit.txt` lists as differing.

## Lessons

- **Compare against a known-good reference, not against yourself.** The earlier equivalence check compared the
  sinking variants of the new translation with each other, and all of them carried the same bug. 0.0.3's output was
  the reference that mattered.
- **"Before" and "after" builds tell more than "on" and "off" switches.** The bug lived in translator output that no
  runtime switch could turn off; the decisive question was which *build* first showed it.
- **A pass whose analysis is right can still print the wrong thing.** The renaming and the dependency analysis were
  correct. The output stage silently used an older copy of the text for one case.
