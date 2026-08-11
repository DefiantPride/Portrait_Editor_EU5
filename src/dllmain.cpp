// =============================================================================
//  ACE Portrait Bridge  v2.0.0
//  Europa Universalis V 1.3.11 "Pavia", launcher checksum 6210
// =============================================================================
//
//  WHAT IT DOES
//    Two hotkeys, while the game is running:
//
//      Ctrl+Shift+L   LOAD   selected character  ->  portrait editor
//      Ctrl+Shift+S   SAVE   portrait editor     ->  selected character
//
//    Open a character's panel, open the portrait editor (console: pe), press
//    LOAD, edit the face, press SAVE, then save the game.
//
//  WHY THIS IS SIMPLE
//    Both endpoints are the same structure:
//
//      CPortraitEditorWindow + 0x108  ->  CDnaString
//      CCharacter            + 0x148  ->  CDnaString
//
//    So the whole transfer is a memcpy of count*8 bytes between two gene
//    buffers. No Base64, no clipboard, no files, no game functions called.
//    Verified in Cheat Engine Lua before this was written.
//
//  WHAT IT DOES NOT DO
//    - It does not save your game. Changes live in memory until you do.
//    - It does not redraw the in-game portrait. Close and reopen the
//      character's panel, or let a day pass. (The editor's own preview may
//      need "Restart Portrait".)
//    - It calls no engine code, so it cannot trigger a refresh itself.
//
//  VERSION BINDING
//    Every offset came from one specific build. VerifyBuild() checks two
//    function prologues before anything runs. Do not remove it.
//
//  BUILD
//    cl /LD /EHsc /O2 /std:c++17 src\dllmain.cpp /link /OUT:ace_portrait_bridge.dll user32.lib
//    (user32.lib is needed for GetAsyncKeyState)
//
// =============================================================================

// MSVC flags fopen as deprecated in favour of fopen_s. fopen is correct and
// portable here; the log file is opened, written and closed immediately.
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
//  Build-specific constants (RVAs from image base 0x140000000)
// -----------------------------------------------------------------------------

namespace rva {
    constexpr uintptr_t kEditorVftable   = 0x64255D8;  // CPortraitEditorWindow (primary)
    constexpr uintptr_t kLateralCtxVft   = 0x62D97A8;  // CCharacterLateralViewContext
    constexpr uintptr_t kCDnaStringVft   = 0x6426478;
    constexpr uintptr_t kCharacterDbPtr  = 0x8351220;
    constexpr uintptr_t kFnGetDna        = 0x440FE70;  // guard only, never called
    constexpr uintptr_t kFnToBase64      = 0x1E6CFB0;  // guard only, never called
}

namespace off {
    constexpr uintptr_t kEditorDna = 0x108;   // editor's own CDnaString
    constexpr uintptr_t kCtxId     = 0x48;    // uint32 character id of the open panel

    constexpr uintptr_t kCharId    = 0x08;
    constexpr uintptr_t kCharDna   = 0x148;

    constexpr uintptr_t kDnaVec    = 0x08;
    constexpr uintptr_t kDnaHash   = 0x28;
    constexpr uintptr_t kDnaFlag   = 0x2C;

    constexpr uintptr_t kVecBuf    = 0x00;
    constexpr uintptr_t kVecCount  = 0x0C;

    constexpr uintptr_t kDbArray   = 0x30;
    constexpr uintptr_t kDbCount   = 0x3C;
    constexpr uintptr_t kDbStride  = 0x10;
    constexpr uintptr_t kDbEntry   = 0x08;

    constexpr size_t kBytesPerGene = 8;       // 4 x uint16: (tmpl,val,tmpl,val)
}

// CCharacter::GetDna prologue -- the `mov rax,[rcx+0x148]` literally encodes
// the DNA member offset this whole tool depends on.
static const uint8_t kSigGetDna[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x81, 0x48, 0x01, 0x00, 0x00
};
// CDnaString::ToBase64String prologue -- its `mov rax,[rcx+8]` encodes the
// vector offset inside CDnaString.
static const uint8_t kSigToBase64[] = {
    0x40, 0x55, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8D, 0x6C, 0x24, 0x20,
    0x48, 0x8B, 0x41, 0x08
};

