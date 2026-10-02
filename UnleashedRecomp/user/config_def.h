// This file gets included in both config.h and config.cpp, with their own macros changing
// the preprocessed output. The header is only going to have the declarations this way.

CONFIG_DEFINE_ENUM_LOCALISED("System", ELanguage, Language, ELanguage::English);
CONFIG_DEFINE_ENUM_LOCALISED("System", EVoiceLanguage, VoiceLanguage, EVoiceLanguage::English);
CONFIG_DEFINE_LOCALISED("System", bool, Subtitles, true);
CONFIG_DEFINE_LOCALISED("System", bool, Hints, true);
CONFIG_DEFINE_LOCALISED("System", bool, ControlTutorial, true);
CONFIG_DEFINE_LOCALISED("System", bool, AchievementNotifications, true);
CONFIG_DEFINE_ENUM_LOCALISED("System", ETimeOfDayTransition, TimeOfDayTransition, ETimeOfDayTransition::Xbox);
CONFIG_DEFINE("System", bool, ShowConsole, false);

CONFIG_DEFINE_ENUM_LOCALISED("Input", ECameraRotationMode, HorizontalCamera, ECameraRotationMode::Normal);
CONFIG_DEFINE_ENUM_LOCALISED("Input", ECameraRotationMode, VerticalCamera, ECameraRotationMode::Normal);
CONFIG_DEFINE_LOCALISED("Input", bool, Vibration, true);
CONFIG_DEFINE_LOCALISED("Input", bool, AllowBackgroundInput, false);
CONFIG_DEFINE_ENUM_LOCALISED("Input", EControllerIcons, ControllerIcons, EControllerIcons::Auto);

CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_A, SDL_SCANCODE_S);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_B, SDL_SCANCODE_D);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_X, SDL_SCANCODE_A);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_Y, SDL_SCANCODE_W);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_DPadUp, SDL_SCANCODE_UNKNOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_DPadDown, SDL_SCANCODE_UNKNOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_DPadLeft, SDL_SCANCODE_UNKNOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_DPadRight, SDL_SCANCODE_UNKNOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_Start, SDL_SCANCODE_RETURN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_Back, SDL_SCANCODE_BACKSPACE);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_LeftTrigger, SDL_SCANCODE_1);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_RightTrigger, SDL_SCANCODE_3);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_LeftBumper, SDL_SCANCODE_Q);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_RightBumper, SDL_SCANCODE_E);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_LeftStickUp, SDL_SCANCODE_UP);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_LeftStickDown, SDL_SCANCODE_DOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_LeftStickLeft, SDL_SCANCODE_LEFT);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_LeftStickRight, SDL_SCANCODE_RIGHT);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_RightStickUp, SDL_SCANCODE_UNKNOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_RightStickDown, SDL_SCANCODE_UNKNOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_RightStickLeft, SDL_SCANCODE_UNKNOWN);
CONFIG_DEFINE_ENUM("Bindings", SDL_Scancode, Key_RightStickRight, SDL_SCANCODE_UNKNOWN);

CONFIG_DEFINE_LOCALISED("Audio", float, MasterVolume, 1.0f);
CONFIG_DEFINE_LOCALISED("Audio", float, MusicVolume, 1.0f);
CONFIG_DEFINE_LOCALISED("Audio", float, EffectsVolume, 1.0f);
CONFIG_DEFINE_ENUM_LOCALISED("Audio", EChannelConfiguration, ChannelConfiguration, EChannelConfiguration::Stereo);
CONFIG_DEFINE_LOCALISED("Audio", bool, MusicAttenuation, false);
CONFIG_DEFINE_LOCALISED("Audio", bool, BattleTheme, true);

