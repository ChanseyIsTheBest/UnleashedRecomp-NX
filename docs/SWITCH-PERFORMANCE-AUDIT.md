# Audit of the Switch performance changes

Every performance change of the Switch build is held to one rule: **the rendered image and the game's behaviour
stay exactly the same**. No setting, resolution or shader output changes. For each change this document says why
that holds, what could break it, and which `[Switch]` key in `config.toml` turns it off so it can be isolated on the
console.

- [SWITCH-PERFORMANCE.md](SWITCH-PERFORMANCE.md): what each change does, when it came in and what it gained.
- [SWITCH-MESA.md](SWITCH-MESA.md): the driver build and the driver-side changes.

Round numbers below are the sections of SWITCH-PERFORMANCE.md (its table maps them to the test-set folders).

The defaults described here are those of round 14: the final build's (version 0.0.4, "Final build" in
SWITCH-PERFORMANCE.md) with every switch of rounds 11 to 13 on ("Current state" there). Four round 10 renderer changes
are off: with them on, long play lost the GPU (see
[GPU loss with four round 10 renderer changes](#gpu-loss-with-four-round-10-renderer-changes)). The final build also
sets the output and internal resolution per docked and handheld mode; those are defaults chosen by the user, not
performance changes (see [Platform modules and diagnostics](#platform-modules-and-diagnostics)).

## How changes are checked

**On the PC, before a test build:**

- An exactness argument, written down here: the same operations on the same operands reach the same outputs, or
  the work that is skipped provably cannot be observed.
- Shader translator changes: all 1,385 SPIR-V modules of the game's shader cache pass `spirv-val` (Vulkan 1.1 for
  the modules that use quad operations). Shaders a change does not apply to are compared with the previous
  translator's output. `XENOS_RECOMP_DUMP_DIR=<folder>` dumps the HLSL and SPIR-V of every shader;
  `XENOS_SINK_DEBUG=1` adds, per shader, why each statement before the alpha test stays where it is
  (`XENOS_SINK_DEBUG_SETS=1` also prints what each statement reads and writes). `XENOS_RECOMP_ONLY=<hash>,...`
  (with the dump folder) translates only those shaders and writes no cache.
- Since round 16, the translation itself is checked against 0.0.3's: every shader's `main()` from both translators
  runs on the same random inputs (see **Translator audit**). Valid SPIR-V and a readable diff were not enough: the dark
  eyes passed both.
- Native replacements of guest code run against the guest's own output. The LZX decoder matched the game's
  shader archive, and 3,000 corrupted streams ran without a crash.
- The changed app sources are compiled for the Switch before every full build.

**On the console, by the user:**

- Verify switches, for the changes whose exactness depends on data only seen at run time. Each runs both paths and
  reports any difference:

| Switch | Compares | A difference shows as |
|---|---|---|
| `SwitchVerifyTextureSizes` | 2D texture sizes from the shared constants with `GetDimensions()` | magenta pixels |
| `SwitchVerifyShadowGather` | every gathered shadow-map texel with its point fetch | magenta pixels |
| `SwitchVerifyNativeDecompress` | every native LZX result with the game's own decoder | `[lzx] ... MISMATCH` lines |
| `SwitchVerifyMessageDispatch` | the table's handler with the one the game's dispatcher picks | `[dispatch] MISMATCH` lines |
| `SwitchVerifyNativeHotFunctions` | registers and memory of each native function (rounds 11-13) with the recompiled one's | `[native] MISMATCH` lines |
| `SwitchVerifyNativeAudio` | the mixer kernels' results with the recompiled kernels' | `[audio dsp] MISMATCH` lines |

The last three compare one call in `SwitchVerifyEvery` (32 in the test configurations; every call was too slow to
reach a stage). The round 13 verify run compared over 2 million native calls, the light field included: 0 mismatches.

- A/B configurations. Every test set pairs configurations that differ in one group of switches, measured at the same
  place (see "How to measure" in SWITCH-PERFORMANCE.md).
- The first-cutscene audio test for any change to guest synchronisation (see [Guest kernel](#guest-kernel-kernelimportscpp)).

Every build names itself in the first line of `stderr.log` (`Build: <id>, guest code ..., LTO ...`) and prints one
line per round listing which of its switches are on, so a log always shows what produced it.

## Shader translator (`patches/XenosRecomp-switch-perf.patch`)

XenosRecomp turns each Xbox 360 shader into HLSL, which DXC compiles to SPIR-V. Variants are chosen per pipeline
through specialization constants (`g_SpecConstants()`), which the driver folds when it compiles the pipeline.

**Constants through uniform buffers** (`SwitchConstantsUBO`). Every constant, texture index and sampler index reads
either the dynamic uniform buffer (set 4) or the original pointer, chosen by `SPEC_CONSTANT_CONSTANTS_UBO`. Both
read the same bytes of the same upload allocation.
- Risk: a negative relative index (`a0` below the array) reads another part of the upload buffer on the pointer
  path and an out-of-range constant (zero on NVIDIA hardware) on the uniform-buffer path. Neither matches the
  Xbox 360, and the game does not rely on either.

**`max(a, a)` → `a`.** Only when both operands are the same text (register, swizzle, negation and absolute value).
`max(a, a) == a` exactly, NaN and signed zero included. The `a0` update of `MaxA` is emitted separately.

**Predicate blocks.** Consecutive instructions under the same predicate share one `if (p0)`. A block closes:
- before an unpredicated instruction or the other condition;
- right after an instruction that writes `p0`;
- around the GI bicubic wrapper;
- at the end of every exec clause.

Inside a block `p0` cannot change, so every instruction runs under the same condition. Unbalanced braces would make
DXC fail and the cache regeneration stop; they could not produce wrong code silently.

**Texture sizes from constants** (`SwitchTextureSizeConstants`; verify: `SwitchVerifyTextureSizes`). The offset
fetches, bilinear-weight fetches, bicubic GI filter and pixel-coordinate helper take the slot's size from the shared
constants instead of `GetDimensions()`. The arithmetic that follows is unchanged, so the same size gives
bit-identical results. The renderer records the size of every descriptor it writes: all writes go through
`SetTextureDescriptor`, and all views start at mip 0.
- Risk: a descriptor write that bypasses the wrapper, or a view with a non-zero base mip. The verify switch shows
  either.

**Texcoord swap mask**: `1u << index` instead of `1ull << index`; the indices are below 16.

**Reverse-Z pass skipped without reverse Z.** When reverse Z is off, pass 1 rewrites every output of pass 0, so
pass 0 is skipped through a specialization constant. The only exception, an output that only pass 0 writes, already
held a value computed with the wrong projection.

**Unread vertex outputs** (`SwitchTrimVertexOutputs`). Every vertex shader ends with one guarded store per
interpolator component (`if (spec bit) o<k>.c = 0`) before each exit. The renderer sets the bits of the outputs the
pixel shader's SPIR-V never loads, and every bit when there is no pixel shader.
- A location counts as read when its Input variable is the pointer operand of `OpLoad`, an access chain,
  `OpCopyObject`, `OpCopyMemory(Sized)`, a function argument or an `InterpolateAt*` instruction. With a
  variable-pointers capability, everything counts as read.
- DXC assigns locations in declaration order, and both stages declare the interpolators in the same order.
- Nothing reads a trimmed output.

**Velocity-map vertex shader.** The one shader that was emitted as a `while (true) switch (pc)` state machine is now
if/else with `do/while` loops. This is accepted only when every jump is forward and all blocks nest (a stack walk).
Every other shader translates byte for byte as before.
- Caveat: the compiler may now fuse a multiply and an add that sat in different `case` blocks. That is at most a
  last-bit difference, in this one shader's outputs.

**Shadow-map gathers** (`SwitchShadowGather`, `SwitchShadowGatherSpecialization`; verify: `SwitchVerifyShadowGather`).
A 2x2 filter at (±0.5, ±0.5) texels, or a 3x3 filter at (−1..1) texels, is read with one or four gathers. This
happens only when the renderer's per-slot conditions hold:
- power-of-two sides, so `offset / size` is exact;
- one mip level;
- point filtering both ways, without anisotropy.

Each gather then returns exactly the texels of the point fetches: the red channel after the view swizzle, like
`.x`. The 3x3 mapping was checked texel by texel against the gather order (x = (i0, j1), y = (i1, j1), z = (i1, j0),
w = (i0, j0)). Otherwise the point fetches run. Pipelines whose gathered slots are all known to be gatherable are
built without the check and the fallback.

**Skinning specialization** (`SwitchBonesSpecialization`). A pipeline variant resolves the `mrgHasBone` branch (vertex
boolean b0 only). It is used only for draws whose b0 matches; `ProcSetBooleans` re-selects the pipeline when b0
changes. Until the variant is compiled, the generic pipeline draws.

**Indexed constants from memory** (`SwitchIndexedConstantsFromMemory`, off). The `_Rel` macros read the same element
from either source: `base + min(index, count - 1)`, 0 beyond the array.

**Unused colour channels** (`SwitchTrimPixelOutputs`). A channel is zeroed at the shader's exits only if the write
mask excludes it or there is no colour target. Alpha is kept whenever blending uses a source-alpha factor, or the
alpha test or alpha to coverage is on.

**Constant register counts** (`SwitchTrimConstantUploads`). Each cache entry records the end of the last float4
constant the shader declares, or the whole block when any constant is read relative to `a0`/`aL`. The renderer
uploads at least that much. Registers without a declaration are literals in the shader.

**Alpha-test early-out** (`SwitchAlphaTestEarlyOut`). The straight-line arithmetic between the last write of `oC0.w`
and the alpha test is wrapped in `if (alpha test off || early-out off || !((oC0.w - g_AlphaThreshold) < 0))`. That is
the same comparison as the `clip()` that follows, so a NaN alpha still runs it. Kept pixels run the same code;
discarded pixels skip work whose results they would throw away. Only regions without fetches, derivatives, loops or
other exits qualify.

**Sinking into the early-out** (`SwitchAlphaTestSink`, `SwitchAlphaTestQuadSink`). Statements before the early-out
that only feed the kept pixels' colour move into it.
- **Analysis.** Each statement's reads and writes are tracked per register component. A statement moves only when
  nothing that stays after it reads what it writes, or writes what it reads or writes. The alpha, the alpha test,
  the code after the branch, and everything they read, stay.
- **Renaming (round 9).** Values get names of their own (`_tN`, written once), and the registers read by the rest of
  the shader get copies back. Only the real flow of values then ties statements together. The renamed statements
  compute the same operations on the same operands.
- **Round 10 additions:**
  - The two halves of a shadow filter write shared temporaries. Exactly one half runs (a specialization constant
    chooses). Both halves must write the same components, each once, and read none of them; otherwise nothing
    changes.
  - **Round 16 fix (the dark eyes).** When both renamed halves stayed before the branch, the output used the
    filter's original lines, which read and wrote the registers, not the renamed halves. The statements around it
    had been renamed to the temporaries, which nothing then wrote (they stayed 0). A moved statement could also read
    a register the original filter had just overwritten. This affected 69 pixel shaders: SonicEye, SonicEnamel,
    SonicMetal, SuperSonic, and the shadow-receiving (`@c@`, `@cv@`) Glass, Metal, Ice and Common materials. In them:
    - the shadow fetches used stale coordinates (`r16.xy` in the eyes, never written);
    - the shadow tests compared against 0;
    - in the eyes, the world position (`TEXCOORD7.x`) was also replaced by a shadow depth, so they lost their lighting.

    The bug was in every build since 0.0.4, with every switch off and on any driver. The construct is now built from
    its renamed halves. Found by evaluating the 0.0.3 and current translations on the same inputs (see **Translator
    audit**).
  - A component-wise statement with several components becomes one statement per component (`SplitParser`). It
    accepts numbers, registers, constants, temporaries, constant-array accessors with a swizzle, constructors, unary
    minus, `+ - * /`, comparisons, `?:` and the component-wise intrinsics. A scalar is broadcast into several
    components only if it is a number or a variable, so nothing runs twice. Anything else keeps the statement whole.
  - A loop that sets its own counter (`aL`) to 0, and whose counter no other statement reads, moves as one
    statement. Loops that use `a0` never move.
- **Per-pixel variant.** Fetches with an implicit level of detail, and derivatives, stay before the branch: in a
  branch that some pixels of a 2x2 quad skip, they would be undefined. Gathers do not use derivatives and may move.
- **Quad variant** (off by default since round 10: it was slower). Fetches and derivatives move into a block that
  runs for every quad with at least one kept pixel. The early-out test is combined across the quad with
  `QuadReadAcrossX/Y`, and helper pixels take part, so levels of detail are unchanged. These modules need Vulkan 1.1.
- **Without the sink bits**, the moved statements run where they were. The renderer sets the bits only for pipelines
  whose early-out can skip pixels.

**Transparent pixels skipped** (`SwitchSkipTransparentPixels`). A blended pixel that the blend provably leaves
unchanged is discarded before the blend.
- Only UNORM targets (sources clamped to [0, 1], finite texels), add or reverse subtract, no depth write, no alpha to
  coverage.
- `_ALPHA`: a source alpha of 0 or less, with SRC_ALPHA/ZERO colour source factors and ONE/INV_SRC_ALPHA destination
  factors.
- `_ZERO`: all four outputs 0 or less, with destination factors that are then 1.
- NaN outputs are never discarded.

Float targets are excluded: a zero factor times an infinite or NaN source is not 0, and a −0 destination could
become +0.

**Pass-through, copy and kill flags** (`SHADER_FLAG_*` in the cache entry). Each is detected from the translated text
with exact patterns; a shader that does not match gets no flag, so a miss only costs an optimisation.
- `POSITION_PASS_THROUGH`: the position is the POSITION input with w = 1 plus the half-pixel offset, nothing else.
- `TEXCOORD_PASS_THROUGH`: `oTexCoord0.xy` is written once, from TEXCOORD0 through the texcoord swap, with no
  control flow.
- `PIXEL_COPY` / `DEPTH_COPY`: the pixel shader writes one texel of one slot and nothing else.
- `PIXEL_KILL`: a kill instruction was translated.

**Sampler masks** (round 15). Each cache entry has `textureSlotsRead`: bit s for every sampler slot the shader declares.
Every texture fetch the translator emits names a declared sampler (an undeclared one would not compile), so the mask
holds every slot the shader reads. An entry without it (an older cache) reads every slot. Used by the renderer to tell
a texture that is read from one that is only bound.

**Translator audit** (round 16, `tools/switch-shader-audit/`). Each shader's `main()` from two translations runs on the
same random inputs, and the outputs are compared.
- `build-003-translator.sh` rebuilds the 0.0.3 translator (the same submodule commit with only the MinGW DXC patch).
  Both translators dump every shader with `XENOS_RECOMP_DUMP_DIR`.
- `hlsleval.py` interprets the preprocessed HLSL in float32, the header's helper functions included. The constants
  are one set of memory blocks that the push-constant pointers and the UBOs both read. Textures are smooth functions
  of the coordinate, or texel grids for the gather paths.
- `audit.py` runs 16 inputs per shader and per specialization set the renderer can use with that shader. It masks the
  bits as the renderer does (the quad-sink bit only with a quad variant). It counts as equal:
  - a pixel the blend skip discards where the blend would leave the target unchanged;
  - components a specialization declares unused.
- `localize.py` prints the first statement of the new translation whose value never occurs in the old run. That is
  where they part ways (it found the eye bug in seconds).
- Not modelled: levels of detail, derivatives, helper pixels.
- Result after the round 16 fix: all 1,385 shaders and 12,089 specialization sets, no difference. The fix changed 69
  pixel shaders (the six SonicEye ones checked before and after).

## plume (`patches/plume-switch-perf.patch`)

**Dynamic uniform buffers**: a new range type, a builder helper and `setGraphicsDescriptorSetDynamic` (Vulkan only).

**Persistent pipeline cache** (`SwitchPipelineCache`). The cache is loaded when the device is created, if its key
matches. It is saved from a background thread at the end of loading screens, and during play only with
`SwitchPipelineCacheSaveDuringPlay`. `vkGetPipelineCacheData` has no external-synchronisation requirement, so saving
while pipelines are created is allowed. The key hashes a build tag, the vendor, the device, the driver version and
the cache UUID.
- **Build tag (round 10).** The tag is the SHA-256 of the driver library linked into the NRO
  (`UNLEASHED_RECOMP_SWITCH_DRIVER_ID`), so the cache survives rebuilds of the rest of the NRO. The driver keys its
  own entries by everything a pipeline is compiled from, so entries of changed shaders are simply not found. The tag
  is needed at all because on Horizon the driver's cache UUID does not change between driver builds.
- **Legacy key.** One old key is also accepted: the round-8 builds' file (`0x4901F4390434165A`), and only when the
  driver hash is the one those builds used.
- A file from another driver build is rejected by the tag, and the driver also validates its own entries.

**Depth targets without `TRANSFER_DST`** (Switch only). The port never writes depth through transfer operations on
Vulkan:
- depth copies and MSAA depth resolves are shader draws;
- clears use `vkCmdClearAttachments`;
- `copyTexture` and `copyTextureRegion` are only used with colour textures.

**Redundant-bind filtering.** Pipeline, index buffer, vertex buffers, viewport, scissor and depth bias are compared
with what is bound and skipped when equal.
- Vulkan state persists across render passes and pipeline binds within a command buffer; the filter resets in
  `begin()`.
- Binding a pipeline with a static depth bias invalidates the remembered dynamic value, as Vulkan requires.

**Persistent mapping** of upload, read-back and GPU-upload buffers; `unmap()` is a no-op for them.

**Scratch vectors** replace `thread_local` in `barriers()` and `setVertexBuffers()`. This is safe because a command
list is recorded by one thread at a time (the copy command list under `g_copyMutex`).

**Precise barriers** (`SwitchPreciseBarriers`).
- Source access: the writes possible in the old layout. Destination access: everything possible in the new layout.
  Buffers use the accesses their barriers declare.
- Stage masks are unchanged, so every wait-for-idle a real dependency needs remains. When the stages cannot perform
  the derived access, the conservative masks are used.
- Risk: a write while plume believes an image is in a read-only layout would no longer be flushed. The renderer
  always transitions before writing: render targets through `AddBarrier`, uploads through `COPY_DEST` barriers.

**Stencil-only format and dynamic stencil reference** (`RenderFormat::S8_UINT`, `setStencilReference`). Only the
coverage hand-over uses them (see [Resolves](#resolves)).

**Read-only depth attachments** (round 15; `SwitchReadOnlyDepthSampling`, `SwitchSampledSlotResolves`,
`SwitchDepthRestoreAlias`). `depthAttachmentReadOnly` attaches depth in `DEPTH_STENCIL_READ_ONLY_OPTIMAL` with
`STORE_OP_NONE` (nothing is stored, so sampling the same image in the pass is not a write-after-read hazard).
- **Load.** The load stays `LOAD`. Upstream plume used `LOAD_OP_NONE`, which by the specification leaves the contents
  undefined inside the pass, although its draws test depth. NVK reads the memory anyway, but it turned ZCULL off for
  such passes. The round 15 test build still had `LOAD_OP_NONE`: same pixels, no ZCULL in read-only passes.
- **ZCULL.** With `LOAD` the pass loads the buffer's plane, which its last writable pass stored and which nothing
  changed since. With `STORE_OP_NONE`, no `STORE_ZCULL` follows.
- **Rests on driver behaviour.** Sampled-image descriptors keep `SHADER_READ_ONLY_OPTIMAL` while their image is in the
  read-only depth layout, a mismatch the validation layers would report. NVK never reads the layout of a descriptor.

## Renderer (`UnleashedRecomp/gpu/video.cpp`, `video.h`)

### Constants and descriptors

**Set 4 binding.** One descriptor set per upload buffer, with three dynamic offsets (vertex, pixel and shared blocks).
- All three blocks must be in the same upload buffer; when one rolls over, all three are uploaded again.
- Offsets are 256-byte aligned, and ranges equal the block sizes, so offset + range never passes the buffer's end.
- The binding state is reset at every `BeginCommandList` and after ImGui.

**Specialization bits after masking.** The global bits are ORed in `SanitizePipelineState`, after the per-shader
mask. Every pipeline path (render thread, background compilation, precompiled list) goes through it, so pipeline
hashes stay consistent.

**Lazy push constants.** The constant pointers are pushed only when the draw's shaders are not proven to read their
constants through set 4; unknown shaders get them. The pushed state is forgotten:
- at the start of each command buffer;
- after the port's own draws, which overwrite the first push-constant word;
- after ImGui.

**Constant copies and uploads** (`SwitchSparseConstantCopies`, `SwitchTrimConstantUploads`).
- The game thread copies only the runs of constant groups that changed.
- The render thread swaps, compares and stores in one pass.
- Uploads copy the registers the shaders can read, rounded up to 256 bytes, with the whole block reserved. They upload
  again when a shader needs more than the last upload held.

Each shader reads the same bytes as before.

**Compact texture heap** (`SwitchCompactTextureHeap`). The heap has 16,384 descriptors (64 KB, which NVK reads from a
constant bank). A texture that would find it full gets the null texture and a log line. The high-water mark in play
stays below 2,048.

**Copies keep the vertex constants** (`SwitchCopyKeepsVertexConstants`, round 15, from MarathonRecomp-NX). With the
uniform-buffer constants a resolve copy's push constants overwrite only the pushed pointers, which are pushed again
before a draw whose shaders read them; the constant blocks stay bound. The next draw no longer uploads its vertex
constants again.

**Frame queries one by one** (`SwitchQueryResetPerQuery`, round 15, from MarathonRecomp-NX). The frame's two timestamp
queries are reset with one call each (a 3D-engine semaphore write in NVK) instead of one call for both (a copy-engine
fill). Each is still reset before it is written.

### Draws and pipelines

**Depth-only draws without a pixel shader** (`SwitchDepthOnlyWithoutPixelShader`). Such a pixel shader cannot change
depth, coverage or memory, so the draw is built without a fragment stage. It qualifies when:
- the draw has no colour target, but a depth target;
- alpha to coverage and the alpha test are off;
- its SPIR-V has no `FragDepth` or `SampleMask` output, no image writes and no atomics;
- its only kills are the translator's alpha-test kill and the transparent-pixel discards, which never run without a
  colour target.

**Pixel constants for draws without a fragment stage** (`SwitchSkipUnusedPixelConstants`) stay dirty for the next
draw that has one.

**No-op draws** (`SwitchSkipNoOpDraws`). A draw that writes no colour channel and no depth changes nothing: the port
has no stencil use outside its own hand-over, no occlusion queries and no shader stores. Its state stays for the next
draw.

**Pipeline lookup and sampler caches** (`SwitchPipelineLookupCache`, `SwitchSamplerCache`). Both are keyed by the full
raw state, plus b0 and the gatherable slots. Any new pipeline starts a new generation.

**Render and sampler state filters** (`SwitchSkipRedundantRenderStates`, `SwitchSkipRedundantSamplerStates`). The D3D
thread does not send a state value the render thread already has; the render thread applies each state as an
assignment.
- `D3DRS_ALPHATESTENABLE` is always sent: its effect depends on the render target at the time.
- Samplers are filtered only while the anisotropic filtering setting is unchanged.
- A state sent by another thread, or a change of D3D thread, clears the filter.

### Command hand-over (game thread to render thread)

**Batches** (`SwitchBatchRenderCommands`, `SwitchBatchSeveralDraws`, `SwitchLargerCommandBatches`,
`SwitchRenderQueueToken`, `SwitchZeroCopyBatches`, `SwitchIdleRenderThreadBatches`). On the D3D thread, commands
collect in a batch. It goes out:
- at a draw, once it holds enough commands (up to 512 while the render thread is idle, since round 10);
- at every flush point: texture and buffer unlocks, ImGui, the Present sequence, a change of D3D thread;
- when it is full.

Every command either carries its data (constants and DrawPrimitiveUP vertices are copied) or is flushed before the
game can change that data. The order and content the render thread sees are unchanged; only when they arrive
changes. Other threads enqueue directly, as before (their order relative to the D3D thread was never defined).
Zero-copy batches hand the buffer itself over, through a single-producer, single-consumer pool of 32; with none free,
the batch is copied as before.
- Risk: a D3D-thread command whose effect the game waits for without a flush point. The only waits on the render
  thread are Present (after flushing) and the change of D3D thread (after `BeginCommandList`, which flushes).

**D3D-thread test** (round 10). The thread's TLS region pointer (`TPIDRRO_EL0`, which is distinct for every thread) is
compared with the one noted on the D3D thread wherever `g_presentThreadId` is set. This is equivalent to comparing
thread ids.

**Streaming buffers** (`SwitchStreamingBuffers`).
- A draw after an unlock reads the unlock's copy in the upload ring; a draw before it reads the buffer. The last
  version is written back after all of the frame's draws, behind one barrier.
- Bindings follow their buffer: stream and index bindings record it, UP draws and the instancing stream clear it, and
  destroyed buffers are removed.
- A frame slot's ring memory is reused only after its fence.

### Resolves

A `StretchRect` only records the texture as a destination of the surface. The texture samples the surface itself
until the surface is about to change; then the copy is made, or avoided as below.

**Lazy resolves** (`SwitchLazyResolves`). A depth copy waits while:
- draws only test depth (no depth writes; D32F has no stencil);
- the surface is a depth attachment, or ready for sampling after an eager transition (round 10);
- no texture slot holds one of its textures.

Its contents then do not change, and nothing samples it while it is bound. A clear makes only the copies of the
surfaces it clears.

**Hand-over at clears** (`SwitchResolveHandOver`). A surface resolved into exactly one texture is about to be cleared
entirely. The texture takes the surface's image (the copy's exact result), and the surface takes the texture's old
image, which the clear overwrites.
- Only when both images were created alike: format, size, one level, same kind of target, one sample. Never the back
  buffer.
- The texture is sampled through a view made from its own view description.
- Commands already recorded keep their old descriptors, views and framebuffers until the frame's fence. Layouts move
  with the images, and images leave every framebuffer cache when destroyed.

**Pending over a Present** (`SwitchKeepResolvesPending`). A colour resolve still pending at the end of a frame stays
pending.
- Every event that changes the surface still makes the copy first.
- A released surface makes its copies.
- A CPU update of a texture whose copy was carried over makes the copy first.
- Depth resolves are dropped at the end of the frame exactly as before.

**Hand-over at draws** (`SwitchCoverageHandOver`, `SwitchExactCoverage`). A draw into a surface with one pending texture
hands the image over as at clears. The conditions: no blending that reads the target, every channel written, no depth
buffer, one sample. The pixels the draw does not write then get the old contents back, in one of two ways:
- **Stencil marks.** The draw marks its pixels in an 8-bit stencil buffer (a rolling reference, cleared every 255
  hand-overs), and a copy then restores the unmarked pixels.
- **Proven full-screen draws.** A draw proven to cover the whole target needs no marks. Proof: one axis-aligned
  rectangle from a position pass-through vertex shader, a pixel shader that cannot discard, inside the depth range,
  not culled. The edge pixels within 1/64 pixel of the rectangle's outline (at most two rows or columns per side) get
  the old contents first, and the draw overwrites everything it covers.

**Restore draws skipped** (`SwitchSkipRestoreDraws`). The game draws a surface's own pending resolve back into it; the
draw is skipped when all of these hold:
- the pixel shader is a copy shader (`SHADER_FLAG_PIXEL_COPY` / `DEPTH_COPY`);
- the vertex shader passes the position and texture coordinates through;
- point filtering;
- the texture coordinates at every vertex equal the framebuffer position over the surface size, within 1/64 texel (a
  different texel is half a texel away);
- colour: no blending and no depth write. Depth: depth test ALWAYS with writes, and viewport depth [0, 1].

Every pixel it would write already holds that value.

**Dead copies** (`SwitchSkipDeadCopies`, round 10). Before a pending resolve is copied (a draw into or a clear of its
surface), the render thread reads the commands after the current one. It reads the rest of the batch, then its window
of commands already taken from the queue, and takes more.
- **When the copy is dropped.** A resolve into the texture (which rewrites every texel), or its destruction, comes
  before both:
  - any draw with the texture in a texture slot, the draw being flushed included;
  - any CPU update of the texture.
- **Otherwise** (including running out of commands) the copy is made.
- **Waiting.** It waits for more commands only for copies of 4 MB or more: at most 1 ms per copy and 2 ms per frame,
  and never past a command the D3D thread may be waiting for (Present, an unlock, ImGui).
- Risk: a read of the texture that the scan does not see. Textures are read only by draws through texture slots,
  vertex textures included (checked at every draw), and by CPU updates (`UnlockTextureRect`, checked).
- Off by default in the final build (one of the four changes behind the GPU loss, see below).

**Copies decided at submit** (`SwitchSubmitTimeCopies`, round 15). A resolve copy into a texture is drawn with
`copy_conditional_vs`, which collapses the triangle to a point when a word the render thread writes just before the
frame is submitted is 0. The render thread follows the texture for the rest of the frame:
- **Drawn** (word 1): a draw that can sample the texture's own image (it is in a slot one of the draw's shaders reads,
  by the translator's sampler masks, every slot for the port's own shaders; checked after the draw's resolves and
  hand-overs), a CPU update of it, or the end of the frame first.
- **Not drawn** (word 0): first another copy into it (drawn, or itself never read), a hand-over of a surface's image to
  it, a resolve into it from a colour surface, or its destruction. A colour resolve is always copied or handed over
  before the texture's own image can be read again: until then its slots sample the surface, and every event that
  changes the surface, updates the texture from the CPU or ends the frame without keeping it pending makes the copy
  first. The one path that forgets a pending resolve, a render target released between a frame's end and the next
  frame's start, does not happen: render targets are released by the D3D thread, which sends the next frame's start
  right after the end. A depth resolve still pending at a frame's end is dropped, so for depth textures only an actual
  copy or hand-over counts.
- Barriers, layouts and passes are recorded exactly as before; a copy that draws nothing leaves the texture's image as
  it was, which nothing reads.
- Not `vkCmdDrawIndirect`: this fork's pre-Turing indirect draw pushes the macro header before the indirect segment, the
  order its own comment (constant-buffer path) says kills the channel when a Horizon MME sync is pending.

**Read-only depth sampling** (`SwitchReadOnlyDepthSampling`, round 15, from MarathonRecomp-NX). A draw that tests its
depth buffer without writing it while it samples one of the buffer's pending resolves attaches the buffer read-only
(`DEPTH_READ`) and samples the buffer itself, as the slots do while the resolve waits. The buffer does not change before
its copy is made, so the texels are the copy's. Only single-sampled D32 buffers (no stencil) whose resolve textures have
its format and size, one level and an identity component mapping; not with a waiting depth clear to make in the pass
or a coverage hand-over. Framebuffers with a read-only depth attachment are cached separately (key with bit 0 set) and
leave the caches with their images.

**Bound but unsampled depth resolves** (`SwitchSampledSlotResolves`, round 15). As lazy resolves, for a draw that only
tests the buffer while slots hold its pending textures but no shader of the draw samples those slots; the buffer is
attached read-only, a layout in which those slots can still sample it later (a writable attachment would leave it in a
layout they could not sample without a new barrier).

**Depth restore aliased** (`SwitchDepthRestoreAlias`, round 15). A depth-copy draw from one D32 buffer's pending resolve
into another of the same size (the target), which provably writes every pixel of the target with the source's value at
that pixel (copy shader, position and texture coordinates passed through, point filtering, each pixel its own texel
within 1/64, depth test ALWAYS with writes, no discard, alpha test or coverage, a proven full-screen quad with no
uncertain edge pixels, viewport depth [0, 1], no colour target, the target with no pending resolves): not drawn. The
target stands for the source until the two would differ:
- a draw that only tests the target is tested against the source, attached read-only;
- a draw writing the target's depth, a resolve of the target: the copy is made first (port's depth copy);
- a draw writing the source's depth, a depth clear of the source, the source's destruction: the copy is made first,
  decided at submit (word 0 when the target's next use in the frame is a depth clear, its destruction or another
  aliased restore);
- a depth clear or destruction of the target: the alias ends, nothing to copy;
- no hand-over moves either image while the alias holds.
A depth value is in [0, 1] (viewport range, clamped clears), so saturating it and clamping it to the viewport leaves it
unchanged: the target would have held the source's values exactly.

### Clears

**Overwritten colour clears** (`SwitchSkipOverwrittenClears`). A colour clear waits for the next command. If that
command is a draw that provably replaces every pixel of the target (the proof of the exact hand-over), only the edge
pixels are cleared.

**Clears carried to their pass** (`SwitchCarryClears`, round 10, off by default in the final build). The waiting clear
also waits through commands that
cannot read or write its surface: target changes, depth clears, buffer unlocks and draws into other targets. Its
pending resolves were already made or handed over by the clear. A new one can only come through a resolve from the
surface, which makes the clear first. A new clear of the same surface replaces the waiting one, since it overwrites
every pixel.

**Depth clears** (`SwitchSkipOverwrittenDepthClears`, round 10, off by default in the final build). A clear of a D32F
depth buffer alone (no stencil to clear) waits the same way. The next draw into that depth buffer either makes it inside its own pass, or skips it
because the draw writes every depth pixel regardless of the old value. A skip needs:
- depth test ALWAYS, with depth writes;
- a position pass-through vertex shader on a proven full-screen quad;
- a pixel shader that cannot discard, and no alpha test, alpha to coverage or blend skip.

The edge pixels are cleared with rectangles. Any other command that could involve the depth buffer makes the clear
first.

### Barriers

**Eager transitions** (`SwitchEagerSampleTransitions`, `SwitchEagerDepthTransitions`). A surface left with pending
resolves moves to the sampling layout in the barrier batch of the change of target. Before, it got a batch of its own
when first sampled. Only the barrier moves. A depth buffer bound again for depth-test-only draws goes back to an
attachment with the draw's barrier and keeps its resolves pending: no slot holds its textures, so nothing has
sampled it. `SwitchEagerDepthTransitions` (round 10) is off by default in the final build; the colour one stays on.

### Present

**Present on the render thread** (`SwitchPresentOnRenderThread`, round 10, on by default in the final build). Present
waits only until the render thread has recorded the frame. By then the render thread has read everything the game thread sent, including its copies of
constants and vertices. The render thread then, before it processes the next frame's first command:
1. submits the frame;
2. presents;
3. waits for the fence of the frame slot it reuses, and resets that slot;
4. acquires the next image.

The swap chain and the queue are used only by the render thread; other threads' submissions go through plume's queue
mutex. The per-frame state the game thread owns (its constant and vertex copies) is reset by the game thread, after
the recording is done.
- Risk: the render thread copies buffer and texture unlocks from guest memory when it processes them. While it is
  still presenting, the game could rewrite that memory first. So the D3D thread waits for the present to finish before
  it locks a buffer or texture; commands only queue up meanwhile.
- The swap-chain resize path runs on the render thread in this mode, between frames, as it did on the main thread.
- The mode starts only once the game's own D3D thread renders; the installer's frames are unchanged.

**Present without the record wait** (`SwitchPresentWithoutRecordWait`, round 11). Present returns once the frame's
commands are handed over; the wait for the render thread's recording moves to the start of the next Present and to
buffer and texture locks. Everything the render thread reads from the game thread's per-frame memory is read before
that wait ends, so nothing it reads can change under it: the frame's constant and vertex copies are double-buffered.

**Per-object lock waits** (`SwitchPerResourceLockWait`, round 13). What a lock must not overtake is the render thread's
copy of an earlier unlock of the same object, which reads guest memory when the render thread gets to the command. Each
buffer and texture counts its unlocks sent and not yet copied (decremented right after the copy, on every path) and
remembers the Present count of the last one. A lock waits until that count is 0, only if the last unlock came from an
earlier frame, and only on the D3D thread: within a frame it never waited before either. The counters use
sequentially consistent operations and a waiter count, so a waiter that saw a pending unlock is always woken. Every
unlock command is processed (none is merged or dropped), so the count always drains. A copied or moved object starts
at 0.

**The gamma pass** (round 13). With the default brightness and no Xbox colour correction every exponent is exactly 1.
The pass reads the 8-bit intermediate image with `Load` (`k / 255` per channel) and writes an 8-bit target; `pow`
(`exp2` of `log2`) returns its input to within a few units in the last place, which can never move a stored value, so
it is skipped then (a uniform branch on push constants).

### GPU loss with four round 10 renderer changes

**What happened.** Two long sessions with round 10's config 2 lost the GPU:
- `vkQueueSubmit` returned `VK_ERROR_DEVICE_LOST`, after 21,040 and about 28,000 frames of normal play (15 to 20
  minutes, heavy scenes at 1080p).
- Then the game froze. The render thread was inside `vkQueuePresentKHR`, waiting for a sync the lost channel would never
  signal. The main thread was blocked on the queue mutex, in a copy submission of `D3DXFillTexture`.
- Nothing faulted on the CPU, so Atmosphère wrote no report. The two reports found on the SD card came from an older
  build (module id `C10DCEA7...`).

**What it is not.** Config 3 had exactly these four off: `SwitchSkipDeadCopies`, `SwitchCarryClears`,
`SwitchSkipOverwrittenDepthClears` and `SwitchEagerDepthTransitions`. Everything else was as in config 2: the round 10
shaders, the present on the render thread, the driver. It played without the loss. That rules out the shaders, the
pipelined present and the driver changes, each on its own.

Also checked on the PC, and sound:
- **The four changes' own bookkeeping.**
  - Dropping a dead copy unlinks the texture and the surface on both sides.
  - A waiting clear is made before a destruction of its surface and before the end of the frame.
  - An early transition of a surface destroyed in the same frame is skipped, since its resolves are emptied then.
  - Carried clears never happened in the logs (0 per frame).
- **The profilers' query pools.** Bounded at 4,096 draws and 160 passes.
- **The texture descriptor allocator.** Locked, and the null slots are protected. At most 1,024 descriptors were in use.
- **The pipelined present's fence and reset order.** Also its swap-chain resize and `WaitForGPU`, which both run
  between frames.
- **Mesa's C11 threads.** They are its own, on pthreads, and `mtx_init` returns 0. The sts2 port's lost channels came
  with an `mtx_init` that reported failure; that does not apply here.

**Suspects.** Two of the four change how depth buffers are cleared and when they leave the attachment layout. The
driver's ZCULL keeps a plane per depth image:
- `LOAD_ZCULL` when a pass that loads depth begins;
- `STORE_ZCULL` when it ends;
- a zero fill only when the image leaves `UNDEFINED`.

Its own comment says `LOAD_ZCULL` on bad data kills the GPU context. So the depth changes are the first suspects, but
nothing proves it yet.

**Now.** The four are off by default: 42.4 instead of 44.0 FPS at 1080p. To find the one, turn one on at a time and
play for 30 minutes or more. If the GPU is lost, `crash.log` now records the driver's reason (see
[Platform modules and diagnostics](#platform-modules-and-diagnostics)): `notification type 8` is an idle timeout (a
hang), and the other types are faults.

## Guest kernel (`kernel/imports.cpp`)

An earlier set of guest-kernel changes made the audio stop for good at the first in-game cutscene on the console:
- critical sections that spun and skipped the kernel signal;
- lock-free lookup of events and semaphores;
- a plain TLS array.

They were reverted, and the audio came back. Since then every guest-kernel change has its own switch and is tested with
that cutscene.

**Fast critical sections** (`SwitchFastCriticalSections`). A thread about to wait counts itself in the critical
section's `LockCount` before it compares the owner (the guest never reads `LockCount`; −1 means nobody waits). The
leaving thread clears the owner, issues a full barrier, then reads the count. A leaver that sees no waiter therefore
cleared the owner before any waiter compared it, so that waiter does not sleep.
- A critical section whose `LockCount` did not start at −1 keeps getting the wake-up.
- The wait's 1 ms safety timeout stays.

**Fast events and semaphores** (`SwitchFastEvents`). Waiters are counted under the object's mutex, and the notifies
are skipped only when the count is 0.

**Spin before sleeping** (`SwitchGuestSpinBeforeSleep`, round 10). The guest's spin locks read the lock word (reads
only) for up to 512 iterations before their 10 µs sleep. The acquisition itself, a compare-and-swap, is unchanged.

**Critical sections' thread id from the caller** (round 10). `RtlEnterCriticalSection` and `RtlTryEnterCriticalSection`
take r13 from the calling context instead of from `g_ppcContext`, which points to that same context.

**Relaxed guest atomics** (`SwitchRelaxedAtomics`). `stwcx.`/`stdcx.` become relaxed compare-and-swaps: PowerPC's
reservation orders no other access. The game's `sync`, `lwsync` and `eieio` stay fences.

**Lean critical-section leave** (`SwitchLeanCriticalSectionLeave`, round 12). The leaving thread's release store of
the owner and its acquire load of the waiter count are already ordered for the waiter protocol above; the full barrier
between them was redundant and is dropped. On in the round 12 and 13 configurations that were played.

**Resource waits** (`SwitchFastResourceWaits`, round 12). Five game loops pump the database loader, call `Sleep(5)` and
look again. Hooks at those loops mark the thread, and its next `Sleep(5)` sleeps 0.5 ms. Only the length of a sleep
changes; the loop still looks at the same condition until it holds.

`SwitchFastCriticalSections`, `SwitchFastEvents` and `SwitchGuestSpinBeforeSleep` are on by default in the final build.

Round 14 adds four, each off by default until a test set has played it with the first-cutscene audio check:
- **Targeted dispatcher wakeups** (`SwitchTargetedDispatcherWakeups`). `KeWaitForMultipleObjects` waits on a global
  generation that every event set and semaphore release advanced. Now each event and semaphore counts, under its own
  mutex, the multi-object waits on it; a call that may wait registers on each of its objects before it first looks at
  them and unregisters on every return. A set or release reads that count in the same critical section in which it
  changes the object, and advances the generation only if it is not 0. A waiter registered before that critical section
  is woken (the generation it read before looking moves on); one registered after it sees the new state when it looks
  (its look takes the same mutex). Reset and the waits themselves never make a waiter's condition true, and every
  signal goes through `Event::Set` or `Semaphore::Release`.
- **Semaphores wake one** (`SwitchSemaphoreWakeOne`). A release of one unit notifies one waiter instead of all. A woken
  waiter takes the unit under the mutex; a condition variable's notify-one wakes a thread still blocked, so two releases
  wake two waiters; a waiter that timed out meanwhile still takes a unit it finds (the wait checks the count), so no
  unit is left while a waiter sleeps. Releases of more than one unit still notify all.
- **Strong compare-and-swap** (`SwitchStrongCriticalSectionCas`). The enter's weak compare-and-swap could fail with the
  owner word still 0 (on AArch64 a store-exclusive fails whenever the cache line was written in between); the code took
  that 0 for an owner and waited for the word to stop being 0, on a free critical section, until a later leave or the
  wait's 1 ms safety timeout. The strong one retries such failures, so a failure always reports another owner.
- **Spin before the kernel wait** (`SwitchCriticalSectionSpin`). A thread that finds the critical section owned reads
  the owner word for up to 512 `yield` hints (about 2 µs, the guest spin locks' `SpinUntilFree`) and tries again as soon
  as it reads 0. It only delays the wait; the waiter protocol after it is unchanged.
The round 10 test builds that were played (configs 2, 3 and 5) had them on. The first-cutscene check stays the test for
any further guest-kernel change.

## Guest code replaced by native code (`misc_impl.cpp`, `patches/aspect_ratio_patches.cpp`)

**RTTI** (`SwitchNativeRtti`).
- `type_info::operator==` does the same string comparison of the decorated names. Results for pairs of type_infos in
  the executable's image are remembered, since their names never change. The memo was per thread in round 9; since
  round 10 it is one lock-free table of 64-bit entries, with the pair and the result in one word, so a reader sees a
  whole entry or none.
- `__RTtypeid` reads the locator's type descriptor. A null object or a missing descriptor runs the recompiled code,
  which throws.

**Shader constant setters** (`SwitchNativeShaderConstants`). They copy into the device's registers (16-byte aligned
stores, as `stvx`) and OR the dirty word. Overlapping ranges run the recompiled code.

**LZX decompression** (`SwitchNativeDecompress`; verify: `SwitchVerifyNativeDecompress`). Only contexts without the
streaming flag are handled; the guest resets those on every call. The native decoder writes the same bytes, size and
result. On anything it cannot reproduce with certainty it gives up, and the guest decoder runs. It was verified
identical on the console for every archive of a play session: 320 archives, 323 MB.

**Rounds 11 to 13** (`patches/native_hot_patches.cpp`, `patches/message_dispatch.cpp`, `patches/audio_dsp_patches.cpp`).
Each runs only if the guest code at its address, and of every helper it folds in, hashes to the code it was written
from (FNV-1a at boot; otherwise the recompiled code runs and stderr says so). Each leaves guest memory and every
register the context holds (r3-r10, r13, f1-f13, v0-v13, the FPSCR) exactly as the recompiled code does, because MSVC's
interprocedural register allocation lets callers read volatile registers. Stores below the stack pointer, dead once the
function returns, are not repeated. Helper addresses are written relative to the hooked one, so that
`switch-direct-calls.py` does not take them for hooks.
- **Message dispatch.** Each of the 227 dispatchers compares a message's type with a fixed list and calls the matching
  handler. The table holds the same list, decoded from the guest code at boot, and picks the same handler.
- **Exact `type_info` set.** The image's 8,368 type descriptors have pairwise different names (checked at boot), so two
  different descriptors compare unequal, as the string compare would say.
- **Map find, quaternion decoder, bone palette, layer mask test, visibility test, CRI handle search, name compare.**
  Integer code, or float code copied operation for operation (the decoder's and the palette's products and sums in the
  same order and precision). The CRI search replaces two divisions by power-of-two sizes with masks, which give the
  same results for those sizes.
- **Render-walk prefetch** (round 13): prefetches only. The entry two ahead is read only when it lies in the 4 KB page
  of the entry being tested (mapped); no value read for it reaches the answer.
- **Light field** (round 13). The decoder's values are exact in any mode: `(byte / 256)^2` needs at most 16 bits, and
  byte 24 times 1/255 is one rounding to single precision in either. The lerps compute what the compiled recompiled code
  computes: `fmla(a, b - a, t)` per lane under flush to zero (GCC had fused every `vmaddfp`), and for the last value
  `float(fma(float(1 - t), a, float(b * t)))` without it. Each value is pinned between the right pair of FPCR writes
  with empty volatile asm (the writes are volatile asm without a memory clobber, which GCC may otherwise move other
  operations across). The two buffers the last lerp reads are written where the blend helper keeps them, and the last
  lerp is the recompiled one, called with the registers the helper passes, so every register ends as before. The verify
  mode compares the result and both buffers.
- **Mixer kernels.** The recompiled bodies themselves, with the context registers in a local struct; on any exit the
  kernels did not expect they hand back to the recompiled code.
- **Fast LZX, FindFile, file handles.** Same bytes and results as the guest decoder and file functions; the decoder
  still gives up on anything it cannot reproduce.

**UI modifier cache** (`SwitchModifierCache`). Results are tagged with the generation of the path table; any change of
it (a project loaded or freed) makes them stale. Since round 10, the first thread to look up (the game's) uses a
larger table outside TLS. Round 14 (`SwitchModifierIndex`): a miss looks the address up in a hash map that holds the
same keys and values as the ordered map (both changed together under the same mutex, the first value for a key kept by
both, a freed project's range erased from both), instead of walking the ordered map.

**Round 15** (`patches/audio_dsp_patches.cpp`, `native_r15_pool.cpp`, `native_r15_material.cpp`,
`native_r15_localized.cpp`), under the same rules (code hashes, every register and guest store as the recompiled code):
- **ADX decoder** (`SwitchNativeAdxDecoder`). The guest rounds each step to single; the native's plain product and inner
  sum are single operations that give the same values (exact double products; double rounding innocuous for the sum of
  two singles, in every rounding mode), the outer step is the guest's double fused multiply-add of the same operands.
  The code table, history offset and the two scale constants are checked at boot. Outputs, input, state and pointer
  table must not overlap (else the recompiled function runs). PC test against the generated function: 900,000 calls,
  0 differences.
- **Resampler loop** (`SwitchFastAudioResampler`): a hand-written loop in the localized copy (`switch-localize.py`
  checks the loop's guest instructions before replacing it). Single operations for `fsubs`/`fadds`/`fmuls` of singles,
  the guest's double fused multiply-add for `fmadds`. PC test: 30 million samples, 0 differences.
- **Pool allocator** (`SwitchNativePoolAllocator`). The lock's virtual calls are made directly to
  `RtlEnterCriticalSection`/`RtlLeaveCriticalSection` (r3 + 4, as the wrappers) only when the vtable entry is the known
  wrapper (checked per call, the wrappers' code checked at boot). The verify mode holds the list's lock around both
  runs, so no other thread changes the list while memory is put back between them.
- **Material animation** (`SwitchNativeMaterialAnimation`). The callees are called with the same registers; the setter
  (82E46EB0) gets a forwarding hook so the verify mode compares its arguments at every call. PC test with stub callees:
  300,000 calls, 0 differences.
- **Localized copies** (`SwitchNativeSplineAnimation`, `SwitchNativePathFollowing`, `SwitchNativeMoppVm`,
  `SwitchLocalizedCriHelpers`): the recompiled statements themselves with the context's registers (and the FPSCR's
  cached mode, for the main-thread groups) in locals, helpers copied in, calls out with the context written back and
  read again. Exact by construction when nothing contracts multiply-adds differently in the copy than in the original:
  the main-thread copies are compiled only with `SWITCH_EXPLICIT_FMA` (no contraction anywhere). Made by the build from
  its own recompiled code.

## Code generation and build

**ppc/ comes from `build-switch.sh` only** (round 15). Step 4 runs XenonRecomp with its environment modes, then the
code generation pass, the direct calls and the localized copies edit its output. CMake's own rule for ppc/ ran
XenonRecomp again in step 6 whenever `SWA.toml` was newer than ninja's record of the rule, or the build folder had no
record. That run used none of the modes and no pass, and it overwrote step 4's sources. Round 15's TOML edit set it off:
the build failed only because the localized copies, made from step 4's sources, needed the code generation pass's
tables. Earlier builds were not affected (the ninja log shows no such run between 2026-09-29 and round 15).
- The Switch rule now only echoes a note.
- Step 4 regenerates ppc/ when the TOML changes: it is part of the code generation id.
- Check after a build: `ppc_config.h` holds the `// switch-codegen-pass` block when any pass option is on.

**Math flags.**
- `-fno-math-errno` only removes the `errno` update of libm calls.
- `-fno-trapping-math` lets GCC assume floating-point operations do not trap. Traps are off on the host, and
  XenonRecomp's `mffs` returns only the rounding mode.
- `-frounding-math` and the default contraction stay: the game's C runtime changes the rounding mode (`mtfsf`), so no
  transformation that assumes round-to-nearest is allowed. Neither is `-ffast-math`, nor anything else that
  reassociates.

**Round-8 code generation** (`SWITCH_CLASSIC_CODEGEN=1` builds the previous one).
- Guest memory accesses are plain instead of `volatile`.
- `PPC_LOOP_BARRIER()`, a compiler barrier on every backward branch, keeps wait loops re-reading memory.
  Memory-mapped accesses stay volatile.
- The relaxed compare-and-swaps are compiler barriers, so the accesses around the game's atomics keep their program
  order.
- Dead callee-saved stores are dropped.
- The FPSCR mode is tracked across labels (a fixpoint over the branch edges).

**Direct calls** (`SWITCH_DIRECT_CALLS=1`). A call becomes a direct call to `__imp__sub_X` only when `sub_X` has no hook.
Any `0x82…`/`0x83…` value in the app sources and recompiler configs counts as hooked, which is over-inclusive and so
safe. After linking, the build checks the ELF: every hook symbol is present, and none was bypassed.

**LTO, PGO, `-O2`, `-fipa-pta`.** These are compiler optimisations under the same language rules;
`-fno-strict-aliasing` stays everywhere.
- PGO applies a function's profile only if its control flow matches the instrumented build (a checksum per function).
  A changed function simply gets no profile.
- The profile must come from the same generated code: collect it again after regenerating `ppc/` or changing the
  direct-calls hooks.

**Round 11 code generation options** (`tools/switch-codegen-pass.py`, applied to XenonRecomp's output).
- Leaf locals: in a function that calls nothing, the context's registers are locals, read at entry and written back at
  every return (every register the function may write, without liveness analysis).
- 64-bit D-form addresses: `base + rA + displacement` computed in 64 bits for displacements 0-4095 (or any from r1). A
  32-bit wrap past the top of guest memory would differ; the guard page above the 4 GB guest range turns that case into
  a fault instead of a silent difference, and none has occurred.
- Constant VMX tables and narrow loop barriers change only how the compiler sees the code.
- **The floating-point rule.** GCC fuses a multiply and an add whenever it sees the product flow into the add
  (`-ffp-contract=fast`); a pass that shows it more data flow could change a rounding. So every function with
  double-precision or vector floating-point arithmetic is left exactly as XenonRecomp wrote it. Single precision is
  immune: each instruction rounds its result to `float`, and the product of two singles is exact in double. After each
  build, `elf_fp_check.py` counts the fused and unfused instructions of those 2,834 functions against the previous build
  (round 13: 7 differ, from code duplication and one moved fusion; rebuilds have always moved some).

**Inlined floating-point compare** (`SWITCH_INLINE_FP_COMPARE`, round 12). `fcmpu`/`fcmpo` set the same CR bits,
branchless; a compare rounds nothing.

**Prefetches** (round 13). The game's `dcbt`/`dcbtst` hints became `__builtin_prefetch` of the same guest address (the
operands are CT, RA, RB). A prefetch never faults and changes no value; GCC does not treat it as a memory access. (It
does change the function's PGO profile: all 130 functions with one no longer match the round-8 profile, found in
round 14.)

**Guest `memcpy`, `memmove`, `memset`** (`SWITCH_INLINE_MEMCPY`, round 14). The hook called the C library's function
with `base + r3`, `base + r4` (or `r4` as an `int` for `memset`) and `r5`, and set `r3` to its own low 32 bits; the
replacement line does exactly that, with the size as a constant where the guest code loads one with `li r5,N` right
before the call (only straight-line statements that leave `r5` alone in between, no label). Only overlapping copies,
undefined for `memcpy`, could differ between the library's code and GCC's inline copy. Functions with double-precision
or vector floating-point arithmetic are left alone, as by every option of the pass. GCC's profile counts call sites as
well as branches, so a function with a replaced call no longer matches a profile collected without the option (1,112
of 1,114 in round 14): the option belongs with a new PGO collection.

**Fewer FPCR mode switches** (`SWITCH_FEWER_MODE_SWITCHES`, round 14; `recompiler.cpp`, `ppc_context.h`). Where the mode
is known to be the vector unit's (FZ set), these instructions keep it:
- moves: `fmr` (a `double` copy), `fabs`/`fnabs`/`fneg` (bit operations), `lfd`/`lfdx`/`stfd`/`stfdx`/`stfiwx` (64- or
  32-bit loads and stores);
- `fcfid` (an integer as a double is 0 or at least 1: never denormal), `fctidz`/`fctiwz` (a denormal truncates to 0
  whether FZ makes it 0 first or not; the range compare is false for it either way);
- `lfs`/`lfsx` through `PPCFloatToDoubleAnyMode`: the conversion where the float's exponent is not 0, else the
  significand (an integer below 2^23, exact as a double) times 2^-149, a normal double, with the float's sign; a bitwise
  select, no branch;
- `stfs`/`stfsx`/`frsp` through `PPCDoubleToFloatBitsAnyMode`: the conversion for magnitudes of 2^-126 or more (the
  result is then a normal float and its input a normal double, which FZ leaves alone); below that the significand
  shifted to units of 2^-149 (shift 30 to 63) and rounded in the FPCR's rounding mode (from the cached FPCR in the
  context), a carry into bit 23 giving the smallest normal float as the hardware's rounding does;
- `fcmpu` through `compareBits`: unordered if either magnitude is above infinity's bits, otherwise the sign and
  magnitude as a two's complement key (both zeros 0), compared as integers.
In the other direction, where the scalar mode is known, `vcfsx`/`vcfux` (integers as floats, scaled by 2^-n with n at
most 31: 0 or at least 2^-31), `vctsxs` (a denormal times at most 2^31 truncates to 0) and `vrfin`/`vrfiz` (a denormal
rounds to the zero of its sign) keep it. The helpers were run against the hardware's own conversions and compares on
x86 with FTZ and DAZ set (which flush as FZ does), in all four rounding modes: 104,882,480 cases, 0 differences, and
the same harness without the helpers found 27 million. Where the mode is unknown (after a call or a label) nothing
changes, so no conditional switch, and no branch, is added or removed there; the unconditional switches are
`msr` without a branch. Only the modes recorded at labels change in 202 functions, which can add or remove a
conditional switch right after a label, before the first instruction that needs a mode: no floating-point arithmetic
sits between that label and the switch, so no multiply and add that GCC could fuse end up in a different basic block.
Host code called from the recompiled code already ran in either mode (after any vector instruction); the natives that
compute in floating point set the mode themselves. Level 2 also drops the conditional switches before the moves and
conversions where the mode is unknown, which leaves it unknown: the first instruction that needs a mode then switches,
so every multiply and add on either side stay apart as before, but thousands of functions' control flow changes, which
is why it waits for a PGO profile collected with it.

**Hot-function list** (round 14). `__attribute__((hot))` can change GCC's unrolling, and with it where a multiply and an
add meet in one basic block; the functions added in round 14 have no double-precision or vector floating-point
arithmetic, and every other function keeps the attribute it had.

**Shader cache stamp.** The shader cache is regenerated whenever the translator patches or `shader_common.h` change.

**Explicit multiply-adds** (`SWITCH_EXPLICIT_FMA`, round 15). XenonRecomp emits the guest's fused instructions as
`__builtin_fma` (scalar; `fmadds` and the other single ones round the double result to single), `PPCVectorFma`
(`vmaddfp`, `vmaddcfp128`: NEON `vfmaq_f32`) and `PPCVectorNegatedFms` (`vnmsubfp`: the negated fused a*b - c, as GCC
compiled the former expression), and everything is compiled with `-ffp-contract=off`, so no other multiply and add is
fused. Each statement then rounds as the guest instruction does. The code generation pass processes floating-point
functions too. Check after a build: `tools/switch-fused-check.py <ELF>` lists functions with more fused instructions than the guest.
- **Round 15 release ELF.** 5 functions are listed, all explained.
  - 3 are hooks that hold copies of the same code: a native or localized copy, and the verify mode's copy.
  - The other 2 hold the guest's own fused operations more than once: `-fprofile-use` duplicates blocks (tail
    duplication, unrolling), and their inlined callees have no fused instructions.


**Scalar reciprocal square roots** (`SWITCH_SCALAR_RSQRT`, round 15). `vrsqrtefp`/`vrefp` whose input is a `vmsum3fp`/
`vmsum4fp` result of the same block (all four lanes equal) becomes one scalar `1 / sqrtf` or `1 / x`, splatted: the
same correctly rounded value as the vector form in each lane (the tracking ends at labels, calls and mid-asm hooks).

## Driver (Mesa/NVK)

The game links NVK statically. The driver it is built with has two layers of Switch changes on top of mesa-switch
(Mesa 26.2.2 with the Horizon backend):
- **base:** the Switch driver changes made before this port;
- **this port:** UnleashedRecomp's own changes.

Driver changes are held to the same rule as the game's, with one difference: some rest on how the hardware behaves,
which the code cannot prove. Those are marked below, with what was seen on the console. The game sets driver switches
before the Vulkan instance is created: two have `[Switch]` keys (`SwitchZcullGreater`, `SwitchLinkVaryings`), and any
other goes through `SwitchMesaEnvironment = "NAME=value;NAME=value"`. [SWITCH-MESA.md](SWITCH-MESA.md) covers the build.

| Change | Layer | Default here | Turned off by | The image holds because |
|---|---|---|---|---|
| Operand reuse | this port | on | `NAK_DEBUG=noreuse` | hardware rules; identical on the console |
| Scheduler latencies | both | 200 cycles | `NAK_TEX_LATENCY`, `NAK_MEM_LATENCY`, `NAK_ATTR_LATENCY` | only the order changes |
| Branch flattening | base | on | — | the same values are selected |
| FADD32I saturation | base | on | — | a correctness fix |
| Unread varyings dropped at link | this port | on | `SwitchLinkVaryings = false` | the fragment shader reads the same values |
| Varyings of pipelines without a fragment shader | this port | on | `NVK_SWITCH_VS_ONLY_VARYINGS=0` | nothing reads them |
| Colour writes to missing attachments dropped | base | on | — | Vulkan discards them |
| ZCULL | base | on | `NVK_ZCULL=off` | hardware, with planes that match the depth buffer |
| ZCULL direction `greater` | this port | on | `SwitchZcullGreater = false` | hardware; nothing missing seen |
| Draw-path fast paths, set 4 by differences | base (opt-in: this port) | not run | — | not used by this game |
| Buffer copies on the copy engine | base | off | — | not used |
| Subtiling split | base | unchanged | `NVK_SUBTILING_KNOB` | not changed |

### Shader compiler (NAK)

**Operand reuse** (this port). Maxwell's control bits can ask the operand collector to keep the value an instruction read
in slot A, B or C for the next instruction. That instruction then takes the operand from the reuse cache instead of the
register file, which avoids register-bank conflicts. The flag is set only between two neighbouring instructions of one
basic block, so the second is never a branch target, and only when:
- both are unpredicated FADD, FMUL, FFMA, FMNMX, FSET, FSETP or SEL (`NAK_DEBUG=reusebasic` limits it to the first
  three), and the first does not yield;
- the same single GPR sits in the same operand slot in both, exactly where the encoder puts it (for FFMA with its third
  source in the constant buffer, the second source moves to slot C);
- the first does not write that register (the cached value is not invalidated by a write);
- the second waits on no scoreboard, so no variable-latency result can land in between.

The instructions compute the same thing; only where an operand is read from changes.
- **Rests on hardware behaviour.** NVIDIA documents none of this, and the rules above are inferred. On the console the
  images matched with and without it, and the GPU frame at the hub went from 22.63 to 22.49 ms.
- **Risk.** A hardware rule the conditions miss would give wrong values in the affected instructions, visible as a
  rendering difference; `NAK_DEBUG=noreuse` isolates it. The setting is part of the shader cache key, so binaries with
  and without it never mix.
- **Off since NAK revision 5 (round 13).** It was the one change whose correctness rests on undocumented hardware
  behaviour when the dark character eyes were found, so it became opt-in (`NAK_DEBUG=reuse`; `noreuse` wins). The eyes
  stayed dark without it; round 16 found their cause in the translator.
- **Asked for by the game since round 14** (`SwitchOperandReuse`, off until a test set has played it): the game sets
  `NAK_DEBUG=reuse` before the instance is created, unless `SwitchMesaEnvironment` sets `NAK_DEBUG` itself. The driver's
  default stays off for the other games built with it.

**Scheduler latencies.**
- Base: the texture and memory latencies go from 32 to 200 cycles.
- This port: attribute access is split from memory, and all three are settable with `NAK_TEX_LATENCY`,
  `NAK_MEM_LATENCY` and `NAK_ATTR_LATENCY` (1 to 1000, default 200).

These are the scheduler's estimates, used only to order instructions. The waits that make results correct (scoreboard
barriers and stall counts) are computed separately, from the hardware's latencies. So every value stays the same;
only the order and the register pressure change. The values are part of the shader cache key.

**Branch flattening** (base: NIR `peephole_select` limit 0 → 8). A small `if`/`else` becomes both sides computed plus a
select. NIR flattens only instructions without side effects (no stores, discards or barriers), and the select takes
the value the branch would have produced. Flattening can raise register pressure: a spill to local memory would cost
time, not correctness, and `NVK_SHADER_STATS=1` shows it.

**FADD32I saturation** (base). On SM50, FADD32I (the long-immediate form) has no saturation bit, so `fsat(a + imm)` lost
its clamp when encoded that way. Such adds are no longer encoded that way. This changes the output compared with a
compiler without the fix, towards what the shader says: it is a correctness fix, not a performance change.

**Cache keys.** NAK revision 5, the link flag and the scheduler latency key are part of the shader cache key and the
pipeline cache UUID. A driver change or a change of settings never reuses a binary compiled differently.

### Pipeline linking (NVK)

**Unread varyings** (this port; `SwitchLinkVaryings`, `NVK_LINK_VARYINGS`). When a pipeline has both stages, the
components of the generic outputs its fragment shader never reads are removed from the last pre-rasterization shader.
Dead-code elimination then removes the math and vertex fetches that only fed them.
- **What it touches.** Only generic 32-bit scalar or vector varyings, and nothing with transform feedback. Position,
  clip and cull distances, point size, layer and viewport are never touched.
- **What counts as unread.** A component is unread only if every use of its input variable is a direct load and none
  of them reads it. Any other access (an array index, `interpolateAt`, a copy) keeps the whole input, and so does any
  input that is not a 32-bit scalar or vector.
- **Why the image holds.** The fragment shader reads exactly the values it read before. The renderer trims the
  translated shaders the same way on its side (`SwitchTrimVertexOutputs`); this covers the pipelines it does not.

**Pipelines without a fragment shader** (this port; `NVK_SWITCH_VS_ONLY_VARYINGS`). Depth-only pipelines drop every
generic output of the vertex shader, since nothing reads them. It applies only when the pipeline state says there is
no fragment shader at all; a pipeline library that gets its fragment shader later is left alone.

**Colour writes to missing attachments** (base). Fragment-shader writes to colour attachments the pipeline does not
have are removed, along with what only fed them; Vulkan discards such writes. Writes that go to every target and the
second blend source stay, and the attachment mask is part of the pipeline key.

### ZCULL

**ZCULL on Horizon** (base). Hierarchical depth culling: the 3D engine keeps a coarse depth range per screen tile and
rejects fragments that fail the depth test for a whole tile, before they are shaded.
- **Setup.** The driver uses the kernel's ZCULL geometry, binds a ZCULL context buffer to each 3D channel, and gives
  each eligible depth image a plane (its saved ZCULL data).
  - Eligible means a depth attachment whose only other uses are read-only, with optimal tiling, and not 3D or sparse.
  - The renderer creates its depth targets without `TRANSFER_DST` for this (the plume patch).
- **At each pass with such a depth buffer.**
  - When the pass begins: `LOAD_ZCULL` if depth is loaded, `CLEAR_ZCULL_REGION` if it is cleared or discarded.
  - When it ends: `STORE_ZCULL` if depth is stored.
  - A layout change from `UNDEFINED` fills the plane with zeros first.
- **Rests on hardware behaviour.** ZCULL rejects only fragments the depth test would reject, as long as the plane
  describes the depth buffer. The driver does not update the plane in two cases:
  - writes outside depth passes, hence the eligibility rule;
  - depth clears made inside a pass with `vkCmdClearAttachments` (a TODO in the driver), which is how the renderer
    clears. Those rely on the 3D engine's own clear keeping ZCULL right.
- **Seen on the console.** No missing pixels, but little gain either: `NVK_ZCULL=off` changed no pass at the hub.
- **Risk.** The driver's own comment says `LOAD_ZCULL` on bad plane data kills the GPU context. That makes ZCULL the
  first suspect in the [GPU loss](#gpu-loss-with-four-round-10-renderer-changes), together with two of the round 10
  depth changes.

**ZCULL direction** (this port; `SwitchZcullGreater`, `NVK_ZCULL=less|greater|off`). The driver used `ZDIR_LESS` for
every depth target, as NVIDIA's driver does, but the game's main pass tests depth `GREATER` (reverse Z).
- `greater` uses `ZDIR_GREATER` for every target; `off` never enables ZCULL.
- One direction holds for the whole run, so a plane is always loaded with the direction it was stored with.
- **Rests on hardware behaviour.** Passes that test the other way (the shadow maps) are expected to lose the culling,
  not to cull wrongly. On the console nothing was missing in either kind of pass, and `greater` gave −0.17 ms, mostly
  in the half-resolution pass with depth.

### Not used by this game

- **Indirect draws on Maxwell** (`vkCmdDrawIndirect`): the pre-Turing path pushes `CALL_MME_MACRO(NVK_MME_DRAW_INDIRECT)`
  and its first words, then `nvk_cmd_buffer_push_indirect`, which on Horizon flushes a deferred MME sync first. The
  constant-buffer path's comment says that order puts semaphore methods between the macro header and its data and
  kills the channel; that path was fixed (the sync is flushed before the header), the indirect draw was not. Round 15
  avoided it (copies decided at submit read their word in the vertex shader instead). Fix before any use.

- **Draw-path fast paths and set 4 by differences** (base; opt-in since this port).
  - The fast paths: cheaper draw emission, fewer constant-buffer rebinds, dynamic-state shortcuts and pipeline
    prefetch. Set 4 by differences writes dynamic uniform buffers to the root table by differences. Each has a
    self-check that turns it off on a mismatch.
  - They run only for an application that asks through the driver's shared structures (`pedido > 0`). UnleashedRecomp
    asks only with `SwitchNvkFastPaths = true`, which is off by default; the log line `NVK fast paths: off (...)`
    confirms it.
- **Buffer copies on the copy engine** (base). Off unless `NVK_COPY_ENGINE=1`.
- **Subtiling** (base). `NVK_SUBTILING_KNOB` can override the fragment subtiling split. Without it, the value is the
  one the driver always used on the T210.

### Removed

Removed in round 7, because the round-4 test set measured no gain from them:
- the vertex-attribute trimming (`NVK_SWITCH_VI_READ_ONLY`);
- the early depth test for discarding shaders (`NVK_SWITCH_EARLY_Z_KILL`);
- small-target compression (`NVK_SWITCH_COMPRESS_MIN_KB`).

A driver change stays only with a measured gain.

### Build and diagnostics (no effect on output)

- `build-unified.sh` can rebuild incrementally (`MESA_SWITCH_INCREMENTAL=1`), and retries ninja once when parallel
  bindgen steps collide on Windows.
- `NVK_SHADER_STATS=1` prints, for each compiled shader, its registers, local memory, occupancy, size, instructions,
  static cycles and spills. Each line is keyed by the start of the BLAKE3 of its SPIR-V, which the draw profiler prints
  too.

### Where the driver is

The driver the final build links is published as the `unleashedrecomp-driver` branch of `ChanseyIsTheBest/mesa-switch`,
in three layers:
1. `0d9d4f08`: mesa-switch with Mesa 26.2.2. The build's local source tree has the same source code; it lacks only files
   ignored by git and the executable bits.
2. `acc21efd`: the base changes above, described in the branch's `nfsmw/README.md`.
3. `1e47a108`: this port's changes above.

The patch file in that `nfsmw/` folder is the branch's exact diff from `0d9d4f08`, and it applies cleanly. The branch
stays on 26.2.2: moving it to the repository's 26.2.3 needs a new build and test.

## Platform modules and diagnostics

- **apm** (`SwitchHandheldGpuBoost`, on since 1.0.0). Requests 0x92220008 only in handheld mode, polls the clocks for 1.5 s and
  reverts if the memory clock moved.
- **SaltyNX / Status Monitor** (`SwitchOverlayFps`). When the process has no free port session, it may release `sm:`
  for a few milliseconds: not in the first seconds, then at most once a minute. A service opened in that window would
  fail.
- **Logs** (`SwitchLog`, off by default). With it on, stderr goes to `stderr.log` next to the NRO, line-buffered,
  the game's log to `UnleashedRecomp.log`, and crash reports to `crash.log`. Otherwise no file is opened: the
  logger keeps its first lines in memory until the config is read, then drops them.
- **Profilers** (all off by default):
  - The pass and draw profilers only add timestamps, plus a flush before each draw timestamp.
  - The CPU sampler pauses each thread for a few microseconds every 2 ms.
  - Reports are formatted and written by a background thread, in one write. Writing line by line from a thread others
    wait on caused the freezes fixed in round 9. The sampler never holds its lock while writing, and the game thread
    only tries that lock.
  - The frame log (`SwitchFrameLog`) formats one frame's text a minute on the render thread.
- **Stall watchdog** (`SwitchStallWatchSeconds`, 0 = off by default; 1 for tests): a thread at priority 0x1E that only
  reads. With it off, frames are still counted, for the crash reports.
- **Crash reports** (`os/switch/crash_switch.cpp`). With `SwitchLog` (since 1.0.0; before, always) they are
  appended to `crash.log` next to the NRO. Each is followed by `svcBreak` either way, so Atmosphère writes its report.
  Once a report has started, nothing takes a lock or allocates: the report is formatted in a static buffer and written
  through the file system service directly.
  - A CPU exception goes to the libnx user exception handler, after the battd_nx port's `nx_crash_handler.c`. It writes
    the thread, fault (pc, lr, far, esr), registers, frame-pointer chain, return addresses found on the stack, and a
    dump of the stack. The offsets are `[crash] +0x...` lines, which `tools/switch-cpu-profile.py` names.
  - A lost GPU is reported through plume's callback, on the first `VK_ERROR_DEVICE_LOST` from a submission, fence wait,
    query read, present or acquire. The report carries the driver's last error messages, among them its
    `channel N lost: notification={...} error={...}`. A release Mesa gives those only to a `VK_EXT_debug_utils`
    messenger, so plume now registers one on Switch, for errors only. With a messenger, Mesa formats its rare
    warnings and errors before filtering them, which costs nothing measurable. This replaced the freeze described
    above.
- **Docked and handheld** (display defaults the user chose, not performance changes).
  - **Window.** 1920x1080 docked, 1280x720 in handheld mode (`GameWindow`). The mode comes from
    `appletGetDefaultDisplayResolution` and its change event, polled once per frame with no wait. No applet message
    loop runs, so `appletGetOperationMode()` would keep its first value. A dock set to 720p counts as handheld.
  - **Switching at run time.** The new size goes to plume (`SetSwitchSwapChainSize`), which rebuilds the swap chain
    between frames. The NWindow cannot be resized while a swap chain holds buffers, so the WSI retires the old swap
    chain and sets the new size. The game then remakes its render targets, as after a window resize on the PC. The
    NWindow crop is off, so the whole buffer is shown at either size.
  - **Internal resolution.** Kept per mode: `SwitchDockedResolutionScale` 0.8 (1536x864) and
    `SwitchHandheldResolutionScale` 0.9 (1152x648). The Resolution Scale option shows and changes the current mode's
    value, and its reset gives that mode's default.
- Since round 10, the sampler and the watchdog see system calls where Horizon reports a blocked thread: with its PC at
  the SVC instruction. They had only looked right after it, so no wait had been attributed before.

## Known limitations

- **Unlock race** (as upstream). The render thread copies a buffer's or texture's contents from guest memory when it
  processes the unlock; the game could rewrite that memory first if the render thread fell far behind. Batching keeps
  the render thread close; locks wait for that object's unlocks from earlier frames (per object since round 13).
- `KeSetBasePriorityThread` maps guest priorities around 0x2C, while threads start at 0x3B.
- Loader-thread writes into GPU-upload vertex and index buffers can race GPU reads of the same buffer (as upstream).
- plume never invalidates read-back memory before the CPU reads it (as upstream; nothing is read back during play).
- The quad sinking variant is slower on this driver than the per-pixel one (0.57 ms at 1080p), so it is off by
  default.
- The four round 10 renderer changes above are off: one or more of them loses the GPU in long play, and which one is
  not known yet.
- Switching between docked and handheld while the game runs is new in the final build, and has not been tested on
  the console yet.

## Checklist for a new build on the console

1. With `SwitchLog = true`: the first line of `stderr.log` names the build, and the "Switch round" lines show which
   switches are on. The final build writes no log without it.
2. After a translator change: one session each with `SwitchVerifyShadowGather = true` and
   `SwitchVerifyTextureSizes = true` (menus, a day stage, a night stage, a hub). No magenta.
3. After a change to the LZX decoder: one session with `SwitchVerifyNativeDecompress = true`. Every `[lzx]` line says
   all identical.
4. After a guest-kernel or audio change: play into a stage until its first in-game cutscene. The audio must keep going
   during and after it (round 14's kernel switches passed it; round 15's audio natives need it).
5. After a change to native code: one session with `SwitchVerifyMessageDispatch`, `SwitchVerifyNativeHotFunctions` and
   `SwitchVerifyNativeAudio` on and `SwitchVerifyEvery = 32` (hub, a light-heavy stage, a cutscene, a few loads; since
   round 15 also a boss or stage with physics, for the MOPP machines). No `MISMATCH` line. Calls that leave a localized
   copy (virtual calls, recursion) are counted, not compared.
6. A/B the new switches with the test-set method in SWITCH-PERFORMANCE.md.
7. Dock and undock during a stage. The picture follows at 1080p or 720p, and Resolution Scale shows that mode's value.
8. If the game closes with an error, turn `SwitchLog` on, reproduce it, read `crash.log` next to the NRO, and name
   its offsets with `tools/switch-cpu-profile.py crash.log --elf <that build's ELF>`.