// -----------------------------------------------------------------------------
//  Logging
// -----------------------------------------------------------------------------

static std::string g_dir;
static uintptr_t   g_base = 0;

static void Log(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (FILE* f = fopen((g_dir + "ace_portrait_bridge.log").c_str(), "a")) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, line);
        fclose(f);
    }
}

// -----------------------------------------------------------------------------
//  Safe reads
// -----------------------------------------------------------------------------

static bool ReadMem(const void* src, void* dst, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool WriteMem(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template <typename T>
static bool Read(uintptr_t addr, T* out) {
    if (!addr) return false;
    return ReadMem(reinterpret_cast<const void*>(addr), out, sizeof(T));
}

// -----------------------------------------------------------------------------
//  Scanning for objects by vtable pointer
//
//  Same technique the Lua scripts used: an object's first qword is its vtable,
//  so scanning writable memory for that pointer finds every live instance.
//  Image and guarded pages are skipped -- objects live on the heap.
// -----------------------------------------------------------------------------

// MSVC forbids __try in a function that also has C++ objects requiring
// unwinding, so the guarded loop lives here on its own -- raw pointers only,
// no destructors. A region can vanish or change protection mid-scan; the
// handler simply abandons that region.
static size_t ScanRegionCollect(const uintptr_t* begin, size_t qwords,
                                uintptr_t needle, uintptr_t* out, size_t cap) {
    size_t n = 0;
    __try {
        for (size_t i = 0; i < qwords && n < cap; ++i) {
            if (begin[i] == needle)
                out[n++] = reinterpret_cast<uintptr_t>(begin + i);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // partial results from before the fault are still valid
    }
    return n;
}

// A qword holding an object's vtable address is NOT proof of an object. The
// same value turns up in stack slots, in freed memory, and inside other
// structures that cache the pointer. Taking the first match was only ever
// working by luck. So every candidate is checked by a validator before it is
// accepted, and the scan keeps going until one passes.
static bool IsValidEditor(uintptr_t candidate);
static bool IsValidContext(uintptr_t candidate);

constexpr size_t kCandidatesPerRegion = 64;

// Locating both objects used to cost ~100 s each: two independent sweeps of
// every committed private read-write region, and both objects sit near the top
// of the address space so early exit saved nothing.
//
// Two changes fix that.
//
//   1. One pass, both needles. Halves the work outright.
//
//   2. Skip very large regions on the first pass. This game allocates through
//      mimalloc, which manages its heap in segments of a few tens of MB, and
//      both objects live in that heap. The multi-hundred-MB regions are asset
//      and texture data -- the bulk of the address space and none of what we
//      want. If the fast pass misses either object we repeat with no limit, so
//      the worst case is slower, never wrong.
//
// regionLimit == 0 means no limit.
// Scans memory for the editor and for every panel context.
//
// Two things make this fast enough to be usable.
//
//   Descending order. Every object we want lives high in the address space
//   (0x539.../0x53A... in practice) while the address space starts at 0x10000
//   and holds >15 GB of committed memory in between. Walking upward reached
//   them last, every time -- 100 to 200 seconds. Enumerating regions is cheap;
//   only reading them is slow, so we collect the region list first and then
//   read it top-down.
//
//   This is only safe because the caller now picks a context by which
//   character id the most contexts agree on, not by address order. Under the
//   old lowest-address rule, reversing the scan would have changed the answer.
//
//   Early exit. Once the editor is found and at least two contexts name the
//   same character, there is nothing further to learn.
constexpr size_t kMaxRegions = 8192;

struct Region { const uintptr_t* base; size_t qwords; size_t bytes; };

static void ScanAll(uintptr_t editorVft, uintptr_t* editorOut,
                    uintptr_t ctxVft, uintptr_t* ctxOut, uint32_t* idOut,
                    size_t ctxCap, size_t* ctxCount,
                    size_t* bytesScanned, size_t* rejected) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    // pass 1 -- enumerate usable regions (no reads, so this is quick)
    static Region regions[kMaxRegions];
    size_t regionCount = 0;

    auto addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const auto maxAddr = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < maxAddr && regionCount < kMaxRegions &&
           VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) {
        const auto next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;

        const bool usable =
            mbi.State == MEM_COMMIT &&
            mbi.Type  == MEM_PRIVATE &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY));

        if (usable && mbi.RegionSize >= sizeof(uintptr_t)) {
            regions[regionCount].base   = reinterpret_cast<const uintptr_t*>(mbi.BaseAddress);
            regions[regionCount].qwords = mbi.RegionSize / sizeof(uintptr_t);
            regions[regionCount].bytes  = mbi.RegionSize;
            ++regionCount;
        }

        if (next <= addr) break;
        addr = next;
    }

    // pass 2 -- read them highest first
    uintptr_t cand[kCandidatesPerRegion];

    for (size_t r = regionCount; r-- > 0; ) {
        const Region& reg = regions[r];
        *bytesScanned += reg.bytes;

        if (!*editorOut) {
            const size_t n = ScanRegionCollect(reg.base, reg.qwords, editorVft,
                                               cand, kCandidatesPerRegion);
            for (size_t i = 0; i < n && !*editorOut; ++i) {
                if (IsValidEditor(cand[i])) *editorOut = cand[i]; else ++*rejected;
            }
        }

        if (*ctxCount < ctxCap) {
            const size_t n = ScanRegionCollect(reg.base, reg.qwords, ctxVft,
                                               cand, kCandidatesPerRegion);
            for (size_t i = 0; i < n && *ctxCount < ctxCap; ++i) {
                if (!IsValidContext(cand[i])) continue;
                uint32_t id = 0;
                Read(cand[i] + off::kCtxId, &id);
                ctxOut[*ctxCount] = cand[i];
                idOut[*ctxCount]  = id;
                ++*ctxCount;
            }
        }

        // enough once the editor is known and two contexts name the same one
        if (*editorOut && *ctxCount >= 2) {
            bool agree = false;
            for (size_t i = 0; i < *ctxCount && !agree; ++i)
                for (size_t j = i + 1; j < *ctxCount && !agree; ++j)
                    if (idOut[i] == idOut[j]) agree = true;
            if (agree) return;
        }
    }
}

