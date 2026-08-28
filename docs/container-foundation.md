# 容器与资源基础设施设计

## 目标

本设计在全项目 OOP 重构之前建立统一的视图、字节所有权和类型化连续存储。
它解决当前 `bytes::Buffer` 同时承担字节流、类型数组和资源句柄三种职责的问题，并为
后续 `Lexer`、`Parser`、`SemanticSession`、`Assembler` 和 `Linker` 类提供稳定底座。

当前实现基线为 443 项测试全部通过，stage-next 与 stage-fixed 全部产物逐字节一致。

## C++接口对齐

标准库公开接口以 C++标准库为命名和行为基准：

- 模块使用 `luna.std.vector`、`luna.std.span`、`luna.std.utility`和`luna.std.memory`；
- 类型和方法使用 lowercase：`vector<Value>`、`span<Value>`、
  `byte_buffer`、`push_back`、`size`、`capacity`、`data`、`reserve`和`clear`；
- 真正的值/错误和类型保留给未来的 `luna.std.expected`，本阶段不发明 `result`替代品；
- Luna 没有异常、迭代器、concepts 和下标操作符时，接口必须记录差异，不能伪造支持。

编译器内部类采用 LLVM/Clang 风格的领域对象命名，不把标准库 lowercase 规则机械套用
到 Lexer、Parser、SemanticSession、Assembler 等工具链类型。

## 当前事实

- 本批次之前的生产源码没有 class，只有 `utility::move<Value>` 一个泛型声明；本批次新增的
  `byte_buffer`、`span<Value>` 和 `vector<Value>` 是首组生产级类与泛型类。
- `bytes::Buffer` 在生产源码中出现约 368 次。
- 从字节地址到类型指针的显式转换约 384 次。
- append/reserve/release 类操作约 249 次。
- 公开接口中约有 41 个重复的 `Value + runtime::Error` 结果结构。
- M5 已实测支持具体泛型类的构造、移动构造、析构、类组合和 bound method。
- M5 已实测支持泛型类中的 typed pointer、`sizeof(Value)`、扩容和 move-only RAII。

## 当前能力边界

M5 能安全表达容器自身的所有权，但还没有公开的动态非平凡元素生命周期原语：

- 没有 allocation expression；
- 没有面向原始地址的原地构造操作；
- 没有显式 `destroy_at`；
- 对原始内存赋值不等价于开始非平凡对象生命周期。

因此本阶段实现两类容器：

1. 非泛型字节所有者 `byte_buffer`；
2. 仅接受可平凡重定位类型的 `vector<Value>`。

完整的非平凡 `vector<Value>`不是本阶段前置条件。出现真实消费者时，再单独设计原地
构造、显式销毁和失败回滚，不把未经证明的对象生命周期隐藏在容器内部。

## 模块与文件

```text
library/include/luna/std/span.lh      # interface-only，导出泛型实现
library/include/luna/std/buffer.lh
library/src/std/buffer.la
library/include/luna/std/vector.lh    # interface-only，导出泛型实现
```

模块名分别为：

- `luna.std.span`
- `luna.std.buffer`
- `luna.std.vector`

泛型导出体必须位于接口，这是当前 M4/M5 跨模块单态化契约的必要例外；不能为了形式上
分离声明与实现，把泛型 `impl` 移入消费者不可见的 `.la`。

本阶段不创建单文件目录。每个路径都直接注册到 `LIBRARIES`；两个纯泛型模块登记为
interface-only。

## `span<Value>`与`const_span<Value>`

两个类型都是非拥有泛型值类，布局为一个指针和一个元素计数，不分配、不释放。

共同操作：

- `init(pointer, count)`
- `is_valid() const`
- `empty() const`
- `size() const`
- `data()`
- `get(index)`，带“index < size”前置条件
- `subspan(offset, count)`，按 C++约定要求调用方提供有效范围

`span<Value>`暴露可写引用和可写指针，并可转换成 `const_span<Value>`；
`const_span<Value>`只暴露只读形式。

当前 Luna 不支持 `operator []`，因此接口使用 `get`，不伪造不存在的语言能力。
越界检查版本可在真实调用需求出现时增加 `at`，本阶段不引入异常或隐藏 trap。

## `byte_buffer`

`byte_buffer`是非泛型、move-only、RAII 字节类。扩容与内存搬迁实现只编译一次，避免导出
泛型体在每个消费模块重复生成完整算法。

私有字段：

- `data_pointer: *u8`
- `size_bytes: usize`
- `capacity_bytes: usize`

不变量：

