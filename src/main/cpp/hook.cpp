//
// Hook framework implementation
//

#include "hook.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include <climits>
#include <cerrno>

extern uint64_t il2cpp_base; // From il2cpp_dump.cpp

#ifdef __arm__
#define ARM_MODE 1
#elif defined(__aarch64__)
#define ARM64_MODE 1
#elif defined(__i386__)
#define X86_MODE 1
#elif defined(__x86_64__)
#define X86_64_MODE 1
#endif

// Inline hook implementation
static bool install_inline_hook(void* target, void* hook_func, void** original_func) {
    if (!target || !hook_func) {
        LOGE("Invalid hook parameters");
        return false;
    }

    // Verify address is valid (in readable memory)
    // Try to read first byte to check if address is valid
    volatile uint8_t test_read = *((volatile uint8_t*)target);
    (void)test_read; // Suppress unused warning
    
    // Calculate page size
    size_t page_size = sysconf(_SC_PAGESIZE);
    void* page_start = (void*)((uintptr_t)target & ~(page_size - 1));
    
    // Make memory writable
    int ret = mprotect(page_start, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (ret != 0) {
        LOGE("Failed to make memory writable: %p (errno: %d)", target, errno);
        return false;
    }

#if defined(ARM64_MODE)
    // ARM64: Use LDR + BR for absolute address jump
    // This requires 2 instructions (8 bytes)
    uint64_t* target_ptr = (uint64_t*)target;
    
    // Save original instruction
    if (original_func) {
        *original_func = (void*)(uintptr_t)target_ptr[0];
    }
    
    // LDR x16, [PC, #8]  ; Load hook address
    // BR x16              ; Jump to hook
    // Hook address (8 bytes)
    uint64_t hook_addr = (uint64_t)(uintptr_t)hook_func;
    
    // LDR x16, [PC, #8] = 0x58000050 (little endian: 0x50000058)
    // BR x16 = 0xD61F0200
    target_ptr[0] = 0x58000050; // LDR x16, [PC, #8]
    target_ptr[1] = 0xD61F0200;  // BR x16
    target_ptr[2] = hook_addr;   // Hook address
    
    __builtin___clear_cache((char*)target, (char*)target + 24);
    
#elif defined(ARM_MODE)
    // ARM: LDR PC, [PC, #-4] + address
    // This is a 2-instruction hook
    uint32_t* target_ptr = (uint32_t*)target;
    
    // Save original instructions
    if (original_func) {
        *original_func = (void*)((uintptr_t)target_ptr[0] | ((uintptr_t)target_ptr[1] << 32));
    }
    
    // LDR PC, [PC, #-4]
    target_ptr[0] = 0xE51FF004;
    // Hook function address
    target_ptr[1] = (uint32_t)(uintptr_t)hook_func;
    
    __builtin___clear_cache((char*)target, (char*)target + 8);
    
#elif defined(X86_MODE) || defined(X86_64_MODE)
    // x86/x64: JMP rel32 (E9)
    int64_t offset = (int64_t)hook_func - ((int64_t)target + 5);
    
    if (offset < INT32_MIN || offset > INT32_MAX) {
        LOGE("Hook offset too large for JMP rel32");
        return false;
    }
    
    uint8_t* target_bytes = (uint8_t*)target;
    
    // Save original 5 bytes for restoration
    if (original_func) {
        uint8_t* orig_bytes = new uint8_t[5];
        memcpy(orig_bytes, target_bytes, 5);
        *original_func = (void*)orig_bytes;
    }
    
    // JMP rel32: E9 [offset]
    target_bytes[0] = 0xE9;
    *(int32_t*)(target_bytes + 1) = (int32_t)offset;
    
    __builtin___clear_cache((char*)target, (char*)target + 5);
    
#else
    LOGE("Unsupported architecture");
    return false;
#endif

    LOGI("Hook installed at %p -> %p", target, hook_func);
    return true;
}

bool hook_init(void* il2cpp_handle) {
    if (!il2cpp_handle) {
        LOGE("Invalid il2cpp handle");
        return false;
    }
    LOGI("Hook system initialized");
    return true;
}

bool hook_install(HookInfo* info) {
    if (!info || !info->va) {
        LOGE("Invalid hook info");
        return false;
    }
    
    if (info->hooked) {
        LOGW("Hook already installed for %s::%s", info->class_name, info->method_name);
        return true;
    }
    
    void* target_addr = (void*)info->va;
    void* hook_addr = info->hook_func;
    
    if (!target_addr || !hook_addr) {
        LOGE("Invalid hook addresses for %s::%s", info->class_name, info->method_name);
        return false;
    }
    
    if (install_inline_hook(target_addr, hook_addr, &info->original_func)) {
        info->hooked = true;
        LOGI("Successfully hooked %s::%s::%s at %p", 
             info->namespace_name, info->class_name, info->method_name, target_addr);
        return true;
    }
    
    LOGE("Failed to install hook for %s::%s", info->class_name, info->method_name);
    return false;
}

bool hook_remove(HookInfo* info) {
    if (!info || !info->hooked) {
        return false;
    }
    
    // TODO: Implement hook removal
    // This requires restoring original instructions
    LOGW("Hook removal not fully implemented");
    return false;
}