// -----------------------------------------------------------------------------
//  CDnaString access
// -----------------------------------------------------------------------------

struct DnaInfo {
    uintptr_t dna   = 0;
    uintptr_t buf   = 0;
    int32_t   count = 0;
    uint32_t  hash  = 0;
    uint8_t   flag  = 0;
};

static bool DescribeDna(uintptr_t dna, DnaInfo* out, const char** why) {
    if (!dna) { *why = "null CDnaString"; return false; }

    uintptr_t vft = 0;
    if (!Read(dna, &vft)) { *why = "unreadable"; return false; }
    if (vft != g_base + rva::kCDnaStringVft) { *why = "not a CDnaString"; return false; }

    uintptr_t vec = 0;
    if (!Read(dna + off::kDnaVec, &vec) || !vec) { *why = "null vector"; return false; }

    int32_t   n   = 0;
    uintptr_t buf = 0;
    if (!Read(vec + off::kVecCount, &n))  { *why = "unreadable count"; return false; }
    if (!Read(vec + off::kVecBuf,   &buf)){ *why = "unreadable buffer"; return false; }
    if (n <= 0 || n > 4096 || !buf)       { *why = "implausible gene vector"; return false; }

    out->dna = dna; out->buf = buf; out->count = n;
    Read(dna + off::kDnaHash, &out->hash);
    Read(dna + off::kDnaFlag, &out->flag);
    return true;
}

// -----------------------------------------------------------------------------
//  Object lookup
// -----------------------------------------------------------------------------

constexpr size_t kFastRegionLimit = 128ull * 1024 * 1024;

static uintptr_t g_cachedEditor = 0;
static uintptr_t g_cachedCtx    = 0;

