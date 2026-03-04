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
    // Look for patterns like: "name": "GetMailReward", "va": "0x..."
    std::regex method_pattern("\"name\"\\s*:\\s*\"([^\"]*(?:Mail|GetReward|ReadMail|DeleteMail|ReceiveMail)[^\"]*)\"");
    std::regex va_pattern("\"va\"\\s*:\\s*\"([^\"]+)\"");
    std::regex class_pattern("\"name\"\\s*:\\s*\"([^\"]*(?:Mail|MailManager)[^\"]*)\"");
    std::regex namespace_pattern("\"namespace\"\\s*:\\s*\"([^\"]+)\"");
    
    std::sregex_iterator iter(json_content.begin(), json_content.end(), method_pattern);
    std::sregex_iterator end;
    
    int found = 0;
    for (; iter != end && mail_hook_count < 32; ++iter) {
        std::string method_name = iter->str(1);
        
        // Check if it's a mail-related method
        if (method_name.find("Mail") != std::string::npos ||
            method_name.find("GetReward") != std::string::npos ||
            method_name.find("Read") != std::string::npos ||
            method_name.find("Delete") != std::string::npos ||
            method_name.find("Receive") != std::string::npos) {
            
            // Find VA near this method name (look forward in the JSON)
            size_t pos = iter->position();
            size_t search_start = pos;
            size_t search_end = std::min(pos + 500, json_content.length());
            std::string context = json_content.substr(search_start, search_end - search_start);
            
            std::smatch va_match;
            if (std::regex_search(context, va_match, va_pattern)) {
                std::string va_str = va_match[1].str();
                uint64_t va = 0;
                // Parse hex string without exceptions (exceptions disabled)
                char* endptr = nullptr;
                va = strtoull(va_str.c_str(), &endptr, 16);
                if (endptr == va_str.c_str() || *endptr != '\0') {
                    LOGW("Failed to parse VA: %s", va_str.c_str());
                    continue;
                }
                
                HookInfo* info = &mail_hooks[mail_hook_count];
                info->method_name = strdup(method_name.c_str());
                
                // Try to find class and namespace (search backward from method)
                size_t backward_start = pos > 2000 ? pos - 2000 : 0;
                std::string backward_context = json_content.substr(backward_start, pos - backward_start);
                
                std::smatch class_match, ns_match;
                // Search for the most recent class name before this method
                // Use reverse search by finding all matches and taking the last one
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
                
                info->va = va;
                
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
                
                LOGI("Found mail method: %s::%s::%s at 0x%" PRIx64,
                     info->namespace_name, info->class_name, 
                     info->method_name, info->va);
            }
        }
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