CONFIG_DEFINE("Video", std::string, GraphicsDevice, "");
CONFIG_DEFINE_ENUM("Video", EGraphicsAPI, GraphicsAPI, EGraphicsAPI::Auto);
CONFIG_DEFINE("Video", int32_t, WindowX, WINDOWPOS_CENTRED);
CONFIG_DEFINE("Video", int32_t, WindowY, WINDOWPOS_CENTRED);
CONFIG_DEFINE_LOCALISED("Video", int32_t, WindowSize, -1);
CONFIG_DEFINE("Video", int32_t, WindowWidth, 1280);
CONFIG_DEFINE("Video", int32_t, WindowHeight, 720);
CONFIG_DEFINE_ENUM("Video", EWindowState, WindowState, EWindowState::Normal);
CONFIG_DEFINE_LOCALISED("Video", int32_t, Monitor, 0);
CONFIG_DEFINE_ENUM_LOCALISED("Video", EAspectRatio, AspectRatio, EAspectRatio::Auto);
CONFIG_DEFINE_LOCALISED("Video", float, ResolutionScale, 1.0f);
CONFIG_DEFINE_LOCALISED("Video", bool, Fullscreen, true);
CONFIG_DEFINE_LOCALISED("Video", bool, VSync, true);
#if defined(__SWITCH__)
CONFIG_DEFINE_LOCALISED("Video", bool, FrameGeneration, false);
CONFIG_DEFINE_HIDDEN("Video", float, FrameGenerationFlowScale, 0.25f);
CONFIG_DEFINE_HIDDEN("Video", bool, FrameGenerationPerformanceMode, true);
#endif
CONFIG_DEFINE_ENUM("Video", ETripleBuffering, TripleBuffering, ETripleBuffering::Auto);
CONFIG_DEFINE_LOCALISED("Video", int32_t, FPS, 60);
CONFIG_DEFINE("Video", bool, ShowFPS, false);
CONFIG_DEFINE("Video", uint32_t, MaxFrameLatency, 2);
CONFIG_DEFINE_LOCALISED("Video", float, Brightness, 0.5f);
CONFIG_DEFINE_ENUM_LOCALISED("Video", EAntiAliasing, AntiAliasing, EAntiAliasing::MSAA4x);
CONFIG_DEFINE_LOCALISED("Video", bool, TransparencyAntiAliasing, true);
CONFIG_DEFINE("Video", uint32_t, AnisotropicFiltering, 16);
CONFIG_DEFINE_ENUM_LOCALISED("Video", EShadowResolution, ShadowResolution, EShadowResolution::x4096);
CONFIG_DEFINE_ENUM_LOCALISED("Video", EGITextureFiltering, GITextureFiltering, EGITextureFiltering::Bicubic);
CONFIG_DEFINE_ENUM("Video", EDepthOfFieldQuality, DepthOfFieldQuality, EDepthOfFieldQuality::Auto);
CONFIG_DEFINE_ENUM_LOCALISED("Video", EMotionBlur, MotionBlur, EMotionBlur::Original);
CONFIG_DEFINE_LOCALISED("Video", bool, XboxColorCorrection, false);
CONFIG_DEFINE_ENUM_LOCALISED("Video", ECutsceneAspectRatio, CutsceneAspectRatio, ECutsceneAspectRatio::Original);
CONFIG_DEFINE_ENUM_LOCALISED("Video", EUIAlignmentMode, UIAlignmentMode, EUIAlignmentMode::Edge);

CONFIG_DEFINE_HIDDEN("Codes", bool, AllowCancellingUnleash, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, DisableAutoSaveWarning, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, DisableBoostFilter, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, DisableDLCIcon, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, DisableDPadMovement, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, DisableDWMRoundedCorners, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, DisableLowResolutionFontOnCustomUI, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, EnableEventCollisionDebugView, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, EnableGIMipLevelDebugView, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, EnableObjectCollisionDebugView, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, EnableStageCollisionDebugView, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, FixEggmanlandUsingEventGalleryTransition, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, FixUnleashOutOfControlDrain, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, HomingAttackOnJump, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, HUDToggleKey, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, SaveScoreAtCheckpoints, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, SkipIntroLogos, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, UseAlternateTitle, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, UseArrowsForTimeOfDayTransition, false);
CONFIG_DEFINE_HIDDEN("Codes", bool, UseOfficialTitleOnTitleBar, false);

CONFIG_DEFINE("Update", time_t, LastChecked, 0);

