# x86-64 Assembler 设计

## 目标与边界

`luna.compiler.x86.assembler`把受控的 GNU 风格 x86-64 汇编子集编码为`Object`。本次重构
不扩展指令集、不改变 LUNAOBJ1/ELF 语义，也不增加模块；它把历史`State + *State`过程式
实现收敛成一个拥有完整汇编会话的私有`Assembler`类。

模块仍只有一个`.lh`，实现按职责分为：

```text
assembler/
  facade.la       # 生命周期、按行消费、结果转移
  operands.la     # 无状态 operand/register/memory 解析算法
  symbols.la      # SymbolName 与 SymbolTable
  rules.la        # 指令和条件码规则表
  encoding.la     # Assembler 编码成员方法
  source.la       # directive、label、fixup 与 validation 成员方法
```

这些文件共享同一模块私有声明，不形成`rules`、`state`或`encoding`子模块。

## 所有权与不变量

`Assembler`直接组合并拥有：

- 一个待构造的`object::Object`；
- 一个保持符号名唯一且 binding 地址稳定的`SymbolTable`；
- `vector<Fixup>`和`vector<NumericLabel>`两个类型化 RAII 序列；
- 初始化一次、只读使用的 87 条 instruction rule 和 30 条 condition rule；
- 当前 section、诊断 line 和 first-error 状态。

构造函数建立唯一有效空状态。`run()`只推进会话；`take_result()`在成功时零拷贝移交
`Object`并把内部存储重置为空，在失败时释放半成品并返回有效空对象。析构函数兜底释放
尚未移交的对象，组合字段随后按逆序自动析构。调用端不再观察 fixup 字节长度、不再转换
裸类型指针，也不再手工配对三套 release 路径。

`Fixup`保存指向 map 稳定 slot 的`SymbolBinding`非拥有指针。`SymbolTable`的生命周期覆盖
全部 fixup 解析，且字段声明/析构顺序保证 vectors 先于 symbol table 销毁。

## 规则与分派

历史 encoder 通过 87 段串行 mnemonic if 链和 30 段 condition if 链分派。当前构造函数把
这些闭合集合建立为数据表：查找是表循环，编码族由`EncodingKind`的`switch`选择。
同类指令共享 width、prefix、opcode、group 和方向参数，新增一条既有编码族指令只增加
一条记录。

规则表属于每个汇编会话的只读策略配置。规则字段使用精确的`u8`宽度，整个表保持在几
KiB 的单个 facade 栈对象内；不产生逐指令动态分配。当前线性查找与旧 if 链同为 O(k)，
后续只有基准证明查找成为热点时才引入排序或哈希索引。

directive、section、symbol kind、legacy register 和 condition alias 同样使用表加`for`；
闭合编码行为使用`switch`。只有按行消费和扫描换行符属于输入状态推进，保留`while`。

## 最新语言特性审查

- class、访问控制、构造、析构和 bound method：用于汇编会话及其 first-error/资源不变量；
- class composition：直接组合 SymbolTable 与两个 typed vector；
- generics：`vector<Fixup>`、`vector<NumericLabel>`和 SymbolTable 内的`map`消除裸字节容器；
- move：Object 在`take_result()`边界按现有被动记录契约转移，Assembler 本身不移动；
- overload/default：该边界没有同一语义族的可选参数，不增加装饰性 overload/default；
- operator：仅 SymbolName 保留 map 排序所需的自然`operator <`；
- Strategy：instruction rule 是真实的闭合编码策略数据；不用虚类层次表达静态 x86 子集；
- inheritance、virtual dispatch、RTTI：不存在运行期可替换 assembler 类型，不适用；
- friend：同模块私有方法已经提供最窄协作面，不需要扩大访问权。

## 验证门禁

本批次必须保持：

- 443 项语言、工具、ELF、FFI 与负诊断测试全绿；
- numeric forward/backward fixup、未解析 numeric label 和 symbol relocation 行为覆盖；
- stage-next/stage-fixed 的全部 assembly、object 与`luna`逐字节一致；
- 3 MiB `sem_funcs.s`汇编时间不高于迁移前 0.75 秒的可见噪声范围；
- formatter、module graph、接口行数和统一目录 audit 全绿。

## 验证结果

最终在远程 `caw` x86-64 WSL2 完成：

- audit 为 75 modules、统一驱动闭包 65 objects，formatter 为 0 reflow/0 token drift；
- relocation、numeric forward/backward fixup、未解析 label 和 ELF round-trip 目标测试 6/6；
- 完整测试 443/443 通过，用时 20.46 秒；
- 三阶段 verify 用时 256.51 秒，stage-next/stage-fixed 全部产物逐字节一致；
- 3,069,988 字节`sem_funcs.s`三次汇编为 0.767、0.774、0.762 秒；
- 新 assembler 与旧 anchor 对同一`sem_funcs.s`生成的 1,128,394 字节对象逐字节相同；
- `x86_64_assembler.lo`为 917,923 字节，最终`luna`为 4,704,081 字节。

规则表和类边界没有引入可见汇编吞吐回退；最终 executable 相比上一 anchor 增加约 1.06%，
换取 typed vector、确定性 RAII、闭合规则分派和删除全部 state-pointer 过程式入口。
