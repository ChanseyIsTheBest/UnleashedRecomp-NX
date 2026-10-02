// The CRI sound mixer's hot kernels with their registers in locals (SwitchNativeReverb, SwitchNativeMixKernels, SwitchNativeVoiceKernels).
#include <stdafx.h>

#if defined(__SWITCH__)

#include <algorithm>
#include <bit>
#include <csetjmp>
#include <utility>
#include <kernel/memory.h>
#include <user/config.h>
#include "audio_dsp_patches.h"
#include "native_verify.h"
#include "verify_sampling.h"

// [Switch] Round 11. The game's CRI sound mixer renders each 256-sample block while it holds the lock the game's main
// thread takes to talk to it; in the hub the main thread waited 0.44 ms a frame for it. Most of the mixer's time is in
// a few per-sample kernels: the reverb's diffusion and comb filters, the buffer fill and mix, the ADPCM decoder, the
// IIR filter and the resampler. Their recompiled loops write the context's registers back to memory and read them
// again on every sample (the loop barrier), which is most of what they do.
//
// These hooks run the recompiled code itself with the registers in locals (UnleashedRecompLib/switch/
// localized_audio.inl, made by tools/switch-localize.py): the same statements in the same order, so the same guest
// loads and stores and the same single-precision arithmetic; the context written back before any call out of the
// copied code and at the end. SwitchVerifyNativeAudio runs the copy with its guest stores logged, undoes them, runs
// the recompiled function and compares the stores' final values and the registers (a call out of the copied code
// stops the copy there; that call is then not compared).
//
// Round 15: the copies can hold hand-written replacements of a loop (switch-localize.py's LOOP_REPLACEMENTS), chosen
// by the flags defined here: SwitchFastAudioResampler, the resampler's inner loop in single precision (exact; see the
// script). It is part of the resampler's copy, so it needs SwitchNativeVoiceKernels, and the verify mode covers it.

namespace
{
    bool g_fastAudioResampler = false;
}

#define LOCAL_FAST_RESAMPLER g_fastAudioResampler

namespace localized
{
#define LOCAL_BEFORE_CALL() ((void)0)
#include <switch/localized_audio.inl>
#undef LOCAL_BEFORE_CALL
}

namespace
{
    struct StoreRecord
    {
        size_t address;
        uint32_t size;
        uint64_t previous;
    };

    thread_local std::vector<StoreRecord>* t_storeLog = nullptr;
    thread_local std::jmp_buf* t_leaveCopy = nullptr;

    template<typename T>
    void LoggedStore(uint8_t* base, size_t address, T value)
    {
        StoreRecord record{ address, sizeof(T), 0 };
        memcpy(&record.previous, base + address, sizeof(T));
        t_storeLog->push_back(record);
        memcpy(base + address, &value, sizeof(T));
    }

    [[noreturn]] void LeaveCopy()
    {
        std::longjmp(*t_leaveCopy, 1);
    }
}