// Switch-only options, hidden from the options menu but always written to config.toml (Config::Save, since 1.0.0),
// with their defaults the first time. They stay last: Config::Save writes a section header whenever the section
// changes from one option to the next, so an option of this section placed between two options of another section
// would split that section in two, and TOML rejects the second [Video] header, i.e. the whole file.
// Performance-only switches; none of them changes what is drawn. See docs/SWITCH-PERFORMANCE.md.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchConstantsUBO, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineCache, true);
// Requests the stock 460.8 MHz handheld GPU profile (memory stays at 1331.2 MHz), the configuration commercial games
// ask for. On by default since 1.0.0; it trades some battery life and heat in handheld mode for GPU headroom.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchHandheldGpuBoost, true);
// Publishes FPS and render resolution for Status Monitor / SaltyNX overlays.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchOverlayFps, true);
// The window follows the console's output: 1920x1080 docked, 1280x720 in handheld mode (a dock set to 720p counts
// as handheld). The Resolution Scale video option is kept per mode in these two: 0.8 gives 864p docked, 0.9 gives
// 648p in handheld mode. Changing the option changes the value of the current mode.
CONFIG_DEFINE_HIDDEN("Switch", float, SwitchDockedResolutionScale, 0.8f);
CONFIG_DEFINE_HIDDEN("Switch", float, SwitchHandheldResolutionScale, 0.9f);
// Diagnostics: write stderr.log (the renderer's and the driver's messages, the profilers' reports),
// UnleashedRecomp.log and, on a crash, crash.log, next to the NRO. Off by default: no log files at all (a crash
// still leaves Atmosphère's own report).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLog, false);
// Requests the self-checking draw-path fast paths of nfsmw-nx's Mesa (no effect with other drivers). Off:
// the driver now runs them only for games that ask, and round 4's GPU-bound test showed no difference.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNvkFastPaths, false);
// Shows the built-in profiler (F1 on PC); the GPU time now accounts for NVK's 1.627 ns ticks.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchShowProfiler, false);
// Depth-only draws whose pixel shader has no effect are built without a fragment stage.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchDepthOnlyWithoutPixelShader, true);
// Barrier access masks derived from the image layouts instead of MEMORY_READ | MEMORY_WRITE.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPreciseBarriers, true);
// 2D texture sizes read from the shared constants instead of GetDimensions() queries.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTextureSizeConstants, true);
// Debug: keep the queries and draw magenta wherever a constant size would differ from them.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyTextureSizes, false);
// Render commands from the game's D3D thread sent to the render thread in one batch per draw.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchBatchRenderCommands, true);
// Measurement: GPU time of every render pass (each render-target/depth-buffer change), averaged
// and written to stderr.log every ~5 seconds. Off by default: timestamps between passes add a
// little GPU time of their own.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchGpuPassProfiler, false);
// Measurement (turns the pass profiler on too): GPU time of every draw, grouped by shaders and
// printed for the most expensive passes. Costs more GPU time than the pass profiler alone.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchGpuDrawProfiler, false);
// Vertex shaders skip the outputs the pipeline's pixel shader never reads (all of them but the
// position in depth-only pipelines without a pixel shader).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTrimVertexOutputs, true);
// A 16,384-entry texture descriptor heap (64 KB), which the driver reads from a hardware constant bank
// instead of memory (GPU frame -6 % at the hub). Textures beyond it would render black (logged).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCompactTextureHeap, true);
// Shadow-map filters (4 or 9 point fetches) read with 1 or 4 gathers wherever that returns the very same
// texels: point-filtered, single-mip, power-of-two shadow maps.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchShadowGather, true);
// Debug: do both and draw magenta wherever a gather differs from the point fetches it replaces.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyShadowGather, false);
// Vertex shaders that branch on "has bones" get pipelines built for the value the draw uses (compiled in
// the background; the generic pipeline draws until then).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchBonesSpecialization, true);
// A/B: bone palettes and other per-vertex-indexed constant arrays read through memory (L1) instead of
// the constant bank. Which is faster depends on the scene; off keeps the constant bank.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchIndexedConstantsFromMemory, false);
// Pixel shaders skip the colour channels nothing writes or blends (colour write mask, no target).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTrimPixelOutputs, true);
// Vertex/index buffers the game rewrites mid-frame are drawn from the upload ring instead of being
// copied into place between draws (each copy needed a GPU pipeline drain); written back once per frame.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchStreamingBuffers, true);
// Z-cull in "greater" mode (NVK_ZCULL=greater) unless SwitchMesaEnvironment sets NVK_ZCULL itself.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchZcullGreater, true);
// Driver: vertex shader output components the pixel shader never reads are removed when the two are
// linked into a pipeline (NVK_LINK_VARYINGS), unless SwitchMesaEnvironment sets it itself.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLinkVaryings, true);
// Round 14, driver: the shader compiler's Maxwell operand reuse flags (NAK_DEBUG=reuse, opt-in in the driver since NAK
// revision 5), unless SwitchMesaEnvironment sets NAK_DEBUG itself. Changing it recompiles every shader once.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchOperandReuse, true);
// Resolve copies (render target to texture) are made only when something needs them: a depth copy
// waits while draws only test depth, and a clear makes only the copies of the surfaces it clears.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLazyResolves, true);
// A render target resolved into one texture and then cleared gives the texture its image instead of
// being copied into it (the texture's old image is what gets cleared).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchResolveHandOver, true);
// Colour resolves still pending at the end of a frame stay pending (copied, or handed over, when the
// render target next changes) instead of being copied at every frame's end.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchKeepResolvesPending, true);
// Draws without a fragment stage leave the pixel shader constants for the next draw that has one.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipUnusedPixelConstants, true);
// Loading screens also build the skinning variant (SwitchBonesSpecialization) each model will use.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPrecompileBonesVariants, true);
// Also save the pipeline cache every minute during play, not only when a loading screen ends (a save
// can make the render thread wait if it creates a pipeline at the same time).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineCacheSaveDuringPlay, false);
// A render target resolved into one texture and then drawn into by a draw that neither blends nor leaves
// a channel unwritten gives the texture its image; afterwards, only the pixels the draw did not write
// (marked in an 8-bit stencil buffer) get the old contents, instead of a full copy before the draw.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCoverageHandOver, true);
// Shader constant uploads copy only the registers the draw's shaders can read.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTrimConstantUploads, true);
// Alpha-tested pixel shaders skip the arithmetic between their alpha output and the alpha test for the
// pixels the test discards.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAlphaTestEarlyOut, true);
// Game audio played through the console's audout service from the audio pump thread. false: through
// SDL as before, whose audio thread runs at the lowest priority and could stall for good (silence) when
// the CPU was busy.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAudioOut, true);
// The game's atomic operations (PowerPC lwarx/stwcx.) as plain compare-and-swaps, without the full memory
// barrier the recompiled code added to each: PowerPC's own stwcx. orders nothing, and the game's explicit
// barriers (sync, lwsync, eieio) stay. false: every atomic is a full barrier, as before.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchRelaxedAtomics, true);
// The game's thread copies only the shader constant registers that changed for each draw, instead of the
// whole span between the first and the last changed one.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSparseConstantCopies, true);
// The game's thread hands its render commands over every few draws instead of after every draw: fewer
// wake-ups of the render thread, each paid with a kernel call on the game's thread.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchBatchSeveralDraws, true);
// Pipelines whose shadow-map slots are gatherable for the draw are built without the check and the
// point-fetch fallback (built in the background; the generic pipeline draws until then).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchShadowGatherSpecialization, true);
// A render target left with pending resolves moves to the texture layout with the change of render
// target, instead of mid-pass when its texture is first sampled (fewer GPU waits for idle).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchEagerSampleTransitions, true);
// A/B: the game's threads start on the core its SetThreadIdealProcessor calls name (Xbox 360 hardware
// thread n -> Switch core n / 2), still free to move to the others. false: the scheduler places them.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchThreadIdealCores, false);
// Round 7, GPU. The port's full-screen copies (resolves, coverage fix-ups, the gamma pass) draw one triangle
// over the target instead of that triangle plus a second one over half of it again.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSingleCopyTriangle, true);
// A hand-over at a draw (SwitchCoverageHandOver) whose draw provably writes every pixel of the target (one
// full-screen rectangle through a pass-through vertex shader, no discard) needs no stencil marks and no
// fix-up; only the few edge pixels the rasterizer may leave get the old contents first.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchExactCoverage, true);
// A colour clear followed by such a draw into the same target is not made (only those edge pixels are
// cleared); anything else makes it first, as before.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipOverwrittenClears, true);
// Draws that can write nothing (no colour channel, no depth) are not sent to the GPU.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipNoOpDraws, true);
// Alpha-tested pixel shaders also move the shadow gathers and arithmetic that only feed the colour into
// their early-out (SwitchAlphaTestEarlyOut), so discarded pixels skip them too; shaders that compute their
// alpha late get the early-out for that reason.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAlphaTestSink, true);
// Blended pixels that would leave an 8-bit target unchanged (transparent or black, depending on the blend)
// are discarded before the blend, which saves reading and writing the target.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipTransparentPixels, true);
// Round 7, CPU. The game's thread does not send a render state (sampler state) again when the render thread
// already has that value.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipRedundantRenderStates, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipRedundantSamplerStates, true);
// The game's thread hands its render commands over in batches of up to 256 (sent at 128) instead of 128 (64).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLargerCommandBatches, true);
// Those batches go through a producer token of their own instead of the queue's per-thread lookup.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchRenderQueueToken, true);
// type_info comparisons and typeid (the game's dynamic_casts) run as native code.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeRtti, true);
// The D3D device's shader constant setters run as native code (a copy and a flag update).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeShaderConstants, true);
// The UI's aspect-ratio modifier lookups (once per scene, cast node and cast drawn) are cached.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchModifierCache, true);
// The render thread starts on core 1 and the pipeline compiler threads on core 2, away from the game's main
// thread on core 0 (all may still move to any core).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchHostThreadCores, true);
// The render thread keeps its last pipeline and sampler lookups for states it sees again.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineLookupCache, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSamplerCache, true);
// Round 8, GPU. With SwitchAlphaTestSink, alpha-tested (and transparent-skipping) pixel shaders also move
// their texture fetches and derivatives past the alpha test for every 2x2 quad of pixels that are all
// discarded (such a quad skips them; a quad with a kept pixel runs them on all four, as before).
// Off since round 9: measured 0.57 ms slower in the main pass at 1080p than the per-pixel sinking alone.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAlphaTestQuadSink, false);
// A draw that copies a render target's (or depth buffer's) own pending resolve back into it, as the game does
// to restore EDRAM on the Xbox 360, is skipped: every pixel it would write already holds that value.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipRestoreDraws, true);
// Round 8, CPU. The game's LZX decompression of its archives (XMemDecompress, most of a loader thread's time
// while a stage loads or streams) runs as native code with the same output. Verify: the game's decoder runs
// too and each result is compared ("[lzx]" lines in stderr.log).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeDecompress, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyNativeDecompress, false);
// Guest critical sections skip the wake-up system call when nobody waits (kernel/imports.cpp). A change to the
// guest kernel: on since the round 9 test builds were played with it (audio included).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastCriticalSections, true);
// Guest events and semaphores skip their wake-up system calls when nobody waits. Also a guest kernel change, on
// since round 9 as well.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastEvents, true);
// The game thread's batches of render commands go to the render thread as the buffer they were written in (the
// render thread runs them there and gives the buffer back), not copied into the queue and out of it again.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchZeroCopyBatches, true);
// Measurement: where the registered threads spend CPU time, sampled every 2 ms and written to stderr.log
// every 30 s as code addresses (tools/switch-cpu-profile.py names them). Per-thread CPU use is printed
// with the GPU pass profiler report either way.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCpuProfiler, false);
// Diagnostics: after this many seconds without a presented frame, write what every thread is doing to
// stderr.log ("[stall]" lines; tools/switch-cpu-profile.py names them). 0 turns the watchdog off (release
// builds); 1 for tests, with SwitchLog.
CONFIG_DEFINE_HIDDEN("Switch", float, SwitchStallWatchSeconds, 0.0f);
// Diagnostics: once a minute (five times at most), everything the render thread does in one frame, in order
// ("[frame]" lines in stderr.log): framebuffers, clears, resolves and their copies, barrier batches, draws.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFrameLog, false);
// Round 9, GPU. Off: long play sessions with these four renderer changes on lost the GPU (VK_ERROR_DEVICE_LOST,
// twice, after 21,000 and 28,000 frames) and froze; with the four off (round 9 config 3) the same play did not.
// Which one does it is not known yet: test them one at a time before turning any back on.
//
// A pending resolve whose texture the commands already queued show is resolved into again (or destroyed) before
// anything reads it is dropped instead of copied when its surface is drawn into or cleared.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipDeadCopies, false);
// A colour clear waiting for its draw (SwitchSkipOverwrittenClears) also waits through target changes, depth
// clears and draws into other targets, so it is made in the pass that draws into its surface.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCarryClears, false);
// A clear of a depth buffer alone waits for the next draw into that depth buffer and is made inside its pass;
// when that draw writes every depth pixel regardless of the old values, it is not made.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipOverwrittenDepthClears, false);
// A depth buffer left with pending resolves is made ready for sampling with the barriers of the change of
// target, not in a barrier batch of its own in the middle of a later pass (the shadow maps in the main pass).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchEagerDepthTransitions, false);
// Round 9, CPU. Present waits only until the render thread has recorded the frame; the render thread submits
// it, presents, waits for the GPU and acquires the next image while the game thread starts its next frame.
// Measured +1.6 FPS where the CPU limits the frame (round 9 configs 5 and 6).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPresentOnRenderThread, true);
// With SwitchLargerCommandBatches: while the render thread waits for work, the game thread hands its commands
// over in batches of up to 512 instead of 128, each hand-over then waking it with a system call.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchIdleRenderThreadBatches, true);
// Horizon priority of the render thread (lower is more important): 0x2D is just below the game's threads (0x2C).
CONFIG_DEFINE_HIDDEN("Switch", int32_t, SwitchRenderThreadPriority, 0x2D);
// Guest spin locks watch the lock word for ~2 µs before sleeping (their owner usually runs on another core). A
// guest-kernel change, on since the round 9 test builds were played with it.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchGuestSpinBeforeSleep, true);
// Environment variables for the Mesa/NVK driver, "NAME=value;NAME=value", set before the Vulkan
// instance is created (e.g. "NVK_SWITCH_DIBUJO=0" to A/B the driver's draw-path changes).
CONFIG_DEFINE_HIDDEN("Switch", std::string, SwitchMesaEnvironment, "");