// id -> CCharacter*, or 0. Used both to validate a panel context and to do the
// real lookup.
static uintptr_t ResolveCharacter(uint32_t id) {
    if (id == 0xFFFFFFFF) return 0;

    uintptr_t db = 0;
    if (!Read(g_base + rva::kCharacterDbPtr, &db) || !db) return 0;

    uint32_t  count = 0;
    uintptr_t array = 0;
    if (!Read(db + off::kDbCount, &count)) return 0;
    if (!Read(db + off::kDbArray, &array) || !array) return 0;

    const uint32_t slot = id & 0xFFFFFF;
    if (slot >= count) return 0;

    uintptr_t ch = 0;
    if (!Read(array + slot * off::kDbStride + off::kDbEntry, &ch) || !ch) return 0;

    uint32_t actual = 0;
    if (!Read(ch + off::kCharId, &actual) || actual != id) return 0;
    return ch;
}

// A real editor carries a valid CDnaString at +0x108.
static bool IsValidEditor(uintptr_t candidate) {
    DnaInfo info{};
    const char* why = "";
    return DescribeDna(candidate + off::kEditorDna, &info, &why);
}

// A real panel context holds a character id at +0x48 that resolves in the
// database. Stale copies of the vtable pointer will not.
static bool IsValidContext(uintptr_t candidate) {
    uint32_t id = 0;
    if (!Read(candidate + off::kCtxId, &id)) return false;
    return ResolveCharacter(id) != 0;
}

// Ensure both addresses are known, scanning at most twice (fast pass, then
// unrestricted) and only when a cached address has gone stale.
// Choosing which context is the open panel.
//
// Two things went wrong before this, and the log data fixed both.
//
//   Lowest address is not the answer. Every spouse and child portrait widget
//   has its own context. On a ruler with 26 children one of those sorts below
//   the main panel's, and the wrong face loads.
//
//   Worse, a match can be a STACK slot that momentarily happened to hold the
//   vtable pointer. One showed up at 0x000000E1_22BFF248 while the real
//   objects were at 0x245.../0x246..., its "id" was a fragment of an unrelated
//   pointer, and it had already changed by the time we used it.
//
// So: re-read each candidate after a short pause and drop any whose id moved
// (stack churn), then pick the id held by the MOST contexts. The main panel's
// character is referenced by several widgets at once; stale and accidental
// matches are not. Observed: 3 contexts named the open character, the bogus
// stack one named nothing real.
constexpr size_t kMaxContexts = 64;

static bool EnsureObjects() {
    // The editor is one long-lived object, so caching it is safe -- revalidate
    // with a single pointer read and rescan only if it moved.
    uintptr_t vft = 0;
    if (g_cachedEditor && !(Read(g_cachedEditor, &vft) && vft == g_base + rva::kEditorVftable)) {
        Log("  editor moved, rescanning");
        g_cachedEditor = 0;
    }

    // Contexts are NOT cacheable. There is one per portrait widget, and the
    // ones belonging to a character you have navigated away from stay alive
    // holding their old id. A cached pointer therefore stays perfectly valid
    // while silently naming the previous character -- which is exactly the bug
    // this replaced. They are found fresh on every transfer.
    g_cachedCtx = 0;

    uintptr_t editor = g_cachedEditor;      // pre-set: skips the editor search
    uintptr_t ctxs[kMaxContexts];
    uint32_t  ids[kMaxContexts];
    size_t    n = 0, rejected = 0, bytes = 0;

    const DWORD t0 = GetTickCount();
    ScanAll(g_base + rva::kEditorVftable, &editor,
            g_base + rva::kLateralCtxVft, ctxs, ids, kMaxContexts, &n,
            &bytes, &rejected);

    Log("  scan: %lu ms, %llu MB  editor=%s  contexts=%zu (rejected %zu)",
        GetTickCount() - t0, static_cast<unsigned long long>(bytes >> 20),
        editor ? (g_cachedEditor ? "cached" : "found") : "-", n, rejected);

    // settle: anything whose id or vtable moved was never a real object
    Sleep(50);
    size_t keep = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t nowId = 0;
        uintptr_t nowVft = 0;
        const bool stable =
            Read(ctxs[i], &nowVft) && nowVft == g_base + rva::kLateralCtxVft &&
            Read(ctxs[i] + off::kCtxId, &nowId) && nowId == ids[i] &&
            ResolveCharacter(nowId) != 0;

        if (stable) { ctxs[keep] = ctxs[i]; ids[keep] = ids[i]; ++keep; }
        else Log("    context %p unstable (id 0x%X -> 0x%X) -- discarded",
                 (void*)ctxs[i], ids[i], nowId);
    }
    n = keep;

    // pick the id the most contexts agree on
    size_t best = 0, bestCount = 0;
    for (size_t i = 0; i < n; ++i) {
        size_t count = 0;
        for (size_t j = 0; j < n; ++j) if (ids[j] == ids[i]) ++count;
        if (count > bestCount) { bestCount = count; best = i; }
    }

    for (size_t i = 0; i < n; ++i)
        Log("    context[%zu] %p  id 0x%X%s", i, (void*)ctxs[i], ids[i],
            i == best ? "   <-- chosen" : "");

    if (n) Log("  %zu of %zu contexts agree on character 0x%X", bestCount, n, ids[best]);

    if (editor) g_cachedEditor = editor;
    if (n)      g_cachedCtx    = ctxs[best];

    return g_cachedEditor && g_cachedCtx;
}