// Round 15: the register + displacement forms (SWITCH_WIDE_DFORM, since round 11) are logged too; before, the copy's
// stores through them were neither compared nor undone.
namespace logged
{
#pragma push_macro("PPC_STORE_U8")
#pragma push_macro("PPC_STORE_U16")
#pragma push_macro("PPC_STORE_U32")
#pragma push_macro("PPC_STORE_U64")
#pragma push_macro("PPC_STORE_U8_D")
#pragma push_macro("PPC_STORE_U16_D")
#pragma push_macro("PPC_STORE_U32_D")
#pragma push_macro("PPC_STORE_U64_D")
#undef PPC_STORE_U8
#undef PPC_STORE_U16
#undef PPC_STORE_U32
#undef PPC_STORE_U64
#undef PPC_STORE_U8_D
#undef PPC_STORE_U16_D
#undef PPC_STORE_U32_D
#undef PPC_STORE_U64_D
#define PPC_STORE_U8(x, y) LoggedStore<uint8_t>(base, (x), uint8_t(y))
#define PPC_STORE_U16(x, y) LoggedStore<uint16_t>(base, (x), __builtin_bswap16(y))
#define PPC_STORE_U32(x, y) LoggedStore<uint32_t>(base, (x), __builtin_bswap32(y))
#define PPC_STORE_U64(x, y) LoggedStore<uint64_t>(base, (x), __builtin_bswap64(y))
#define PPC_STORE_U8_D(r, d, y) LoggedStore<uint8_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), uint8_t(y))
#define PPC_STORE_U16_D(r, d, y) LoggedStore<uint16_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), __builtin_bswap16(y))
#define PPC_STORE_U32_D(r, d, y) LoggedStore<uint32_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), __builtin_bswap32(y))
#define PPC_STORE_U64_D(r, d, y) LoggedStore<uint64_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), __builtin_bswap64(y))
#define LOCAL_BEFORE_CALL() LeaveCopy()
#include <switch/localized_audio.inl>
#undef LOCAL_BEFORE_CALL
#pragma pop_macro("PPC_STORE_U8")
#pragma pop_macro("PPC_STORE_U16")
#pragma pop_macro("PPC_STORE_U32")
#pragma pop_macro("PPC_STORE_U64")
#pragma pop_macro("PPC_STORE_U8_D")
#pragma pop_macro("PPC_STORE_U16_D")
#pragma pop_macro("PPC_STORE_U32_D")
#pragma pop_macro("PPC_STORE_U64_D")
}

namespace
{
    bool g_nativeReverb = false;
    bool g_nativeMixKernels = false;
    bool g_nativeVoiceKernels = false;
    bool g_localizedCriHelpers = false;
    bool g_verifyAudio = false;

    std::atomic<uint64_t> g_verifiedCalls;
    std::atomic<uint64_t> g_leftCopy;
    std::atomic<uint32_t> g_mismatches;

    uint64_t ReadFpcr()
    {
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        return fpcr;
    }

    void WriteFpcr(uint64_t fpcr)
    {
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
    }

    using LocalizedFunction = void (*)(localized::LocalRegs&, PPCContext&, uint8_t*);
    using LoggedFunction = void (*)(logged::LocalRegs&, PPCContext&, uint8_t*);

    template<LoggedFunction Logged>
    void Verify(const char* name, PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        std::vector<StoreRecord> log;
        std::jmp_buf leave;
        PPCContext copy = ctx;
        const uint64_t fpcr = ReadFpcr();
        bool left = false;

        t_storeLog = &log;
        t_leaveCopy = &leave;
        if (setjmp(leave) == 0)
        {
            logged::LocalRegs regs;
            regs.Load(copy);
            Logged(regs, copy, base);
            regs.Store(copy);
        }
        else
        {
            left = true;
        }
        t_storeLog = nullptr;
        t_leaveCopy = nullptr;

        // The copy's results, then guest memory as it was, the FPCR included (the recompiled code trusts its FPSCR).
        std::vector<uint64_t> results(log.size());
        for (size_t i = 0; i < log.size(); i++)
            memcpy(&results[i], base + log[i].address, log[i].size);
        for (size_t i = log.size(); i-- > 0;)
            memcpy(base + log[i].address, &log[i].previous, log[i].size);
        WriteFpcr(fpcr);

        original(ctx, base);

        if (left)
        {
            g_leftCopy.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        bool same = ctx.fpscr.csr == copy.fpscr.csr;
        for (size_t i = 0; i < log.size(); i++)
        {
            uint64_t value = 0, result = results[i];
            memcpy(&value, base + log[i].address, log[i].size);
            if (log[i].size < 8)
                result &= (uint64_t(1) << (log[i].size * 8)) - 1;
            same &= value == result;
        }

        localized::LocalRegs a, b;
        a.Load(ctx);
        b.Load(copy);
        same &= memcmp(&a, &b, sizeof(a)) == 0;

        const uint64_t calls = g_verifiedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!same)
        {
            const uint32_t mismatches = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
            if (mismatches <= 32 || (mismatches & 1023) == 0)
                fprintf(stderr, "[audio dsp] MISMATCH %u in %s\n", mismatches, name);
        }

        if ((calls & (calls - 1)) == 0 && calls >= 1024)
        {
            fprintf(stderr, "[audio dsp] verified %llu calls (%llu left the copied code and were not compared), %u mismatches\n",
                (unsigned long long)calls, (unsigned long long)g_leftCopy.load(std::memory_order_relaxed),
                g_mismatches.load(std::memory_order_relaxed));
        }
    }