// ---- Round 11 CPU changes (test set 10). Each is exact; on by default since round 14 (played in rounds 11-13).
// [group A1] Player/object message dispatch as a native table lookup (patches/message_dispatch.cpp);
// the verify mode runs both paths and logs any difference.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeMessageDispatch, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyMessageDispatch, false);
// [group A1] type_info== by an exact set of the image's type descriptors (misc_impl.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchExactTypeInfoSet, true);
// [group A2] Native hot guest functions (patches/native_hot_patches.cpp): map find, quaternion decoder,
// bone palette upload, render-layer mask test; the verify mode runs both and logs any difference.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeMapFind, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeQuatDecode, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeBonePalette, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeLayerMaskTest, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyNativeHotFunctions, false);
// [group C] Present does not wait for the render thread to finish recording the frame (gpu/video.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPresentWithoutRecordWait, true);
// [group E] The CRI sound mixer's hot kernels as exact native code (patches/audio_dsp_patches.cpp);
// the verify mode runs both and logs any difference. Needs the first-cutscene audio check.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeReverb, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeMixKernels, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeVoiceKernels, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyNativeAudio, false);
// [group F1] Native LZX decoder with its state in registers (os/switch/lzx_switch.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastNativeDecompress, true);
// [group F2] Pipeline cache saved only after real misses; optionally once after the post-load compiles.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineCacheSaveOnMiss, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineCacheSaveAfterCompiles, false);
// [group F3] Guest FindFirstFile from directory entries; read-only guest files on libnx handles.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeFindFile, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeFileHandles, true);

