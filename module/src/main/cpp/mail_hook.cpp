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
    
    // Network communication hook - intercept all network requests/responses
    void* network_send_hook(void* self, void* handler, void* content, ...) {
        LOGI("=== Network Send Hook ===");
        LOGI("Self: %p", self);
        
        // Check if this is a mail-related request
        if (handler) {
            // handler is usually a string (RPC name)
            // We need to check if it contains "Mail" or mail-related keywords
            // Note: This is a simplified check - actual implementation may need to use IL2CPP API
            LOGI("Handler: %p", handler);
        }
        
        if (content) {
            LOGI("Content: %p (size: ?)", content);
            // TODO: Parse content to check if it's mail-related
        }
        
        // Call original function (would need to restore and call)
        // For now, return nullptr
        return nullptr;
    }
    
    void* network_call_hook(void* self, void* handler, void* content, void* reply, ...) {
        LOGI("=== Network Call Hook ===");
        LOGI("Self: %p", self);
        LOGI("Handler: %p", handler);
        LOGI("Content: %p", content);
        LOGI("Reply callback: %p", reply);
        
        // TODO: Check if handler contains mail-related keywords
        // TODO: Hook reply callback to intercept mail responses
        
        return nullptr;
    }
    
    void* network_process_message_hook(void* self, void* msg, ...) {
        LOGI("=== Network ProcessMessage Hook ===");
        LOGI("Self: %p", self);
        LOGI("Message: %p", msg);
        
        // TODO: Parse message to check if it's mail-related response
        // TODO: Extract mail data from response
        
        return nullptr;
    }
    
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
    std::regex class_pattern("\"name\"\\s*:\\s*\"([^\"]*(?:Mail|MailManager|Network|XBaseNetwork|XNetwork)[^\"]*)\"");
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
        
        // Filter out constants (they usually don't have offset/VA and are all caps or have "Manager" in name)
        // Constants like "MailManagerGetMailFail" are usually error codes, not methods
        if (method_name.find("Manager") != std::string::npos && 
            method_name.length() > 15) { // Long names with Manager are usually constants
            // Check if it's all caps or has pattern like "XxxManagerXxxXxx"
            bool looks_like_constant = false;
            if (method_name.find("Manager") != std::string::npos) {
                // Check if it follows constant naming pattern (starts with class name)
                size_t manager_pos = method_name.find("Manager");
                if (manager_pos > 0 && manager_pos < method_name.length() - 10) {
                    // Pattern: ClassManagerMethodName - likely a constant
                    looks_like_constant = true;
                }
            }
            if (looks_like_constant) {
                continue; // Skip constants
            }
        }
        
        // Find the class name for this method (search backward)
        size_t pos = iter->position();
        size_t backward_start = pos > 2000 ? pos - 2000 : 0;
        std::string backward_context = json_content.substr(backward_start, pos - backward_start);
        
        std::smatch class_match, ns_match;
        std::sregex_iterator class_iter(backward_context.begin(), backward_context.end(), class_pattern);
        std::sregex_iterator class_end;
        std::string found_class = "Unknown";
        for (; class_iter != class_end; ++class_iter) {
            found_class = class_iter->str(1);
        }
        
        std::sregex_iterator ns_iter(backward_context.begin(), backward_context.end(), namespace_pattern);
        std::sregex_iterator ns_end;
        std::string found_ns = "";
        for (; ns_iter != ns_end; ++ns_iter) {
            found_ns = ns_iter->str(1);
        }
        
        // Exclude .NET framework classes (System.Net.Mail, System.Net.Sockets, etc.)
        if (found_ns.find("System.Net") != std::string::npos ||
            found_ns.find("System.") == 0) {
            continue; // Skip .NET framework methods
        }
        
        // Exclude Unity/UnityEngine classes
        if (found_ns.find("Unity") != std::string::npos) {
            continue;
        }
        
        // Priority 1: MailManager class methods (highest priority)
        bool is_mail_manager_method = false;
        if (found_class.find("MailManager") != std::string::npos) {
            // MailManager class - hook methods but exclude constants
            // Constants: MailManagerGetMailFail, MailManagerMailExist (long, descriptive)
            // Methods: GetMail, ReadMail, DeleteMail (shorter, action verbs)
            if (method_name[0] >= 'A' && method_name[0] <= 'Z') {
                // Exclude constants (long names starting with class name pattern)
                if (method_name.find("MailManager") == 0 && method_name.length() > 18) {
                    // This is likely a constant like "MailManagerGetMailFail"
                    continue;
                }
                // Include actual methods (shorter, action-based names)
                if (method_name.find("Get") == 0 || 
                    method_name.find("Read") == 0 ||
                    method_name.find("Delete") == 0 ||
                    method_name.find("Send") == 0 ||
                    method_name.find("Receive") == 0 ||
                    method_name.find("On") == 0 ||
                    method_name.length() < 18) { // Methods are usually shorter than 18 chars
                    is_mail_manager_method = true;
                }
            }
        }
        
        // Priority 2: Mail-related classes (XMail, MailData, etc.)
        bool is_mail_class_method = false;
        if (!is_mail_manager_method && 
            (found_class.find("Mail") != std::string::npos && 
             found_class != "MailAddress" && // Exclude .NET MailAddress
             found_class.find("MailManager") == std::string::npos)) {
            // Mail-related class - hook methods with mail-related names
            if (method_name[0] >= 'A' && method_name[0] <= 'Z') {
                if (method_name.find("Mail") != std::string::npos ||
                    method_name.find("GetReward") != std::string::npos ||
                    method_name.find("Read") != std::string::npos ||
                    method_name.find("Delete") != std::string::npos ||
                    method_name.find("Receive") != std::string::npos ||
                    method_name.find("Send") != std::string::npos) {
                    is_mail_class_method = true;
                }
            }
        }
        
        // Priority 3: Network communication methods (hook network layer for mail requests/responses)
        bool is_network_method = false;
        if (found_class.find("Network") != std::string::npos || 
            found_class.find("XBaseNetwork") != std::string::npos ||
            found_class.find("XNetwork") != std::string::npos) {
            // Hook network communication methods
            if (method_name == "Send" ||
                method_name == "Call" ||
                method_name == "ProcessMessage" ||
                method_name == "DealMessage" ||
                method_name == "OnMessage" ||
                method_name == "OnReceive" ||
                method_name.find("Send") == 0 ||
                method_name.find("Call") == 0 ||
                method_name.find("Process") == 0 ||
                method_name.find("Handle") == 0) {
                is_network_method = true;
            }
        }
        
        // Priority 4: Methods with mail-related names in any class
        bool is_mail_named_method = false;
        if (!is_mail_manager_method && !is_mail_class_method && !is_network_method) {
            if (method_name[0] >= 'A' && method_name[0] <= 'Z') {
                // More specific patterns for game mail methods
                if (method_name.find("GetMailReward") != std::string::npos ||
                    method_name.find("ReadMail") != std::string::npos ||
                    method_name.find("DeleteMail") != std::string::npos ||
                    method_name.find("ReceiveMail") != std::string::npos ||
                    method_name.find("SendMail") != std::string::npos ||
                    method_name.find("OnMail") != std::string::npos ||
                    (method_name.find("Mail") != std::string::npos && 
                     method_name.length() > 8)) { // Longer names are more likely to be actual methods
                    is_mail_named_method = true;
                }
            }
        }
        
        bool is_mail_method = is_mail_manager_method || is_mail_class_method || is_network_method || is_mail_named_method;
        
        if (!is_mail_method) {
            continue;
        }
        
        // Find offset and VA near this method name (look forward in the JSON)
        // IMPORTANT: Only process methods that have offset/VA (actual executable methods)
        // Constants don't have offset/VA, so this filters them out automatically
        size_t search_start = pos;
        size_t search_end = std::min(pos + 500, json_content.length());
        std::string context = json_content.substr(search_start, search_end - search_start);
        
        std::smatch offset_match, va_match;
        bool has_offset = std::regex_search(context, offset_match, offset_pattern);
        bool has_va = std::regex_search(context, va_match, va_pattern);
        
        // Skip if no offset/VA - this means it's a constant or non-executable symbol
        if (!has_offset && !has_va) {
            continue; // Skip constants and non-executable symbols
        }
        
        // Now we know it's an actual method, log it
        LOGI("Processing mail method candidate: %s::%s::%s", 
             found_ns.c_str(), found_class.c_str(), method_name.c_str());
        
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
            LOGW("Skipping %s::%s::%s: has VA but no offset, cannot calculate runtime address reliably",
                 found_ns.c_str(), found_class.c_str(), method_name.c_str());
            continue;
        } else {
            LOGW("Skipping %s::%s::%s: missing offset/VA or il2cpp_base not initialized",
                 found_ns.c_str(), found_class.c_str(), method_name.c_str());
            continue;
        }
        
        // Verify address is in reasonable range (should be close to il2cpp_base)
        if (runtime_va < il2cpp_base || runtime_va > il2cpp_base + 0x10000000) {
            LOGW("Skipping %s::%s::%s: calculated VA 0x%" PRIx64 " seems invalid (base: 0x%" PRIx64 ")",
                 found_ns.c_str(), found_class.c_str(), method_name.c_str(), runtime_va, il2cpp_base);
            continue;
        }
        
        HookInfo* info = &mail_hooks[mail_hook_count];
        info->method_name = strdup(method_name.c_str());
        info->class_name = strdup(found_class.c_str());
        info->namespace_name = strdup(found_ns.c_str());
        
        info->va = runtime_va;
        info->offset = offset;
        
               // Assign hook function based on method name and class
               if (found_class.find("Network") != std::string::npos) {
                   // Network communication methods
                   if (method_name == "Send" || method_name.find("Send") == 0) {
                       info->hook_func = (void*)network_send_hook;
                   } else if (method_name == "Call" || method_name.find("Call") == 0) {
                       info->hook_func = (void*)network_call_hook;
                   } else if (method_name == "ProcessMessage" || method_name.find("Process") == 0) {
                       info->hook_func = (void*)network_process_message_hook;
                   } else {
                       info->hook_func = (void*)network_process_message_hook; // Default for network
                   }
               } else {
                   // Mail-specific methods
                   if (method_name.find("GetReward") != std::string::npos) {
                       info->hook_func = (void*)mail_get_reward_hook;
                   } else if (method_name.find("Read") != std::string::npos) {
                       info->hook_func = (void*)mail_read_hook;
                   } else if (method_name.find("Delete") != std::string::npos) {
                       info->hook_func = (void*)mail_delete_hook;
                   } else {
                       info->hook_func = (void*)mail_get_reward_hook; // Default
                   }
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

