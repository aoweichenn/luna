# `string` 与 `charconv` 基础设施

## 职责边界

历史 backend 字节拼接模块同时承担字符所有权、整数转换、十六进制填充和 sticky error，
却没有流位置、sink、缓冲策略或多态写入目标。它既不是 C++ stream，也不属于 x86 领域。

当前边界直接对应现代 C++：

- `luna.std.string_view::string_view`对应非拥有的`std::string_view`；
- `luna.std.string::string`对应拥有型、连续、NUL 结尾的`std::string`；
- `luna.std.charconv::to_chars`对应无 locale、无分配的`std::to_chars`；
- codegen 自己负责“固定宽度十六进制”和 8 MiB 汇编上限，因为它们是后端策略；
- drivers 负责诊断格式和临时路径格式，因为它们是命令协议。

因此没有 `output`、`stream`、`builder`或一次转发的 backend module。

## `string_view` 契约

`string_view`是透明的 pointer/length 被动视图，不拥有资源。当前编译器要求源码、符号和路径
视图是合法 UTF-8，因此`from_bytes`、`from_c_string`和`substr`返回
`string_view_result`并完成验证；`is_valid_utf8`与`equal`是无分配算法。这一点比 C++ 原始
字节语义更严格，但类型名、非拥有布局和`substr`命名保持一致。

它不保护可变状态，也没有生命周期行为，所以使用透明 struct 比装饰性 class 更直接；
拥有、扩容和析构只属于`string`。

## `string` 契约

`string`是 move-only RAII class，私有组合`byte_buffer`。它提供 C++ 风格的`empty`、`size`、
`capacity`、`data`、`reserve`、`append`、`push_back`和`clear`。所有成功状态满足：

- `[data(), data() + size())`是连续字符区间；
- `data()[size()] == 0`，终止字节不计入`size()`或`capacity()`；
- 空字符串仍提供可读取的 NUL sentinel；
- move 后源字符串回到有效空状态；
- append 接受自身区间，扩容后仍从正确的重定位地址复制；
- 失败的 append 恢复原长度与 NUL 终止不变量。

Luna 当前没有异常和分配器协议，因此分配型操作返回`runtime::Error`；没有可表达分配失败的
隐式 copy constructor，所以首版有意不提供 C++ 的深复制语义。需要复制时应在未来以
`expected<string, Error>`承载显式失败，不能把失败藏进半有效对象。

`detach()`只服务旧`bytes::Buffer`结果的原子迁移：调用端先读取 size/capacity，再把返回地址
立即装入唯一旧资源句柄。它不是推荐的标准库字符串操作；最后一个旧 buffer 消费者迁移后
应随适配边界删除。

## `charconv` 契约

`to_chars(first, last, value, base = 10)`支持`u64`、`i64`、`usize`和`isize`精确重载：

- base 范围是 2 到 36；
- 字母数字使用小写；
- 成功返回第一个未写位置；
- 空间不足返回`runtime::Error.no_space`和`last`；
- 无分配、无 locale、无 NUL 追加；
- 最小有符号值通过无溢出的 magnitude 变换处理。

`runtime::Error`代替 C++ 的`std::errc`是当前统一错误域造成的明确差异。接口仍保留
`to_chars_result { ptr, error }`形状，调用端无需扫描结果或维护手写倒序转换。

## 最新语言特性审查

- class、访问控制、组合、构造、析构、move construction 和 move assignment 用于字符串所有权；
- `string_view`是允许公开布局的透明非拥有值，不伪装成资源 class；
- bound method 用于所有字符串查询和修改，调用端不再传递公开 state struct；
- overload/default argument 用于`append`和四个`to_chars`整数族；
- move assignment 是唯一自然 operator；没有适合字符串边界的额外 DSL operator；
- `charconv`是纯算法，没有稳定对象状态，保留 free function；
- 没有运行期替换、层次或类型发现需求，因此 inheritance、virtual dispatch 和 RTTI 不适用；
- 不需要越过私有边界的协作者，因此 friend 不适用；
- generic 不会改善固定的内建整数转换集合，且当前 generic specialization 不能代替有符号
  magnitude 差异，因此使用精确 overload 而不是模板分支。

## 性能与验证要求

codegen 最终通过`detach()`零拷贝转移汇编存储。`charconv`只使用最多 64 字节栈临时区。
本批次必须在 `caw` 保持固定点逐字节一致、443 项行为/负诊断全绿，并单独记录大型
`sem_funcs.s`的 compile 与 assemble 时间，确认通用字符串抽象没有造成可见回退。

最终 `caw` x86-64 WSL2 结果：audit 确认 75 modules、统一驱动闭包 65 objects；单套
stage-next build 为 86.73 秒，三阶段 verify 为 251.72 秒且所有产物逐字节一致；完整测试
443/443 通过，用时 20.21 秒。3,069,988 字节的`sem_funcs.s`由 stage-next 编译生成用时
10.00 秒，汇编用时 0.75 秒，assembler 热路径与迁移前持平。