// ---- Round 12 CPU changes (frame dips). Each is exact; on by default since round 14 (played in rounds 12-13).
// The game's "pump the database loader, Sleep(5), look again" waits for a resource poll every 0.5 ms
// (patches/native_hot_patches.cpp, kernel/imports.cpp): only the host's sleep is shorter.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastResourceWaits, true);
// Native render-walk visibility test (82E26498 with the leaf it calls), CRI playback-handle search (83167518 and its
// three helpers) and resource-name compare (82DFAF58 with its two helper calls) (patches/native_hot_patches.cpp). The
// verify switch of round 11 (SwitchVerifyNativeHotFunctions) covers them too.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeVisibilityTest, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeCriHandleSearch, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeNameCompare, true);
// RtlLeaveCriticalSection without the fence before it looks for waiters (kernel/imports.cpp, with
// SwitchFastCriticalSections): the store-release that frees it and the load-acquire of the waiter count are already
// ordered. Guest synchronisation: needs the first-cutscene audio check.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLeanCriticalSectionLeave, true);
// The verify modes (SwitchVerifyMessageDispatch, SwitchVerifyNativeHotFunctions, SwitchVerifyNativeAudio) compare one
// call in this many (1 = every call, as in round 11, which was too slow to reach a stage).
CONFIG_DEFINE_HIDDEN("Switch", int32_t, SwitchVerifyEvery, 1);
// CPU sampler: frames whose game-thread work took at least this many milliseconds get their own report, with every
// sample of the game thread in them (running or waiting) and its callers (0 = off).
CONFIG_DEFINE_HIDDEN("Switch", int32_t, SwitchSlowFrameProfileMs, 0);
// Round 13, GPU profiler: frames whose GPU time reached this many milliseconds get their own part of the pass report
// ("[gpu slow frames]"): the passes (and, with SwitchGpuDrawProfiler, the draw groups) by the time they add in those
// frames against the average frame, their counters, and the slowest frames whole. Turns the pass profiler on (0 = off).
CONFIG_DEFINE_HIDDEN("Switch", int32_t, SwitchGpuSlowFrameMs, 0);