    template<LocalizedFunction Localized, LoggedFunction Logged>
    void Run(const char* name, bool enabled, PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        if (!enabled)
        {
            original(ctx, base);
            return;
        }

        if (g_verifyAudio && VerifyThisCall())
        {
            Verify<Logged>(name, ctx, base, original);
            return;
        }

        localized::LocalRegs regs;
        regs.Load(ctx);
        Localized(regs, ctx, base);
        regs.Store(ctx);
    }
}

// [Switch] Round 15, SwitchNativeAdxDecoder: the CRI ADX decoder (the hook below) as native code. In the round 14
// stage profile it was the largest single part of the sound server's work, which the server does while it holds the
// lock the game's main thread waits for. Per frame and channel it reads an 18-byte block (a big-endian header whose
// top bit ends the stream, then 32 four-bit codes), computes a scale from the header and a running key, updates the
// key, and writes 32 samples s[k] = c0 * s[k-1] + c1 * s[k-2] + code * scale as floats, keeping the last two (plus a
// small offset) as the channel's history.
//
// The guest computes each sample as plain = round_s(a * ca) (fmuls), inner = round_s(code * scale + plain) (fmadds),
// s = round_s(b * cb + inner) (fmadds), where {a, b} are s[k-1] (with c0) and s[k-2] (with c1) in an order its compiler
// chose per position (kAdxPlainProducts). This computes the same values with a shorter chain:
// - plain: one single-precision multiply; the guest's double product is exact (24 x 16 bits), so its one rounding
//   to single is the same;
// - inner: code * scale is exact in single precision (a code of -8..7 times an integer of 1..8192 times 2^-15), so the
//   guest's value is round_s(round_d(p + plain)) for two singles, which is round_s(p + plain) (double rounding is
//   innocuous for the sum of two binary32 numbers in binary64, and in the directed modes);
// - outer: the guest's own operation, a double fused multiply-add of the same operands (the addend first, so the
//   same NaN when one comes in), rounded to single.
// Everything runs with the FPCR the recompiled code uses here (scalar mode, no flush). Two channels of a frame are
// independent chains and are decoded interleaved; the key, the history and the stores are the guest's, in an order
// nothing can observe: the outputs, the input, the decoder state and the pointer table must not overlap (otherwise
// the recompiled function runs). Every register the recompiled function leaves is set as it leaves it (worked out
// from its listing).

PPC_FUNC_IMPL(__imp__sub_8316AFE0);

namespace
{
    bool g_nativeAdxDecoder = false;

    // The guest data the decoder reads, written with a digit separator (tools/switch-direct-calls.py takes every
    // eight-digit 82/83 token in these sources for a hooked function): the 16 code values (0..7, -8..-1) with the
    // history offset after them, 2^-15 and 2^-12. InitAudioDsp checks them.
    constexpr uint32_t kAdxCodeValues = 0x8219'7D18;
    constexpr uint32_t kAdxCodeValuesSize = 0x44;
    constexpr uint32_t kAdxHistoryOffset = 0x8219'7D58;
    constexpr uint32_t kAdxScaleUnit = 0x820C'8530;
    constexpr uint32_t kAdxCoefficientUnit = 0x820D'F148;

