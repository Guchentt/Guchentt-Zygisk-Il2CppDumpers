# 邮件系统Hook使用说明

## 概述

本项目已扩展支持hook邮件系统功能。当游戏运行时，会自动hook邮件相关的方法，并记录日志。

## 功能特性

1. **自动Hook邮件方法**：从`script.json`自动解析并hook邮件相关方法
2. **多架构支持**：支持ARM、ARM64、x86、x86_64
3. **日志记录**：记录邮件接收、读取、删除、奖励领取等操作
4. **回调机制**：提供回调函数供自定义处理

## Hook的方法

系统会自动hook以下类型的邮件方法：
- `GetMailReward` - 获取邮件奖励
- `ReadMail` / `Read` - 读取邮件
- `DeleteMail` / `Delete` - 删除邮件
- `ReceiveMail` / `Receive` - 接收邮件
- 其他包含"Mail"关键字的方法

## 使用方法

### 1. 准备文件

确保以下文件存在于游戏数据目录：
- `/data/data/GamePackageName/files/dump.cs` - IL2CPP dump文件
- `/data/data/GamePackageName/files/script.json` - 方法信息JSON文件

### 2. 编译模块

```bash
./gradlew :module:assembleRelease
```

### 3. 安装模块

在Magisk中安装生成的ZIP包。

### 4. 运行游戏

启动游戏后，hook系统会自动：
1. 加载`libil2cpp.so`
2. 解析`script.json`查找邮件方法
3. 安装hook
4. 记录日志到logcat

### 5. 查看日志

```bash
adb logcat | grep "Perfare"
```

你会看到类似以下的日志：
```
I/Perfare: Found mail method: Namespace::MailManager::GetMailReward at 0x73297...
I/Perfare: Successfully hooked Namespace::MailManager::GetMailReward at 0x...
I/Perfare: === Mail GetReward Hook ===
I/Perfare: >>> Mail Reward Received Callback
```

## 自定义Hook

### 修改Hook回调

编辑 `mail_hook.cpp` 中的回调函数：

```cpp
void on_mail_reward_received(void* mail_id, void* reward_list) {
    LOGI(">>> Mail Reward Received Callback");
    // 添加你的自定义逻辑
    // 例如：修改奖励、记录到文件等
}
```

### 添加新的Hook方法

1. 在 `mail_hook.cpp` 中添加hook函数：
```cpp
void* your_custom_hook(void* self, ...) {
    LOGI("=== Your Custom Hook ===");
    // 你的逻辑
    return nullptr;
}
```

2. 在 `parse_mail_methods` 中分配hook函数：
```cpp
if (method_name.find("YourMethod") != std::string::npos) {
    info->hook_func = (void*)your_custom_hook;
}
```

## 技术细节

### Hook实现

- **ARM64**: 使用LDR + BR指令实现绝对地址跳转
- **ARM**: 使用LDR PC指令实现跳转
- **x86/x64**: 使用JMP rel32指令实现相对跳转

### 内存保护

Hook安装时会：
1. 计算页面边界
2. 使用`mprotect`使内存可写
3. 安装hook指令
4. 清除指令缓存

## 注意事项

1. **方法签名**：当前实现使用可变参数，实际方法签名可能不同，需要根据dump.cs调整
2. **原始函数调用**：当前hook函数没有调用原始函数，如果需要保持原功能，需要实现trampoline
3. **线程安全**：Hook在独立线程中安装，确保IL2CPP已初始化
4. **错误处理**：如果hook失败，会记录错误日志但不影响游戏运行

## 故障排除

### Hook未安装

检查：
1. `script.json`是否存在且格式正确
2. 日志中是否有"Found mail method"消息
3. VA地址是否有效

### 游戏崩溃

可能原因：
1. Hook函数签名不匹配
2. 内存保护问题
3. 指令缓存未清除

解决方法：
1. 检查dump.cs中的方法签名
2. 确保hook函数参数匹配
3. 添加更多错误检查

## 扩展功能

### 保存Hook数据到文件

在回调函数中添加文件写入：

```cpp
void on_mail_reward_received(void* mail_id, void* reward_list) {
    FILE* f = fopen("/data/data/GamePackageName/files/mail_log.txt", "a");
    if (f) {
        fprintf(f, "Mail ID: %p\n", mail_id);
        fclose(f);
    }
}
```

### 使用IL2CPP API提取数据

```cpp
void on_mail_received(void* mail_obj) {
    // 使用IL2CPP API提取邮件信息
    Il2CppClass* mail_class = il2cpp_object_get_class(mail_obj);
    const char* class_name = il2cpp_class_get_name(mail_class);
    LOGI("Mail class: %s", class_name);
}
```

## 许可证

与原项目相同。