// ---- Round 13 CPU changes. Each is exact; on by default since round 14 (played in round 13).
// With SwitchNativeVisibilityTest: the render walk's visibility test prefetches the two cache lines it will wait for
// two entries ahead (patches/native_hot_patches.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchRenderWalkPrefetch, true);
// The light field's cell sample (decode 8 records, blend them) as native code with one flush-to-zero section instead of
// two FPCR writes per helper call; the last blend stays the recompiled one (patches/native_hot_patches.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeLightField, true);
// A buffer or texture lock waits only until the render thread has copied that object's earlier unlocks, instead of for
// the whole tail of the previous Present (gpu/video.cpp, WaitForPendingUnlocks).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPerResourceLockWait, true);
// Vibration: a stop of the motors is sent once, not again every frame until something else was sent (an IPC each);
// any speed other than 0 is still sent every time, and new vibration devices get a stop again (hid/driver/switch_hid.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVibrationDedupe, true);

// ---- Round 14 CPU changes. Each is exact; on by default since round 15 (played in round 14, first-cutscene audio check
// passed).
// An event set or semaphore release wakes the KeWaitForMultipleObjects callers only when one of them waits on that
// object, instead of every such caller for every signal (kernel/imports.cpp). Guest synchronisation: needs the
// first-cutscene audio check.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTargetedDispatcherWakeups, true);
// A semaphore release of one unit wakes one of its waiters, not all of them (kernel/imports.cpp): the game's worker
// threads wait on shared semaphores and each job woke every one of them. Guest synchronisation: needs the
// first-cutscene audio check.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSemaphoreWakeOne, true);
// RtlEnterCriticalSection with a strong compare-and-swap: a weak one could fail on a free critical section (another write
// to its cache line) and the thread then waited up to 1 ms for an owner that did not exist (kernel/imports.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchStrongCriticalSectionCas, true);
// RtlEnterCriticalSection watches an owned critical section for ~2 µs before it waits in the kernel (kernel/imports.cpp).
// Both are guest synchronisation: they need the first-cutscene audio check.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCriticalSectionSpin, true);

