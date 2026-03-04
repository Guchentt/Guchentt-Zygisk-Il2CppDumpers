//
// Mail system hook definitions
//

#ifndef ZYGISK_IL2CPPDUMPER_MAIL_HOOK_H
#define ZYGISK_IL2CPPDUMPER_MAIL_HOOK_H

#include "hook.h"

// Mail hook callbacks
void on_mail_received(void* mail_obj);
void on_mail_read(void* mail_id);
void on_mail_reward_received(void* mail_id, void* reward_list);
void on_mail_deleted(void* mail_id);
void on_mail_sent(void* mail_data);

// Initialize mail hooks
bool mail_hook_init(void* il2cpp_handle, const char* script_json_path);

#endif //ZYGISK_IL2CPPDUMPER_MAIL_HOOK_H

