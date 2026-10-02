// Player/object message dispatch as a native table lookup (SwitchNativeMessageDispatch).
#include <stdafx.h>

#if defined(__SWITCH__)

#include <user/config.h>
#include <string_view>
#include <unordered_set>
#include "message_dispatch.h"
#include "verify_sampling.h"

// [Switch] SwitchNativeMessageDispatch, SwitchVerifyMessageDispatch, SwitchExactTypeInfoSet (set up by
// InitMessageDispatch in main(), after the image is loaded and before guest code runs; they need SwitchNativeRtti).
//
// The game's ProcessMessage overrides test the message's type against a list, one type at a time
// (tools/switch-message-dispatch.py shows the pattern): typeid(*msg) == typeid(MsgX) for up to 100 message types,
// calling the handler of the first that matches, else the base class's ProcessMessage, which does the same with
// its own list. The player's chain of four makes up to 200 type_info comparisons per message; that was 5% of the
// game thread in the hub.
//
// With native RTTI (misc_impl.cpp), __RTtypeid and type_info== change nothing but r3, so between two comparisons
// nothing changes and typeid(*msg) is the same each time; for two of the image's type descriptors, whose names are
// all different, type_info== is true exactly when they are the same descriptor. So the first entry of the list
// whose descriptor is typeid(*msg) is the one the recompiled code finds, and a table keyed by the descriptor finds
// it directly. The hook then does what the recompiled code does from there: the same frame (the back-chain word
// the stwu writes), the handler with the same r3 and r4, the same return value (the handler's r3, or 1), or the
// base class with the same r3, r4 and r5. Anything else (a null message, a type descriptor outside the image,
// a function whose code is not exactly the pattern) runs the recompiled function.

ImageTypeDescriptors g_imageTypeDescriptors;
bool g_exactTypeInfoSet = false;
bool g_verifyMessageDispatch = false;

extern bool g_nativeRtti;

namespace
{
    constexpr uint32_t RtTypeidAddress = 0x831B2438;   // __RTtypeid
    constexpr uint32_t TypeInfoEqualAddress = 0x831B0AB8; // type_info::operator==
    constexpr uint32_t SaveGprLr28Address = 0x831B0B28;  // __savegprlr_28
    constexpr uint32_t RestGprLr28Address = 0x831B0B78;  // __restgprlr_28
    constexpr uint32_t TypeInfoVftable = 0x8219D83C;     // type_info's vftable: the first word of every descriptor

    struct Entry
    {
        uint32_t typeDescriptor;
        uint32_t handler;
        int32_t adjust;  // r3 = this + adjust
        bool returnsOne; // else the handler's r3
    };

    struct List
    {
        std::vector<Entry> entries;   // first occurrence of each type, in the recompiled code's order
        std::vector<uint16_t> slots;  // open addressing by type descriptor: entry index + 1, 0 when empty
        uint32_t mask = 0;

        static uint32_t Hash(uint32_t typeDescriptor)
        {
            return (typeDescriptor >> 2) * 0x9E3779B1u;
        }

        void Build()
        {
            uint32_t size = 4;
            while (size < entries.size() * 2)
                size <<= 1;

            slots.assign(size, 0);
            mask = size - 1;
            for (size_t i = 0; i < entries.size(); i++)
            {
                uint32_t slot = (Hash(entries[i].typeDescriptor) >> 16) & mask;
                while (slots[slot] != 0)
                    slot = (slot + 1) & mask;

                slots[slot] = uint16_t(i + 1);
            }
        }

        const Entry* Find(uint32_t typeDescriptor) const
        {
            for (uint32_t slot = (Hash(typeDescriptor) >> 16) & mask; slots[slot] != 0; slot = (slot + 1) & mask)
            {
                const Entry& entry = entries[slots[slot] - 1];
                if (entry.typeDescriptor == typeDescriptor)
                    return &entry;
            }

            return nullptr;
        }
    };