// ---- Round 15 CPU changes. Each is exact; on by default since round 15 was played (2026-10-02: faster in the stages, round 15 on vs off).
// The SwitchVerifyNativeHotFunctions / SwitchVerifyNativeAudio modes and SwitchVerifyEvery cover the natives.
// CRI ADX decoder (8316AFE0) with a shorter per-sample dependency chain and stereo channels interleaved
// (patches/audio_dsp_patches.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeAdxDecoder, true);
// CRI resampler inner loop (8315AC20) in single precision with its counters in registers (tools/switch-localize.py,
// part of the resampler's copy: needs SwitchNativeVoiceKernels).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFastAudioResampler, true);
// The CRI mixer's reverb block loop (83146300) localized with its per-sample step inlined
// (patches/audio_dsp_patches.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLocalizedCriHelpers, true);
// The engine's small-object pool allocator (82E02BD8 pop, 82E02CF8 push) without its virtual lock calls
// (patches/native_r15_pool.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativePoolAllocator, true);
// Path following (822D22C8 and its twelve helpers) as one copy of the recompiled code with the registers and the
// flush mode in locals (patches/native_r15_localized.cpp; needs a SWITCH_EXPLICIT_FMA build).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativePathFollowing, true);
// Material (UV) animation keyframe search and lerp (82E46FA0) (patches/native_r15_material.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeMaterialAnimation, true);
// Havok spline-compressed animation sampling (82FC4390 and its six helpers) as one copy of the recompiled code with
// the registers and the flush mode in locals (patches/native_r15_localized.cpp; needs a SWITCH_EXPLICIT_FMA build).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeSplineAnimation, true);
// Havok MOPP virtual machines (long ray 82F78FB0, query 82F78148) as copies of the recompiled code with the registers
// and the flush mode in locals (patches/native_r15_localized.cpp; needs a SWITCH_EXPLICIT_FMA build).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchNativeMoppVm, true);
// The scene-graph update walk (its node and object loops) prefetches the next child node while the current one is
// processed (mid-asm hooks in patches/native_r15.cpp; a prefetch changes no value).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSceneGraphPrefetch, true);
// The UI modifier cache with 4,096 two-way entries instead of 512 direct-mapped ones (patches/aspect_ratio_patches.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLargeModifierCache, true);