    // Per position, the product the guest rounds on its own: L = the newest sample times c0 (the fused one is then the
    // sample before times c1), S = the sample before times c1 (the fused one the newest times c0).
    constexpr char kAdxPlainProducts[] = "SLSLLLLLLLLLLLSLSLLLLSLLLLLLSLLL";
    static_assert(sizeof(kAdxPlainProducts) == 33 && kAdxPlainProducts[31] == 'L',
        "the register state the decoder leaves is worked out for an L at position 31");

    constexpr uint32_t kAdxMaxChannels = 8;

    struct AdxChannel
    {
        uint32_t words[4]; // the 32 codes, the first sample's in the top bits of words[0]
        float scale;
        float s1;          // the newest sample
        float s2;          // the one before
        uint8_t* out;      // the frame's 32 samples
        float s29, plain31, inner31, code31; // for the registers the guest leaves
    };

    // The plain product, rounded on its own: the empty asm keeps GCC from fusing it into the add that follows.
    inline float AdxPlainProduct(float a, float b)
    {
        float product = a * b;
        __asm__("" : "+w"(product));
        return product;
    }

    template<int K>
    __attribute__((always_inline)) inline void AdxSample(AdxChannel& c, const float* codeValues, float c0, float c1,
        double c0d, double c1d)
    {
        const float code = codeValues[(c.words[K / 8] >> (28 - 4 * (K % 8))) & 15];
        const float scaled = code * c.scale;
        float s;
        if constexpr (kAdxPlainProducts[K] == 'L')
        {
            const float plain = AdxPlainProduct(c.s1, c0);
            const float inner = scaled + plain;
            s = float(__builtin_fma(double(c.s2), c1d, double(inner)));
            if constexpr (K == 31)
            {
                c.plain31 = plain;
                c.inner31 = inner;
                c.code31 = code;
            }
        }
        else
        {
            const float plain = AdxPlainProduct(c.s2, c1);
            const float inner = scaled + plain;
            s = float(__builtin_fma(double(c.s1), c0d, double(inner)));
        }

        if constexpr (K == 29)
            c.s29 = s;

        const uint32_t bits = __builtin_bswap32(std::bit_cast<uint32_t>(s));
        memcpy(c.out + 4 * K, &bits, sizeof(bits));
        c.s2 = c.s1;
        c.s1 = s;
    }

    template<int... K>
    void AdxDecodeOne(std::integer_sequence<int, K...>, AdxChannel& a, const float* codeValues, float c0, float c1,
        double c0d, double c1d)
    {
        (AdxSample<K>(a, codeValues, c0, c1, c0d, c1d), ...);
    }

    template<int... K>
    void AdxDecodeTwo(std::integer_sequence<int, K...>, AdxChannel& a, AdxChannel& b, const float* codeValues, float c0,
        float c1, double c0d, double c1d)
    {
        ((AdxSample<K>(a, codeValues, c0, c1, c0d, c1d), AdxSample<K>(b, codeValues, c0, c1, c0d, c1d)), ...);
    }

    // The frames the decoder decodes, for a valid channel count: the smallest of the whole frames in r6 bytes, r10 / 32
    // and (r4 + 31) / 32, as the guest computes them (unsigned, 27-bit).
    uint32_t AdxFrames(const PPCContext& ctx, uint32_t channels)
    {
        uint32_t frames = ctx.r6.u32 / (channels * 18);
        frames = std::min(frames, (ctx.r10.u32 >> 5) & 0x7FFFFFF);
        frames = std::min(frames, (uint32_t(ctx.r4.u32 + 31) >> 5) & 0x7FFFFFF);
        return frames;
    }

    bool AdxDisjoint(uint64_t a, uint64_t aSize, uint64_t b, uint64_t bSize)
    {
        return aSize == 0 || bSize == 0 || a + aSize <= b || b + bSize <= a;
    }

    float AdxLoadFloat(uint8_t* base, uint32_t address)
    {
        return std::bit_cast<float>(PPC_LOAD_U32(address));
    }
}