    uint32_t LoadCode(uint8_t* base, uint32_t address)
    {
        if (address < PPC_CODE_BASE || address - PPC_CODE_BASE >= PPC_CODE_SIZE || (address & 3) != 0)
            return 0;

        uint32_t word;
        memcpy(&word, base + address, sizeof(word));
        return ByteSwap(word);
    }

    // b (link false) or bl (link true) at address: its target.
    bool DecodeBranch(uint32_t word, uint32_t address, bool link, uint32_t& target)
    {
        if ((word & 0xFC000003) != (link ? 0x48000001u : 0x48000000u))
            return false;

        target = address + uint32_t(int32_t((word & 0x03FFFFFC) << 6) >> 6);
        return true;
    }

    // bc with the given BO/BI bits (e.g. 0x41820000, beq): its target.
    bool DecodeConditional(uint32_t word, uint32_t address, uint32_t pattern, uint32_t& target)
    {
        if ((word & 0xFFFF0003) != pattern)
            return false;

        target = address + uint32_t(int32_t(int16_t(word & 0xFFFC)));
        return true;
    }

    constexpr uint32_t MflrR12 = 0x7D8802A6;
    constexpr uint32_t MrR28R5 = 0x7CBC2B78;
    constexpr uint32_t MrR30R3 = 0x7C7E1B78;
    constexpr uint32_t MrR31R4 = 0x7C9F2378;
    constexpr uint32_t ClrlwiDotR29R28 = 0x579D063F; // clrlwi. r29,r28,24
    constexpr uint32_t CmplwiCr6R29 = 0x2B1D0000;    // cmplwi cr6,r29,0
    constexpr uint32_t Beq = 0x41820000;
    constexpr uint32_t BeqCr6 = 0x419A0000;
    constexpr uint32_t BneCr6 = 0x409A0000;
    constexpr uint32_t MrR3R31 = 0x7FE3FB78;
    constexpr uint32_t LisR11 = 0x3D600000;
    constexpr uint32_t MrR4R3 = 0x7C641B78;
    constexpr uint32_t AddiR3R11 = 0x386B0000;
    constexpr uint32_t ClrlwiDotR11R3 = 0x546B063F;  // clrlwi. r11,r3,24
    constexpr uint32_t MrR4R31 = 0x7FE4FB78;
    constexpr uint32_t AddiR3R30 = 0x387E0000;
    constexpr uint32_t LiR3One = 0x38600001;
    constexpr uint32_t MrR5R28 = 0x7F85E378;
    constexpr uint32_t MrR3R30 = 0x7FC3F378;
    constexpr uint32_t AddiR1R1 = 0x38210000;

    struct DispatchProbe
    {
        DispatchProbe* parent;
        uint32_t comparisons;
        uint32_t firstMatch;
        uint32_t firstMatchIndex;
    };

    thread_local DispatchProbe* t_probe = nullptr;

    std::atomic<uint64_t> g_verifiedCalls;
    std::atomic<uint32_t> g_verifyMismatches;
}

class MessageDispatcher
{
public:
    MessageDispatcher(uint32_t address, PPCFunc* original) : m_address(address), m_original(original), m_next(s_first)
    {
        s_first = this;
    }

