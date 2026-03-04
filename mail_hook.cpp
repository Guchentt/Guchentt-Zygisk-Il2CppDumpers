//
// Mail system hook implementation
//

#include "mail_hook.h"
#include "hook.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <cstdarg>
#include <cinttypes>
#include <cstdlib>

// Hook info storage
static HookInfo mail_hooks[32];
static int mail_hook_count = 0;
extern uint64_t il2cpp_base; // From il2cpp_dump.cpp

// Hooked function stubs
extern "C" {
    // Hook function stubs - these will be called when hooked methods are invoked
    // Note: Actual signatures depend on the game's IL2CPP methods
    // These are generic stubs that log the hook and call callbacks
    
    void* mail_get_reward_hook(void* self, ...) {
        LOGI("=== Mail GetReward Hook ===");
        LOGI("Self: %p", self);
        
        // Extract arguments (this is simplified - actual implementation depends on method signature)
        va_list args;
        va_start(args, self);
        void* mail_id = va_arg(args, void*);
        va_end(args);
        
        LOGI("Mail ID: %p", mail_id);
        
        on_mail_reward_received(mail_id, nullptr);
        
        // Call original function (would need to restore and call)
        // For now, return nullptr
        return nullptr;
    }
    
    void* mail_read_hook(void* self, ...) {
        LOGI("=== Mail Read Hook ===");
        LOGI("Self: %p", self);
        
        va_list args;
        va_start(args, self);
        void* mail_id = va_arg(args, void*);
        va_end(args);
        
        LOGI("Mail ID: %p", mail_id);
        
        on_mail_read(mail_id);
        
        return nullptr;
    }
    
    void* mail_delete_hook(void* self, ...) {
        LOGI("=== Mail Delete Hook ===");
        LOGI("Self: %p", self);
        
        va_list args;
        va_start(args, self);
        void* mail_id = va_arg(args, void*);
        va_end(args);
        
        LOGI("Mail ID: %p", mail_id);
        
        on_mail_deleted(mail_id);
        
        return nullptr;
    }
}

// Simple JSON value extractor (for simple cases)
static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return match[1].str();
    }
    return "";
}

static uint64_t extract_json_hex(const std::string& json, const std::string& key) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        std::string hex_str = match[1].str();
        return std::stoull(hex_str, nullptr, 16);
    }
    return 0;
}