static void NativeAdxDecode(PPCContext& ctx, uint8_t* base)
{
    const uint32_t state = ctx.r3.u32;
    const uint32_t channels = PPC_LOAD_U8(state);
    if (ctx.r8.u32 != channels || channels == 0 || channels > kAdxMaxChannels)
    {
        // A channel count that is not the stream's (the guest reports an error), none, or more than handled here.
        __imp__sub_8316AFE0(ctx, base);
        return;
    }

    const uint32_t frames = AdxFrames(ctx, channels);
    const uint32_t input = ctx.r5.u32;
    const uint32_t pointers = ctx.r9.u32;
    const uint32_t stateSize = 44 + 8 * channels; // up to the last channel's history
    uint32_t outputs[kAdxMaxChannels] = {};
    if (frames != 0)
    {
        const uint64_t inputSize = uint64_t(channels) * 18 * frames;
        const uint64_t outputSize = uint64_t(frames) * 128;
        bool separate = uint64_t(input) + inputSize <= 0x1'0000'0000ull && uint64_t(state) + stateSize <= 0x1'0000'0000ull &&
            AdxDisjoint(input, inputSize, state, stateSize) && AdxDisjoint(pointers, 4 * channels, state, stateSize) &&
            AdxDisjoint(ctx.r7.u32, 4, state, stateSize);
        for (uint32_t c = 0; c < channels && separate; c++)
        {
            outputs[c] = PPC_LOAD_U32(pointers + 4 * c);
            separate = uint64_t(outputs[c]) + outputSize <= 0x1'0000'0000ull &&
                AdxDisjoint(outputs[c], outputSize, input, inputSize) &&
                AdxDisjoint(outputs[c], outputSize, state, stateSize) &&
                AdxDisjoint(outputs[c], outputSize, pointers, 4 * channels) &&
                AdxDisjoint(outputs[c], outputSize, ctx.r7.u32, 4) &&
                AdxDisjoint(outputs[c], outputSize, kAdxCodeValues, kAdxCodeValuesSize);
            for (uint32_t d = 0; d < c && separate; d++)
                separate = AdxDisjoint(outputs[c], outputSize, outputs[d], outputSize);
        }

        if (!separate)
        {
            __imp__sub_8316AFE0(ctx, base);
            return;
        }
    }

    // The coefficients, as the guest converts them (its stack slot is below the caller's stack pointer once it
    // returns, so it is not written here).
    ctx.fpscr.disableFlushMode();
    const int64_t c0Int = int16_t(PPC_LOAD_U16(state + 8));
    const int64_t c1Int = int16_t(PPC_LOAD_U16(state + 10));
    const float coefficientUnit = AdxLoadFloat(base, kAdxCoefficientUnit);
    const float c0Single = float(double(c0Int));
    const float c1Single = float(double(c1Int));
    const float c0 = c0Single * coefficientUnit;
    const float c1 = c1Single * coefficientUnit;

    const uint64_t r3In = ctx.r3.u64;
    const uint64_t r5In = ctx.r5.u64;
    const uint64_t r9In = ctx.r9.u64;
    uint32_t doneFrames = 0;

    ctx.f8.f64 = double(c0Single);
    ctx.f9.f64 = double(c1Single);
    ctx.f10.f64 = double(c0Int);
    ctx.f13.f64 = double(c1);

    if (frames == 0)
    {
        ctx.f11.f64 = double(c1Int);
        ctx.f12.u64 = uint64_t(c1Int);
        ctx.r8.s64 = c1Int;
    }
    else
    {
        const float scaleUnit = AdxLoadFloat(base, kAdxScaleUnit);
        const float historyOffset = AdxLoadFloat(base, kAdxHistoryOffset);
        const double c0d = double(c0);
        const double c1d = double(c1);
        float codeValues[16];
        for (uint32_t i = 0; i < 16; i++)
            codeValues[i] = AdxLoadFloat(base, kAdxCodeValues + 4 * i);

        const int32_t multiplier = int16_t(PPC_LOAD_U16(state + 4));
        const int32_t increment = int16_t(PPC_LOAD_U16(state + 6));
        uint32_t key = PPC_LOAD_U16(state + 2);
        bool keyChanged = false;

        float history1[kAdxMaxChannels], history2[kAdxMaxChannels];
        bool historyChanged[kAdxMaxChannels] = {};
        for (uint32_t c = 0; c < channels; c++)
        {
            history1[c] = AdxLoadFloat(base, state + 44 + 8 * c);
            history2[c] = AdxLoadFloat(base, state + 48 + 8 * c);
        }

        AdxChannel last{};
        bool anyBlock = false;
        uint32_t stopChannel = channels;

        auto header = [&](uint32_t frame, uint32_t c) -> uint32_t
            {
                const uint32_t block = input + (frame * channels + c) * 18;
                return (uint32_t(PPC_LOAD_U8(block)) << 8) | PPC_LOAD_U8(block + 1);
            };

        auto begin = [&](uint32_t frame, uint32_t c, uint32_t value, AdxChannel& channel)
            {
                const uint32_t block = input + (frame * channels + c) * 18;
                channel.scale = float(double(int64_t(((key ^ value) & 0x1FFF) + 1))) * scaleUnit;
                key = uint32_t(multiplier * int32_t(int16_t(key)) + increment) & 0x7FFF;
                keyChanged = true;
                for (uint32_t w = 0; w < 4; w++)
                    channel.words[w] = PPC_LOAD_U32(block + 2 + 4 * w);
                channel.s1 = history1[c];
                channel.s2 = history2[c];
                channel.out = base + outputs[c] + frame * 128;
            };

        auto finish = [&](uint32_t c, const AdxChannel& channel)
            {
                history1[c] = channel.s1 + historyOffset;
                history2[c] = channel.s2 + historyOffset;
                historyChanged[c] = true;
                last = channel;
                anyBlock = true;
            };

        for (uint32_t frame = 0; frame < frames && stopChannel == channels; frame++)
        {
            uint32_t c = 0;
            while (c < channels)
            {
                const uint32_t value = header(frame, c);
                if ((value & 0x8000) != 0)
                {
                    stopChannel = c;
                    break;
                }

                AdxChannel a;
                begin(frame, c, value, a);

                // The next channel with this one, unless its header ends the stream (the guest decodes this one
                // before it reads that header; the overlap checks make the order invisible).
                const uint32_t next = c + 1 < channels ? header(frame, c + 1) : 0x8000;
                if ((next & 0x8000) == 0)
                {
                    AdxChannel b;
                    begin(frame, c + 1, next, b);
                    AdxDecodeTwo(std::make_integer_sequence<int, 32>{}, a, b, codeValues, c0, c1, c0d, c1d);
                    finish(c, a);
                    finish(c + 1, b);
                    c += 2;
                }
                else
                {
                    AdxDecodeOne(std::make_integer_sequence<int, 32>{}, a, codeValues, c0, c1, c0d, c1d);
                    finish(c, a);
                    c += 1;
                }
            }

            doneFrames = stopChannel == channels ? frame + 1 : frame;
        }

        for (uint32_t c = 0; c < channels; c++)
        {
            if (historyChanged[c])
            {
                PPC_STORE_U32(state + 44 + 8 * c, std::bit_cast<uint32_t>(history1[c]));
                PPC_STORE_U32(state + 48 + 8 * c, std::bit_cast<uint32_t>(history2[c]));
            }
        }

        if (keyChanged)
            PPC_STORE_U16(state + 2, uint16_t(key));

        ctx.f11.f64 = double(scaleUnit);
        ctx.f12.f64 = double(historyOffset);
        if (anyBlock)
        {
            // After a frame (position 31 is an L): f1 = the plain product, f2 = the code value, f3 = the scale, f4 and
            // f7 = s30, f5 = s29, f6 and f8 = the new history, f9 = s31, f10 = the inner sum.
            ctx.f1.f64 = double(last.plain31);
            ctx.f2.f64 = double(last.code31);
            ctx.f3.f64 = double(last.scale);
            ctx.f4.f64 = double(last.s2);
            ctx.f5.f64 = double(last.s29);
            ctx.f6.f64 = double(float(last.s2 + historyOffset));
            ctx.f7.f64 = double(last.s2);
            ctx.f8.f64 = double(float(last.s1 + historyOffset));
            ctx.f9.f64 = double(last.s1);
            ctx.f10.f64 = double(last.inner31);
        }

        if (stopChannel == channels)
        {
            // Every frame decoded: the pointers after the last channel of the last frame.
            ctx.r4.u64 = r9In + 4 * channels;
            ctx.r5.u64 = r5In + uint64_t(18) * channels * frames;
            ctx.r6.u64 = r3In + 48 + 8 * channels;
            ctx.r8.u64 = ctx.r5.u64 - 16;
        }
        else
        {
            // The header of channel stopChannel in frame doneFrames ended the stream.
            ctx.r4.u64 = r9In + 4 * stopChannel;
            ctx.r5.u64 = r5In + uint64_t(18) * (uint64_t(channels) * doneFrames + stopChannel);
            ctx.r6.u64 = r3In + 48 + 8 * stopChannel;
            ctx.r8.u64 = 0x8000;
        }
    }

    // The bytes consumed, stored at r7, and the frames decoded times 32 in r3.
    const int64_t blocks = int64_t(int32_t(channels)) * int64_t(int32_t(doneFrames));
    ctx.r10.u64 = uint64_t(blocks) + (uint64_t(uint32_t(blocks) << 3) & 0xFFFFFFF8);
    ctx.r9.u64 = uint64_t(uint32_t(ctx.r10.u32 << 1) & 0xFFFFFFFE);
    PPC_STORE_U32(ctx.r7.u32, ctx.r9.u32);
    ctx.r3.u64 = uint64_t(uint32_t(doneFrames << 5) & 0xFFFFFFE0);
}