    void Run(PPCContext& ctx, uint8_t* base)
    {
        if (!m_active)
        {
            m_original(ctx, base);
            return;
        }

        if (g_verifyMessageDispatch && VerifyThisCall())
        {
            RunVerified(ctx, base);
            return;
        }

        const List& list = m_lists[(ctx.r5.u32 & 0xFF) != 0];
        const Entry* entry = nullptr;
        if (!list.entries.empty())
        {
            uint32_t typeDescriptor;
            if (!TypeOfMessage(ctx.r4.u32, base, typeDescriptor))
            {
                m_original(ctx, base);
                return;
            }

            entry = list.Find(typeDescriptor);
        }

        // stwu r1,-frame(r1); the recompiled code keeps r28-r31 in locals, so this and the message are the
        // entry r3 and r4, and the flag word the entry r5.
        const uint64_t self = ctx.r3.u64;
        const uint64_t message = ctx.r4.u64;
        const uint64_t flag = ctx.r5.u64;
        const uint32_t frame = uint32_t(-int32_t(m_frameSize)) + ctx.r1.u32;
        PPC_STORE_U32(frame, ctx.r1.u32);
        ctx.r1.u32 = frame;

        if (entry != nullptr)
        {
            ctx.r4.u64 = message;
            ctx.r3.s64 = int64_t(self) + entry->adjust;
            (PPC_LOOKUP_FUNC(base, entry->handler))(ctx, base);
            if (entry->returnsOne)
                ctx.r3.s64 = 1;
        }
        else
        {
            ctx.r5.u64 = flag;
            ctx.r4.u64 = message;
            ctx.r3.u64 = self;
            (PPC_LOOKUP_FUNC(base, m_baseClass))(ctx, base);
        }

        ctx.r1.s64 = ctx.r1.s64 + m_frameSize;
    }

    static void InitAll(uint8_t* base, bool active, uint32_t& decoded, uint32_t& total, uint32_t& entries)
    {
        for (MessageDispatcher* dispatcher = s_first; dispatcher != nullptr; dispatcher = dispatcher->m_next)
        {
            total++;
            if (dispatcher->Decode(base))
            {
                dispatcher->m_active = active;
                decoded++;
                entries += uint32_t(dispatcher->m_lists[0].entries.size() + dispatcher->m_lists[1].entries.size());
            }
            else
            {
                fprintf(stderr, "[dispatch] %08X does not match the dispatcher pattern; the recompiled code runs.\n",
                    dispatcher->m_address);
            }
        }
    }

private:
    // __RTtypeid(msg) as the native hook computes it; false where it would run the recompiled code (a null
    // object or locator entry) or where the descriptor is not one of the image's.
    static bool TypeOfMessage(uint32_t message, uint8_t* base, uint32_t& typeDescriptor)
    {
        if (message == 0)
            return false;

        const uint32_t locator = PPC_LOAD_U32(PPC_LOAD_U32(message) - 4);
        typeDescriptor = PPC_LOAD_U32(locator + 12);
        return typeDescriptor != 0 && IsImageTypeDescriptor(typeDescriptor);
    }

    // The recompiled code, with type_info== reporting each comparison: the handler it chose must be the table's.
    void RunVerified(PPCContext& ctx, uint8_t* base)
    {
        const List& list = m_lists[(ctx.r5.u32 & 0xFF) != 0];
        const Entry* entry = nullptr;
        bool predicted = list.entries.empty();
        if (!predicted)
        {
            uint32_t typeDescriptor;
            if (TypeOfMessage(ctx.r4.u32, base, typeDescriptor))
            {
                entry = list.Find(typeDescriptor);
                predicted = true;
            }
        }

        DispatchProbe probe{ t_probe, 0, 0, 0 };
        t_probe = &probe;
        m_original(ctx, base);
        t_probe = probe.parent;

        if (!predicted)
            return;

        const uint32_t size = uint32_t(list.entries.size());
        bool same;
        if (entry != nullptr)
            same = probe.firstMatch == entry->typeDescriptor && probe.firstMatchIndex == uint32_t(entry - list.entries.data());
        else
            same = probe.firstMatch == 0 || probe.firstMatchIndex >= size; // a later match is the base class's

        const uint64_t calls = g_verifiedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!same)
        {
            const uint32_t mismatches = g_verifyMismatches.fetch_add(1, std::memory_order_relaxed) + 1;
            if (mismatches <= 32 || (mismatches & 1023) == 0)
            {
                fprintf(stderr, "[dispatch] MISMATCH %u in %08X (flag %u): table %08X at %d, recompiled code %08X at %u of %u\n",
                    mismatches, m_address, (ctx.r5.u32 & 0xFF) != 0, entry ? entry->typeDescriptor : 0,
                    entry ? int(entry - list.entries.data()) : -1, probe.firstMatch, probe.firstMatchIndex, size);
            }
        }

