#pragma once

// [Switch] SwitchNativeReverb, SwitchNativeMixKernels, SwitchNativeVoiceKernels, SwitchVerifyNativeAudio
// (patches/audio_dsp_patches.cpp): before guest code runs.
void InitAudioDsp();
