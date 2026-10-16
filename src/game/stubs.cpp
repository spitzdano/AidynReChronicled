// Hand-written replacements for libultra functions that N64Recomp skips (so the game's own copies
// are not recompiled) but N64ModernRuntime does not define. Each one says what the game expects;
// all are listed in README.md. Controller Pak behaviour lives in pak.cpp.
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

extern "C" gpr cop0_status_read(recomp_context* ctx);

// __osSiGetAccess / __osSiRelAccess: take and release the SI access semaphore around PIF commands.
// The runtime handles controller I/O itself, so there is nothing to guard.
extern "C" void __osSiGetAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
}

extern "C" void __osSiRelAccess_recomp(uint8_t* rdram, recomp_context* ctx) {
}

// __osSpDeviceBusy: is the RSP (SP) busy? RSP tasks run synchronously inside the runtime, so never.
extern "C" void __osSpDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

// __osGetSR: reads the CP0 status register. The runtime keeps the emulated value in the context.
extern "C" void __osGetSR_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = cop0_status_read(ctx);
}

// The boot thread (libultra's "main", running bootproc) ends by starting the real init thread and
// returning into a `break` instruction. aidyn.us.toml hooks that spot to call this instead: the thread
// sleeps until the program quits, then ends itself the way a destroyed game thread does.
#include <atomic>
#include <chrono>
#include <thread>

std::atomic_bool aidyn_quitting{ false };

extern "C" void aidyn_park_boot_thread(void) {
    while (!aidyn_quitting.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw ultramodern::thread_terminated{};
}