// -----------------------------------------------------------------------------
//  Endpoints
// -----------------------------------------------------------------------------

// The portrait editor. Exactly one instance is expected while it is open.
static bool FindEditorDna(DnaInfo* out) {
    const uintptr_t editor = g_cachedEditor;
    if (!editor) {
        Log("  portrait editor is not open (console: pe)");
        return false;
    }

    const char* why = "";
    if (!DescribeDna(editor + off::kEditorDna, out, &why)) {
        Log("  editor DNA at +0x108: %s", why);
        return false;
    }
    Log("  editor %p  dna %p  %d genes", (void*)editor, (void*)out->dna, out->count);
    return true;
}

// The character whose panel is open.
//
// Several CCharacterLateralViewContext objects are alive at once (main panel
// plus sub-widgets for spouse, children, hover cards). Observed across many
// runs, the lowest-address one is the long-lived main panel and its id tracks
// whatever you have open; the others hold stale ids.
static bool FindSelectedCharacterDna(DnaInfo* out, uint32_t* idOut) {
    const uintptr_t ctx = g_cachedCtx;
    if (!ctx) {
        Log("  no character panel open");
        return false;
    }

    uint32_t id = 0;
    if (!Read(ctx + off::kCtxId, &id) || id == 0xFFFFFFFF) {
        Log("  main context has no character set");
        return false;
    }

    const uintptr_t ch = ResolveCharacter(id);
    if (!ch) {
        Log("  could not resolve character 0x%X in the database", id);
        return false;
    }

    uintptr_t dna = 0;
    if (!Read(ch + off::kCharDna, &dna) || !dna) {
        Log("  character 0x%X has no DNA yet -- open its portrait in game first", id);
        return false;
    }

    const char* why = "";
    if (!DescribeDna(dna, out, &why)) {
        Log("  character DNA at +0x148: %s", why);
        return false;
    }

    // A zero hash means the DNA was allocated but never generated. Reading it
    // would hand back the shared global default -- the same generic face for
    // every such character -- so refuse rather than silently succeed.
    if (out->hash == 0) {
        Log("  character 0x%X has an ungenerated DNA (hash 0)", id);
        return false;
    }

    *idOut = id;
    Log("  character 0x%X at %p  dna %p  %d genes", id, (void*)ch, (void*)out->dna, out->count);
    return true;
}

// -----------------------------------------------------------------------------
//  Transfer
// -----------------------------------------------------------------------------