// SwitchVerifyNativeAudio: the decoder's guest writes are its state (the key and the history), each channel's output
// for the frames it can decode, and the consumed size.
static void VerifyAdxDecode(PPCContext& ctx, uint8_t* base)
{
    const uint32_t state = ctx.r3.u32;
    const uint32_t channels = PPC_LOAD_U8(state);
    if (ctx.r8.u32 != channels || channels == 0 || channels > kAdxMaxChannels)
    {
        __imp__sub_8316AFE0(ctx, base);
        return;
    }

    const uint32_t frames = AdxFrames(ctx, channels);
    std::vector<native_verify::Region> regions;
    regions.push_back({ state, 44 + 8 * channels });
    regions.push_back({ ctx.r7.u32, 4 });
    for (uint32_t c = 0; c < channels && frames != 0; c++)
        regions.push_back({ PPC_LOAD_U32(ctx.r9.u32 + 4 * c), frames * 128 });

    native_verify::Verify("ADX decoder", ctx, base, __imp__sub_8316AFE0, NativeAdxDecode, regions);
}

// SwitchNativeReverb: the reverb's per-sample step (six diffusion stages and nine comb filters).
PPC_FUNC_IMPL(__imp__sub_83154100);
PPC_FUNC(sub_83154100)
{
    Run<localized::Local_83154100, logged::Local_83154100>("reverb", g_nativeReverb, ctx, base, __imp__sub_83154100);
}

