//
// Hook framework for IL2CPP method hooking
//

#ifndef ZYGISK_IL2CPPDUMPER_HOOK_H
#define ZYGISK_IL2CPPDUMPER_HOOK_H

#include <cstdint>
#include <cstddef>
#include <functional>

// Hook callback type: original_func_ptr, args...
typedef std::function<void*(void*, ...)> HookCallback;

struct HookInfo {
    const char* class_name;
    const char* namespace_name;
    const char* method_name;
    uint64_t offset;  // RVA offset
    uint64_t va;      // Virtual address
    void* original_func;
    void* hook_func;
    bool hooked;
};

// Initialize hook system
bool hook_init(void* il2cpp_handle);

// Install hook for a method
bool hook_install(HookInfo* info);

// Remove hook
bool hook_remove(HookInfo* info);

// Install mail system hooks
bool hook_mail_system(void* il2cpp_handle);

#endif //ZYGISK_IL2CPPDUMPER_HOOK_H