static void Transfer(bool intoEditor) {
    Log(intoEditor ? "--- LOAD: character -> editor ---"
                   : "--- SAVE: editor -> character ---");

    DnaInfo ed{}, cd{};
    uint32_t id = 0;

    // one scan covers both objects; after the first time they are cached
    EnsureObjects();

    if (!FindEditorDna(&ed)) return;
    if (!FindSelectedCharacterDna(&cd, &id)) return;

    if (ed.count != cd.count) {
        Log("  gene count mismatch: editor %d, character %d -- refusing",
            ed.count, cd.count);
        return;
    }

    const DnaInfo& src = intoEditor ? cd : ed;
    const DnaInfo& dst = intoEditor ? ed : cd;
    const size_t   size = static_cast<size_t>(src.count) * off::kBytesPerGene;

    std::vector<uint8_t> bytes(size);
    if (!ReadMem(reinterpret_cast<const void*>(src.buf), bytes.data(), size)) {
        Log("  could not read the source gene buffer");
        return;
    }

    uint16_t before[2] = {0, 0};
    ReadMem(reinterpret_cast<const void*>(dst.buf), before, sizeof(before));

    if (!WriteMem(reinterpret_cast<void*>(dst.buf), bytes.data(), size)) {
        Log("  WRITE FAILED -- destination not writable");
        return;
    }
    // carry the hash across so the destination stays self-consistent
    WriteMem(reinterpret_cast<void*>(dst.dna + off::kDnaHash), &src.hash, sizeof(src.hash));

    uint16_t after[2] = {0, 0};
    ReadMem(reinterpret_cast<const void*>(dst.buf), after, sizeof(after));

    Log("  %zu bytes copied. gene0 {%u %u} -> {%u %u}",
        size, before[0], before[1], after[0], after[1]);
    Log("  expect Color Coordinates X %.1f%% / Y %.1f%%",
        after[0] / 255.0 * 100.0, after[1] / 255.0 * 100.0);

    if (intoEditor)
        Log("  if the preview does not redraw, click \"Restart Portrait\"");
    else
        Log("  SAVE THE GAME to persist. Reopen the panel or let a day pass to redraw.");
}

// -----------------------------------------------------------------------------
//  Build verification
// -----------------------------------------------------------------------------

static bool CheckSig(uintptr_t addr, const uint8_t* sig, size_t n, const char* what) {
    std::vector<uint8_t> got(n);
    if (!ReadMem(reinterpret_cast<const void*>(addr), got.data(), n) ||
        memcmp(got.data(), sig, n) != 0) {
        Log("  guard FAILED: %s", what);
        return false;
    }
    Log("  guard ok: %s", what);
    return true;
}

static bool VerifyBuild() {
    Log("verifying build signatures...");
    bool ok = true;
    ok &= CheckSig(g_base + rva::kFnGetDna,   kSigGetDna,   sizeof(kSigGetDna),
                   "CCharacter::GetDna prologue (encodes +0x148)");
    ok &= CheckSig(g_base + rva::kFnToBase64, kSigToBase64, sizeof(kSigToBase64),
                   "CDnaString::ToBase64String prologue (encodes +0x08)");
    return ok;
}

// -----------------------------------------------------------------------------

static DWORD WINAPI Worker(LPVOID) {
    Log("=== ACE Portrait Bridge v2.0.0 ===");

    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    Log("eu5.exe base = %p", (void*)g_base);

    if (!VerifyBuild()) {
        Log("BUILD MISMATCH -- offsets are for EU5 1.3.11 (checksum 6210) only.");
        Log("Refusing to read or write anything.");
        return 0;
    }
    Log("ready.  Ctrl+Shift+L = load character into editor");
    Log("        Ctrl+Shift+S = save editor onto character");
    Log("        Ctrl+Shift+R = forget cached addresses (after reopening the editor)");

    bool lDown = false, sDown = false, rDown = false;
    for (;;) {
        const bool mods = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                          (GetAsyncKeyState(VK_SHIFT)   & 0x8000);

        const bool l = mods && (GetAsyncKeyState('L') & 0x8000);
        const bool s = mods && (GetAsyncKeyState('S') & 0x8000);
        const bool r = mods && (GetAsyncKeyState('R') & 0x8000);

        if (l && !lDown) Transfer(true);
        if (s && !sDown) Transfer(false);
        if (r && !rDown) {
            g_cachedEditor = 0;
            g_cachedCtx    = 0;
            Log("cache cleared -- next transfer will rescan");
        }
        lDown = l; sDown = s; rDown = r;

        Sleep(30);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        char path[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string p(path);
        const size_t slash = p.find_last_of("\\/");
        g_dir = (slash == std::string::npos) ? "" : p.substr(0, slash + 1);

        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    }
    return TRUE;
}