// ---- Round 15 GPU changes. Each is exact; on by default since round 15 was played (2026-10-02: 39.7 vs 37.7 FPS, same route).
// Resolve copies that nothing reads before they are fully rewritten draw nothing: each copy's vertex shader reads a word
// the render thread writes just before the frame is submitted, from what the rest of the frame did with the texture
// (no look-ahead into the render queue, no wait; gpu/video.cpp, shader/copy_conditional_vs.hlsl).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSubmitTimeCopies, true);
// A pending depth resolve waits (SwitchLazyResolves) for draws that only test the depth buffer while its texture is bound
// in slots their shaders cannot sample (the translator's sampler masks): the buffer is attached read-only, where those
// slots can still sample it later (gpu/video.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSampledSlotResolves, true);
// From MarathonRecomp-NX: a draw that only tests its depth buffer while sampling one of its pending resolves attaches it
// read-only and samples it, instead of copying it first (gpu/video.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchReadOnlyDepthSampling, true);
// From MarathonRecomp-NX: with the uniform-buffer constants (SwitchConstantsUBO), a resolve copy no longer forces the next
// draw to upload its vertex constants again; their pointer is pushed again when needed (gpu/video.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCopyKeepsVertexConstants, true);
// From MarathonRecomp-NX: the frame's two timestamp queries reset one at a time on the 3D engine instead of a copy-engine
// fill at the start of every frame (gpu/video.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchQueryResetPerQuery, true);
// The late pass's depth restore (a copy of the main depth into a second buffer the late draws only test against) is not
// drawn: the second buffer stands for the main one, attached read-only, until the two would differ, when it gets the
// copy (decided at submit when the main depth changes; gpu/video.cpp).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchDepthRestoreAlias, true);
// The UI's modifier lookups that miss the cache find the path in a hash index instead of walking the ordered map of
// every loaded path (patches/aspect_ratio_patches.cpp, with SwitchModifierCache). Same answers.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchModifierIndex, true);