// SwitchLocalizedCriHelpers (round 15): the reverb's block loop, with the per-sample step above inlined into it (its
// copy, so independent of SwitchNativeReverb): the registers are loaded and stored once per block, not per sample.
// (The verify mode compares a call only when the block loop's first, conditional virtual call is not taken.)
PPC_FUNC_IMPL(__imp__sub_83146300);
PPC_FUNC(sub_83146300)
{
    Run<localized::Local_83146300, logged::Local_83146300>("reverb block", g_localizedCriHelpers, ctx, base, __imp__sub_83146300);
}

// SwitchNativeMixKernels: clearing a voice's buffers, and mixing one buffer into another with a gain.
PPC_FUNC_IMPL(__imp__sub_83144DC0);
PPC_FUNC(sub_83144DC0)
{
    Run<localized::Local_83144DC0, logged::Local_83144DC0>("buffer fill", g_nativeMixKernels, ctx, base, __imp__sub_83144DC0);
}

PPC_FUNC_IMPL(__imp__sub_83151FD0);
PPC_FUNC(sub_83151FD0)
{
    Run<localized::Local_83151FD0, logged::Local_83151FD0>("buffer mix", g_nativeMixKernels, ctx, base, __imp__sub_83151FD0);
}

// SwitchNativeVoiceKernels: the ADPCM decoder, the IIR filter and the resampler; SwitchNativeAdxDecoder: the decoder
// as native code (above).
PPC_FUNC(sub_8316AFE0)
{
    if (g_nativeAdxDecoder)
    {
        if (g_verifyAudio && VerifyThisCall())
            VerifyAdxDecode(ctx, base);
        else
            NativeAdxDecode(ctx, base);
        return;
    }

    Run<localized::Local_8316AFE0, logged::Local_8316AFE0>("ADPCM decoder", g_nativeVoiceKernels, ctx, base, __imp__sub_8316AFE0);
}