- 空状态严格为 null/0/0；
- 非空状态指针非 null，`size_bytes <= capacity_bytes`；
- 容量是实际 mmap 长度；
- 失败操作保留可析构的有效状态；
- move 后源对象回到唯一空状态。

操作：

- 默认构造、move 构造、move assignment、析构；
- `reserve`、`append`、`push_back`、`truncate`、`clear`；
- `size`、`capacity`、`empty`；
- 可写与只读数据指针；首轮 anchor 提升后再增加 span 适配；
- 显式 `release()`返回错误，析构只负责兜底释放。

扩容保留现有能力：

- 所有加法和乘法先做溢出检查；
- 初始容量和倍增策略集中为具名常量；
- append 支持源区间指向自身存储；
- reserve 成功但后续写入失败时，长度不提前增长；
- 空对象 release 成功。

move assignment 和析构无法返回 munmap 错误。其前提是对象只保存本模块创建的有效映射；
对这种映射的 munmap 失败属于运行时不变量破坏。需要观测错误的调用方使用显式
`release()`。

## `@is_trivially_relocatable(Type)`

这是编译期类型查询，结果为 bool，可用于函数内 compile-time `assert`。它复用当前
编译器已有的递归特殊成员分类：

- 标量、指针、引用和只含平凡字段的记录为 true；
- 平凡元素数组递归为 true；
- 声明析构、复制/移动构造或赋值的类为 false；
- 含非平凡基类或字段的聚合为 false；
- 非法、不完整或递归超限类型诊断失败。

名称使用 relocatable 而不是 copyable，因为容器依赖的是“字节搬迁后直接放弃旧存储”
这一更具体的性质。

该类型查询先作为语言能力独立落地并提升 anchor；只有新 anchor 能识别它后，标准库
泛型接口才允许采用。

## `vector<Value>`

`vector<Value>`是 move-only 泛型类，通过类组合持有一个 `byte_buffer`。接口体包含：

```luna
assert(@is_trivially_relocatable(Value));
```

因此非平凡类型在实例化阶段确定性失败，不允许静默字节搬迁资源对象。

操作：

- 默认构造、move 构造、move assignment、析构；
- `reserve(element_capacity)`；
- `push_back(value)`；
- `truncate(element_count)`、`clear()`；
- `size()`、`capacity()`、`empty()`；
- `get(index)`可写/只读重载；
- `as_span()`可写/只读重载。

所有元素容量到字节容量的转换都检查 `count * sizeof(Value)`。底层 mmap 地址满足目标
最大 4096 字节对齐，而 Luna 类型大小按自身对齐向上取整，因此连续元素地址保持对齐。

泛型方法只负责类型换算和引用接口；扩容、自别名处理和字节搬迁全部委托给非泛型
`byte_buffer`，控制单态化代码尺寸和自举编译时间。

## 为什么暂不实现完整的非平凡 `vector<Value>`

完整容器至少需要：

- 在未初始化地址上选择平凡、copy 或 move 构造；
- 逆序显式销毁已构造元素；
- 扩容时逐元素迁移并放弃旧生命周期；
- 中途失败时只回滚已经构造的前缀；
- erase/insert 时正确处理重叠和 move assignment。

现有内部 `lower_special_construction`和`emit_destroy_object`已经具备主要 lowering 能力，
未来可以收敛成经过单独评审的原地生命周期操作。但在没有第一个真实非平凡动态集合
消费者前，不扩展语言表面。

## 迁移策略

新基础设施与旧 `luna.std.bytes`暂时并存。禁止用 forwarding facade 长期维持两套 API。

迁移顺序：

1. 独立落地 span/byte_buffer 和测试；
2. 独立落地类型查询，完成 verify/test 后提升 anchor；
3. 用新 anchor 落地 vector；
4. 先迁移只保存被动记录的类型表、IR、class/generic stores；
5. 在 OOP 重构 Lexer/Parser/SemanticSession 时迁移其记录池；
6. 迁移 ELF/object/assembler 的字节流和记录数组；
7. 删除最后一个旧 `bytes::Buffer`调用后删除 `luna.std.bytes`。

每个子系统必须原子迁移：同一容器不能同时维护旧字节长度和新元素计数。

### 首个编译器采用批次：初始化器下标集合

`semantic/expr/initializer.la`中的三套“已见字段/数组下标”具有同一职责：保存唯一的
`usize`标识、检测重复并报告分配失败。旧实现分别维护`bytes::Buffer`，重复进行裸指针
转换、`length / sizeof(usize)`换算、手工扫描和显式释放。

本批次将它们收敛为文件私有的`InitializerIndexSet`：