// Parse script.json to find mail-related methods (simplified parser)
static bool parse_mail_methods(const char* script_json_path) {
    std::ifstream file(script_json_path);
    if (!file.is_open()) {
        LOGE("Failed to open script.json: %s", script_json_path);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_content = buffer.str();
    
    // Simple pattern matching for mail-related methods
    // Look for patterns like: "name": "GetMailReward", "va": "0x...", "offset": "0x..."
    std::regex method_pattern("\"name\"\\s*:\\s*\"([^\"]+)\"");
    std::regex va_pattern("\"va\"\\s*:\\s*\"([^\"]+)\"");
    std::regex offset_pattern("\"offset\"\\s*:\\s*\"([^\"]+)\"");
    std::regex class_pattern("\"name\"\\s*:\\s*\"([^\"]*(?:Mail|MailManager)[^\"]*)\"");
    std::regex namespace_pattern("\"namespace\"\\s*:\\s*\"([^\"]+)\"");
    
    std::sregex_iterator iter(json_content.begin(), json_content.end(), method_pattern);
    std::sregex_iterator end;
    
    int found = 0;
    for (; iter != end && mail_hook_count < 32; ++iter) {
        std::string method_name = iter->str(1);
        
        // Filter out getters, setters, backing fields, and class names FIRST
        if (method_name.find("get_") == 0 || 
            method_name.find("set_") == 0 ||
            method_name.find("k__BackingField") != std::string::npos ||
            method_name.find("XTable") == 0 ||
            method_name.length() < 4) { // Too short
            continue;
        }
        
        // Only hook actual methods that start with capital letter
        // and contain mail-related keywords
        bool is_mail_method = false;
        if (method_name[0] >= 'A' && method_name[0] <= 'Z') { // Starts with capital
            if (method_name.find("Mail") != std::string::npos ||
                method_name.find("GetReward") != std::string::npos ||
                method_name.find("ReadMail") != std::string::npos ||
                method_name.find("DeleteMail") != std::string::npos ||
                method_name.find("ReceiveMail") != std::string::npos ||
                method_name.find("SendMail") != std::string::npos) {
                is_mail_method = true;
            }
        }
        
        if (!is_mail_method) {
            continue;
        }
        
        LOGI("Processing mail method candidate: %s", method_name.c_str());
        
        // Find offset and VA near this method name (look forward in the JSON)
        size_t pos = iter->position();
        size_t search_start = pos;
        size_t search_end = std::min(pos + 500, json_content.length());
        std::string context = json_content.substr(search_start, search_end - search_start);
        
        std::smatch offset_match, va_match;
        bool has_offset = std::regex_search(context, offset_match, offset_pattern);
        bool has_va = std::regex_search(context, va_match, va_pattern);
        
        if (!has_offset && !has_va) {
            continue; // Need at least one
        }
        
        uint64_t offset = 0;
        uint64_t va_dump = 0;
        
        // Parse offset (RVA)
        if (has_offset) {
            std::string offset_str = offset_match[1].str();
            char* endptr = nullptr;
            offset = strtoull(offset_str.c_str(), &endptr, 16);
            if (endptr == offset_str.c_str() || *endptr != '\0') {
                continue;
            }
        }
        
        // Parse VA (from dump)
        if (has_va) {
            std::string va_str = va_match[1].str();
            char* endptr = nullptr;
            va_dump = strtoull(va_str.c_str(), &endptr, 16);
            if (endptr == va_str.c_str() || *endptr != '\0') {
                va_dump = 0;
            }
        }
        
        // Calculate runtime address
        // Priority: use offset (RVA) if available, as it's more reliable
        uint64_t runtime_va = 0;
        if (offset > 0 && il2cpp_base > 0) {
            runtime_va = il2cpp_base + offset;
            LOGI("Calculated runtime VA from offset: 0x%" PRIx64 " = 0x%" PRIx64 " + 0x%" PRIx64,
                 runtime_va, il2cpp_base, offset);
        } else if (va_dump > 0 && il2cpp_base > 0) {
            // If we only have VA, try to calculate offset from dump VA
            // The dump VA is relative to dump base, we need to find dump base
            // For now, skip methods without offset as they're unreliable
            LOGW("Skipping %s: has VA but no offset, cannot calculate runtime address reliably",
                 method_name.c_str());
            continue;
        } else {
            LOGW("Skipping %s: missing offset/VA or il2cpp_base not initialized",
                 method_name.c_str());
            continue;
        }
        
        // Verify address is in reasonable range (should be close to il2cpp_base)
        if (runtime_va < il2cpp_base || runtime_va > il2cpp_base + 0x10000000) {
            LOGW("Skipping %s: calculated VA 0x%" PRIx64 " seems invalid (base: 0x%" PRIx64 ")",
                 method_name.c_str(), runtime_va, il2cpp_base);
            continue;
        }
        
        HookInfo* info = &mail_hooks[mail_hook_count];
        info->method_name = strdup(method_name.c_str());
        
        // Try to find class and namespace (search backward from method)
        size_t backward_start = pos > 2000 ? pos - 2000 : 0;
        std::string backward_context = json_content.substr(backward_start, pos - backward_start);
        
        std::smatch class_match, ns_match;
        // Search for the most recent class name before this method
        std::sregex_iterator class_iter(backward_context.begin(), backward_context.end(), class_pattern);
        std::sregex_iterator class_end;
        std::string found_class = "Unknown";
        for (; class_iter != class_end; ++class_iter) {
            found_class = class_iter->str(1);
        }
        info->class_name = strdup(found_class.c_str());
        
        // Search for namespace (take the last match)
        std::sregex_iterator ns_iter(backward_context.begin(), backward_context.end(), namespace_pattern);
        std::sregex_iterator ns_end;
        std::string found_ns = "";
        for (; ns_iter != ns_end; ++ns_iter) {
            found_ns = ns_iter->str(1);
        }
        info->namespace_name = strdup(found_ns.c_str());
        
        info->va = runtime_va;
        info->offset = offset;
        
        // Assign hook function based on method name
        if (method_name.find("GetReward") != std::string::npos) {
            info->hook_func = (void*)mail_get_reward_hook;
        } else if (method_name.find("Read") != std::string::npos) {
            info->hook_func = (void*)mail_read_hook;
        } else if (method_name.find("Delete") != std::string::npos) {
            info->hook_func = (void*)mail_delete_hook;
        } else {
            info->hook_func = (void*)mail_get_reward_hook; // Default
        }
        
        info->hooked = false;
        mail_hook_count++;
        found++;
        
        LOGI("Found mail method: %s::%s::%s at runtime 0x%" PRIx64 " (offset: 0x%" PRIx64 "%s)",
             info->namespace_name, info->class_name, 
             info->method_name, info->va, info->offset,
             va_dump > 0 ? ", dump VA available" : "");
    }
    
    LOGI("Parsed %d mail-related methods", mail_hook_count);
    return mail_hook_count > 0;
}

bool mail_hook_init(void* il2cpp_handle, const char* script_json_path) {
    if (!hook_init(il2cpp_handle)) {
        return false;
    }
    
    // Parse script.json to find mail methods
    if (!parse_mail_methods(script_json_path)) {
        LOGW("No mail methods found in script.json, using default hooks");
        // You can add default hooks here if needed
    }
    
    // Install all hooks
    int installed = 0;
    for (int i = 0; i < mail_hook_count; i++) {
        if (hook_install(&mail_hooks[i])) {
            installed++;
        }
    }
    
    LOGI("Installed %d/%d mail hooks", installed, mail_hook_count);
    return installed > 0;
}

// Hook callbacks implementation
void on_mail_received(void* mail_obj) {
    LOGI(">>> Mail Received Callback");
    LOGI("Mail Object: %p", mail_obj);
    // TODO: Extract mail information using IL2CPP API
}

void on_mail_read(void* mail_id) {
    LOGI(">>> Mail Read Callback");
    LOGI("Mail ID: %p", mail_id);
    // TODO: Log mail read event
}

void on_mail_reward_received(void* mail_id, void* reward_list) {
    LOGI(">>> Mail Reward Received Callback");
    LOGI("Mail ID: %p", mail_id);
    LOGI("Reward List: %p", reward_list);
    // TODO: Extract reward information
}

void on_mail_deleted(void* mail_id) {
    LOGI(">>> Mail Deleted Callback");
    LOGI("Mail ID: %p", mail_id);
    // TODO: Log mail deletion
}

void on_mail_sent(void* mail_data) {
    LOGI(">>> Mail Sent Callback");
    LOGI("Mail Data: %p", mail_data);
    // TODO: Log mail sending
}