PPC_FUNC_IMPL(__imp__sub_8315B168);
PPC_FUNC(sub_8315B168)
{
    Run<localized::Local_8315B168, logged::Local_8315B168>("IIR filter", g_nativeVoiceKernels, ctx, base, __imp__sub_8315B168);
}

PPC_FUNC_IMPL(__imp__sub_8315AC20);
PPC_FUNC(sub_8315AC20)
{
    Run<localized::Local_8315AC20, logged::Local_8315AC20>("resampler", g_nativeVoiceKernels, ctx, base, __imp__sub_8315AC20);
}

void InitAudioDsp()
{
    g_nativeReverb = Config::SwitchNativeReverb;
    g_nativeMixKernels = Config::SwitchNativeMixKernels;
    g_nativeVoiceKernels = Config::SwitchNativeVoiceKernels;
    g_verifyAudio = Config::SwitchVerifyNativeAudio;
    g_fastAudioResampler = Config::SwitchFastAudioResampler && g_nativeVoiceKernels;
    g_localizedCriHelpers = Config::SwitchLocalizedCriHelpers;
    if (g_nativeReverb || g_nativeMixKernels || g_nativeVoiceKernels || g_localizedCriHelpers)
    {
        fprintf(stderr, "[audio dsp] reverb %s, mix kernels %s, voice kernels %s with registers in locals%s%s%s\n",
            g_nativeReverb ? "on" : "off", g_nativeMixKernels ? "on" : "off", g_nativeVoiceKernels ? "on" : "off",
            g_fastAudioResampler ? ", single-precision resampler loop" : "",
            g_localizedCriHelpers ? ", reverb block loop with the step inlined" : "",
            g_verifyAudio ? " (verifying against the recompiled code)" : "");
    }

    // The decoder's code, its data and the two constants it was written for.
    uint8_t* const base = g_memory.base;
    g_nativeAdxDecoder = Config::SwitchNativeAdxDecoder &&
        native_verify::CodeMatches(base, 0x8316AFE0, 0x568, 0x0937E4BE8245EFCFull) &&
        native_verify::CodeMatches(base, kAdxCodeValues, kAdxCodeValuesSize, 0xDAB1BC364F82667Bull) &&
        PPC_LOAD_U32(kAdxScaleUnit) == 0x38000000 && PPC_LOAD_U32(kAdxCoefficientUnit) == 0x39800000;
    if (g_nativeAdxDecoder)
    {
        fprintf(stderr, "[audio dsp] native ADX decoder%s\n",
            g_verifyAudio ? " (verifying against the recompiled code)" : "");
    }
}

#endif