- 类型是 class，因为它维护“内部序列不含重复值”的稳定不变量；
- 私有存储组合`vector<usize>`，构造和析构完全依赖 RAII；
- `insert`返回被动的`InitializerInsertResult`，区分“重复”与真实运行时错误；
- `size`返回元素数量，调用端不再观察字节长度或底层地址；
- 仍采用稀疏线性查找，空间为 O(k)，避免按可能极大的数组长度分配 O(n) 位图；后续只有
  在真实大型初始化器基准证明需要时，才替换为有序或哈希策略。

该类只服务一个实现单元，不形成独立模块或文件。它使用泛型、类组合、访问控制、构造、
析构和 const 方法；对象不转移，因此不声明 copy/move；没有替换关系、动态分派、外部协作
或回调状态，因此继承、虚函数、RTTI、friend、bound method、操作符和默认参数均不适用。

## 测试与成本门禁

必须覆盖：

- span 空区间、切片、可写/只读引用；
- byte_buffer 空状态、增长、自 append、重叠 append、truncate、move、显式 release 和析构；
- vector scalar、enum、普通 struct、4096 对齐记录和 move；
- 非平凡类型实例化 vector 的确定性失败；
- 容量加法、乘法和倍增溢出；
- 跨模块泛型实例化和实现单元顺序确定性；
- stage-next/stage-fixed 全产物一致。

在 `caw`记录基础设施采用前后的：

- stage 构建时间；
- 最终 `luna`文件大小；
- 大型 sem_funcs 汇编阶段耗时；
- 泛型实例数量和新增对象大小。

若泛型包装导致明显代码膨胀，优先继续把算法下沉到非泛型 byte_buffer，而不是回退到裸字节
数组和手工类型转换。

## 实现验证与成本

基础设施初始快照在 `caw` 的 x86-64 WSL2 环境完成验证：

- `audit`确认 80 个模块、1 个统一驱动和 74 个驱动闭包对象一致；
- `refmt.py --check`为 0 个待格式化文件、0 个 token 漂移；
- `stage-next`与`stage-fixed`的全部汇编、对象和`bin/luna`逐字节一致；
- 完整测试为 438 passed、0 failed、0 skipped；
- 新 anchor 为 4,536,150 字节，相比此前的 4,507,428 字节增加 28,722 字节，约 0.64%；
- `buffer.lo`为 36,934 字节；`span`和`vector`是 interface-only，当前生产工具链没有新增
  `vector<Value>`单态化实例；
- 三阶段固定点产物的文件时间窗为 1,578.0 秒；3,068,293 字节的`sem_funcs.s`单次汇编
  为 133.53 秒，峰值 RSS 为 5,768 KiB，产物与固定点`sem_funcs.lo`逐字节一致。

当前抽象没有引入生产泛型代码膨胀，所有扩容与搬迁算法只存在于非泛型`byte_buffer`。
`sem_funcs.s`的汇编耗时仍是后续优化阶段应持续观察的重负载，但不属于本批次容器接口的
新增成本。

首个编译器采用批次继续在同一环境验证：

- 普通类方法收集跳过已经提前布局的具体泛型类实例；这些实例只在激活其类型参数后通过
  `instantiate_generic_class`惰性物化方法，修复 imported generic class 作为类字段时的
  `unknown_type`错误；
- `semantic/expr/initializer.la`以一个`InitializerIndexSet`实例替代每条路径中的裸字节
  缓冲、指针转换、字节长度换算和显式释放；
- 驱动闭包为 75 个对象，完整测试为 439 passed、0 failed、0 skipped；
- `stage-next`与`stage-fixed`全部产物逐字节一致，最终 anchor 为 4,556,630 字节，较基础
  设施 anchor 增加 20,480 字节，约 0.45%；
- 最终`sem_expr_initializer.lo`为 249,946 字节；同一源码的临时局部-vector 引导版本为
  246,476 字节，类边界增加 3,470 字节；
- 最终三阶段固定点产物时间窗为 2,125.2 秒，`sem_funcs.s`为 3,070,612 字节。

这里保留稀疏 O(k) 存储与 O(k²) 重复检测，避免为长度可能极大的稀疏数组初始化器分配
O(n) 位图。若真实基准显示该路径成为瓶颈，应在`InitializerIndexSet`内部替换查找策略，
不重新把存储细节泄漏给 lowering 调用端。

## 最新特性采用说明

本基础设施使用：泛型类、类组合、访问控制、构造、析构、move 构造、move assignment、
引用、receiver const 重载、操作符赋值和 compile-time assert。

不使用继承、虚函数、RTTI、friend 或 bound method：容器没有运行时替换关系，也没有
需要携带接收者的策略回调。这里不用这些特性是领域选择，不是保留过程式实现。