        if ((calls & (calls - 1)) == 0 && calls >= 1024)
        {
            fprintf(stderr, "[dispatch] verified %llu dispatches, %u mismatches\n", (unsigned long long)calls,
                g_verifyMismatches.load(std::memory_order_relaxed));
        }
    }

    bool Decode(uint8_t* base)
    {
        const uint32_t fn = m_address;
        uint32_t target;
        if (LoadCode(base, fn) != MflrR12 || !DecodeBranch(LoadCode(base, fn + 4), fn + 4, true, target) ||
            target != SaveGprLr28Address)
        {
            return false;
        }

        const uint32_t stwu = LoadCode(base, fn + 8);
        if ((stwu & 0xFFFF0000) != 0x94210000 || int16_t(stwu & 0xFFFF) >= 0)
            return false;

        m_frameSize = uint32_t(-int32_t(int16_t(stwu & 0xFFFF)));
        if (LoadCode(base, fn + 12) != MrR28R5 || LoadCode(base, fn + 16) != MrR30R3 || LoadCode(base, fn + 20) != MrR31R4 ||
            LoadCode(base, fn + 24) != ClrlwiDotR29R28)
        {
            return false;
        }

        uint32_t flagZero;
        if (!DecodeConditional(LoadCode(base, fn + 28), fn + 28, Beq, flagZero))
            return false;

        auto isEpilogue = [&](uint32_t address)
            {
                uint32_t restore;
                return LoadCode(base, address) == (AddiR1R1 | m_frameSize) &&
                    DecodeBranch(LoadCode(base, address + 4), address + 4, false, restore) && restore == RestGprLr28Address;
            };

        auto returnsOne = [&](uint32_t address)
            {
                uint32_t next;
                return LoadCode(base, address) == LiR3One && (isEpilogue(address + 4) ||
                    (DecodeBranch(LoadCode(base, address + 4), address + 4, false, next) && isEpilogue(next)));
            };

        auto isFunction = [&](uint32_t address)
            {
                return address >= PPC_CODE_BASE && address - PPC_CODE_BASE < PPC_CODE_SIZE && (address & 3) == 0 &&
                    PPC_LOOKUP_FUNC(base, address) != nullptr;
            };

        uint32_t baseClass[2] = {};
        for (uint32_t flag = 0; flag < 2; flag++)
        {
            List& list = m_lists[flag];
            list.entries.clear();

            uint32_t pc = flag == 0 ? flagZero : fn + 32;
            if (pc <= fn + 28)
                return false;

            while (true)
            {
                const uint32_t word = LoadCode(base, pc);
                if (word == CmplwiCr6R29)
                {
                    uint32_t taken;
                    const uint32_t branch = LoadCode(base, pc + 4);
                    uint32_t next;
                    if (DecodeConditional(branch, pc + 4, BeqCr6, taken))
                        next = flag == 0 ? taken : pc + 8;
                    else if (DecodeConditional(branch, pc + 4, BneCr6, taken))
                        next = flag != 0 ? taken : pc + 8;
                    else
                        return false;

                    if (next <= pc)
                        return false;

                    pc = next;
                    continue;
                }

                if (word == MrR3R31)
                {
                    uint32_t call, hi, lo, noMatch;
                    if (!DecodeBranch(LoadCode(base, pc + 4), pc + 4, true, call) || call != RtTypeidAddress ||
                        ((hi = LoadCode(base, pc + 8)) & 0xFFFF0000) != LisR11 || LoadCode(base, pc + 12) != MrR4R3 ||
                        ((lo = LoadCode(base, pc + 16)) & 0xFFFF0000) != AddiR3R11 ||
                        !DecodeBranch(LoadCode(base, pc + 20), pc + 20, true, call) || call != TypeInfoEqualAddress ||
                        LoadCode(base, pc + 24) != ClrlwiDotR11R3 ||
                        !DecodeConditional(LoadCode(base, pc + 28), pc + 28, Beq, noMatch) || noMatch <= pc)
                    {
                        return false;
                    }

                    // lis r11,hi / addi r3,r11,lo: what type_info== gets as its first operand.
                    const uint32_t typeDescriptor = (hi << 16) + uint32_t(int32_t(int16_t(lo & 0xFFFF)));
                    if (!IsImageTypeDescriptor(typeDescriptor))
                        return false;

                    // On a match: mr r4,r31 and addi r3,r30,adjust (either order), bl handler, then the return.
                    const uint32_t first = LoadCode(base, pc + 32);
                    const uint32_t second = LoadCode(base, pc + 36);
                    uint32_t addi;
                    if (first == MrR4R31 && (second & 0xFFFF0000) == AddiR3R30)
                        addi = second;
                    else if (second == MrR4R31 && (first & 0xFFFF0000) == AddiR3R30)
                        addi = first;
                    else
                        return false;

                    uint32_t handler, next;
                    if (!DecodeBranch(LoadCode(base, pc + 40), pc + 40, true, handler) || !isFunction(handler))
                        return false;

                    bool one;
                    if (DecodeBranch(LoadCode(base, pc + 44), pc + 44, false, next) && isEpilogue(next))
                        one = false;
                    else if (DecodeBranch(LoadCode(base, pc + 44), pc + 44, false, next) && returnsOne(next))
                        one = true;
                    else if (returnsOne(pc + 44))
                        one = true;
                    else
                        return false;

                    bool seen = false;
                    for (const Entry& entry : list.entries)
                        seen |= entry.typeDescriptor == typeDescriptor;

                    // A later block for a type already listed can never match first: the earlier one returns.
                    if (!seen)
                        list.entries.push_back({ typeDescriptor, handler, int32_t(int16_t(addi & 0xFFFF)), one });

                    pc = noMatch;
                    continue;
                }

                if (word == MrR5R28 || word == MrR4R31 || word == MrR3R30)
                {
                    const uint32_t a = LoadCode(base, pc + 4);
                    const uint32_t b = LoadCode(base, pc + 8);
                    const bool all = (word == MrR5R28 || a == MrR5R28 || b == MrR5R28) &&
                        (word == MrR4R31 || a == MrR4R31 || b == MrR4R31) && (word == MrR3R30 || a == MrR3R30 || b == MrR3R30);

                    uint32_t target;
                    if (!all || !DecodeBranch(LoadCode(base, pc + 12), pc + 12, true, target) || !isFunction(target) ||
                        !isEpilogue(pc + 16))
                    {
                        return false;
                    }

                    baseClass[flag] = target;
                    break;
                }

                return false;
            }

            if (list.entries.size() > 0xFFFF)
                return false;

            list.Build();
        }

        // Both flags end in the same base class call (the walk for each finds one tail).
        if (baseClass[0] != baseClass[1])
            return false;

        m_baseClass = baseClass[0];
        return true;
    }

    static inline MessageDispatcher* s_first = nullptr;

    uint32_t m_address;
    PPCFunc* m_original;
    MessageDispatcher* m_next;
    bool m_active = false;
    uint32_t m_frameSize = 0;
    uint32_t m_baseClass = 0;
    List m_lists[2];
};

