# 邮件系统Hook功能 - 改动说明

## 概述

本次二次开发为Zygisk-Il2CppDumper项目添加了邮件系统Hook功能，可以在游戏运行时hook邮件相关的方法并记录日志。

## 新增文件

### 核心Hook框架
1. **`module/src/main/cpp/hook.h`** - Hook框架头文件
   - 定义HookInfo结构体
   - Hook系统初始化、安装、移除函数

2. **`module/src/main/cpp/hook.cpp`** - Hook框架实现
   - 支持ARM、ARM64、x86、x86_64架构的inline hook
   - 内存保护处理
   - 指令缓存清除

### 邮件Hook实现
3. **`module/src/main/cpp/mail_hook.h`** - 邮件Hook头文件
   - 邮件Hook回调函数声明
   - Hook初始化函数

4. **`module/src/main/cpp/mail_hook.cpp`** - 邮件Hook实现
   - 从script.json解析邮件方法
   - Hook函数实现
   - 回调函数实现

### 文档
5. **`HOOK_README.md`** - Hook功能使用说明
6. **`hook_config_example.json`** - 配置示例文件
7. **`CHANGES.md`** - 本文件

## 修改的文件

### 1. `module/src/main/cpp/hack.cpp`
- 添加`mail_hook.h`头文件包含
- 在`hack_start()`函数中添加邮件Hook初始化
- 添加`<string>`头文件

### 2. `module/src/main/cpp/CMakeLists.txt`
- 添加`hook.cpp`和`mail_hook.cpp`到编译列表

## 功能特性

### 1. 自动方法发现
- 从`script.json`自动解析邮件相关方法
- 支持通过方法名、类名、命名空间匹配
- 自动提取VA地址

### 2. 多架构支持
- **ARM64**: 使用LDR + BR指令实现绝对地址跳转
- **ARM**: 使用LDR PC指令实现跳转
- **x86/x64**: 使用JMP rel32指令实现相对跳转

### 3. Hook的方法类型
- `GetMailReward` - 获取邮件奖励
- `ReadMail` / `Read` - 读取邮件
- `DeleteMail` / `Delete` - 删除邮件
- `ReceiveMail` / `Receive` - 接收邮件
- 其他包含"Mail"关键字的方法

### 4. 日志记录
- Hook安装日志
- 方法调用日志
- 回调函数日志

## 使用方法

### 1. 准备文件
确保以下文件存在：
- `/data/data/GamePackageName/files/dump.cs`
- `/data/data/GamePackageName/files/script.json`

### 2. 编译
```bash
./gradlew :module:assembleRelease
```

### 3. 安装和运行
- 在Magisk中安装模块
- 启动游戏
- 查看logcat日志：`adb logcat | grep "Perfare"`

## 技术实现细节

### Hook安装流程
1. 解析`script.json`查找邮件方法
2. 提取VA地址、类名、方法名
3. 分配hook函数
4. 修改内存保护
5. 安装hook指令
6. 清除指令缓存

### Hook函数签名
当前使用可变参数`void* hook_func(void* self, ...)`，实际方法签名可能不同，需要根据`dump.cs`调整。

### 内存管理
- Hook信息存储在静态数组中（最多32个）
- 使用`strdup`分配字符串，需要注意内存释放（当前未实现，因为模块生命周期内一直有效）

## 已知限制

1. **方法签名匹配**：当前使用可变参数，可能不完全匹配实际方法签名
2. **原始函数调用**：Hook函数未调用原始函数，如需保持原功能需要实现trampoline
3. **错误处理**：部分错误情况可能未完全处理
4. **内存泄漏**：`strdup`分配的内存未释放（模块生命周期内可接受）

## 后续改进建议

1. **实现Trampoline**：支持调用原始函数
2. **方法签名匹配**：根据`dump.cs`自动生成正确的hook函数签名
3. **配置文件支持**：支持通过配置文件指定要hook的方法
4. **数据提取**：使用IL2CPP API提取邮件和奖励数据
5. **文件日志**：支持将日志写入文件
6. **Hook移除**：实现完整的hook移除功能

## 测试建议

1. 在不同架构设备上测试（ARM、ARM64、x86、x86_64）
2. 测试不同游戏的邮件系统
3. 验证hook是否正确安装
4. 检查日志输出
5. 测试游戏稳定性

## 注意事项

1. Hook可能影响游戏性能
2. 某些游戏可能有反hook保护
3. 需要Root权限和Zygisk支持
4. 仅供学习和研究使用

## 许可证

与原项目保持一致。