#define MESSAGE_DISPATCHER(address) \
    PPC_FUNC_IMPL(__imp__sub_##address); \
    static MessageDispatcher s_dispatcher_##address(0x##address, __imp__sub_##address); \
    PPC_FUNC(sub_##address) { s_dispatcher_##address.Run(ctx, base); }
#include "message_dispatch_list.inl"
#undef MESSAGE_DISPATCHER

namespace
{
    // The descriptors: 4-byte aligned words of the image equal to type_info's vftable, followed by a spare word
    // and a '.'-prefixed printable name. False finds cannot make a comparison wrong: only a type_info the game
    // passes is looked up, and an address it passes as one is one. A name found twice turns everything off.
    bool BuildImageTypeDescriptors(uint8_t* base, uint32_t& count)
    {
        std::vector<uint32_t> found;
        std::unordered_set<std::string_view> names;
        const uint8_t* image = base + PPC_IMAGE_BASE;
        const uint32_t end = uint32_t(PPC_IMAGE_SIZE) - 8;
        for (uint32_t offset = 0; offset < end; offset += 4)
        {
            uint32_t word;
            memcpy(&word, image + offset, sizeof(word));
            if (ByteSwap(word) != TypeInfoVftable)
                continue;

            const char* name = reinterpret_cast<const char*>(image + offset + 8);
            const uint32_t room = uint32_t(PPC_IMAGE_SIZE) - (offset + 8);
            if (name[0] != '.')
                continue;

            uint32_t length = 1;
            while (length < room && length < 4096 && name[length] >= 0x20 && name[length] <= 0x7E)
                length++;

            if (length >= room || length >= 4096 || name[length] != '\0' || length < 2)
                continue;

            if (!names.emplace(name, length).second)
            {
                fprintf(stderr, "[dispatch] type descriptor name %s found twice; exact type comparisons stay off.\n", name);
                return false;
            }

            found.push_back(uint32_t(PPC_IMAGE_BASE) + offset);
        }

        if (found.size() < 64)
            return false;

        const uint32_t first = found.front();
        const uint32_t span = found.back() - first + 4;
        auto* bits = new uint8_t[(span >> 5) + 1]();
        for (uint32_t address : found)
        {
            const uint32_t offset = address - first;
            bits[offset >> 5] |= uint8_t(1u << ((offset >> 2) & 7));
        }

        g_imageTypeDescriptors.bits = bits;
        g_imageTypeDescriptors.first = first;
        g_imageTypeDescriptors.span = span;
        count = uint32_t(found.size());
        return true;
    }
}

void NoteTypeInfoComparison(uint32_t typeInfo, bool equal)
{
    DispatchProbe* probe = t_probe;
    if (probe == nullptr || probe->firstMatch != 0)
        return;

    if (equal)
    {
        probe->firstMatch = typeInfo;
        probe->firstMatchIndex = probe->comparisons;
    }

    probe->comparisons++;
}

void InitMessageDispatch(uint8_t* base)
{
    const bool dispatch = Config::SwitchNativeMessageDispatch;
    const bool verify = Config::SwitchVerifyMessageDispatch;
    const bool exact = Config::SwitchExactTypeInfoSet;
    if (!dispatch && !verify && !exact)
        return;

    if (!g_nativeRtti)
    {
        fprintf(stderr, "[dispatch] native message dispatch and exact type comparisons need SwitchNativeRtti; off.\n");
        return;
    }

    uint32_t descriptors = 0;
    if (!BuildImageTypeDescriptors(base, descriptors))
    {
        fprintf(stderr, "[dispatch] the image's type descriptors could not be verified; everything stays off.\n");
        return;
    }

    g_exactTypeInfoSet = exact;

    uint32_t decoded = 0, total = 0, entries = 0;
    if (dispatch || verify)
    {
        MessageDispatcher::InitAll(base, true, decoded, total, entries);
        g_verifyMessageDispatch = verify;
    }

    fprintf(stderr, "[dispatch] %u type descriptors (all names different); exact type comparisons %s; native message "
        "dispatch %s: %u of %u dispatchers decoded, %u types%s\n", descriptors, exact ? "on" : "off",
        dispatch || verify ? "on" : "off", decoded, total, entries,
        verify ? " (verifying: the recompiled code runs and the table's choice is compared)" : "");
}

#endif
