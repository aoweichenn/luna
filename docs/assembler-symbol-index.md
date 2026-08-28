# Assembler 符号索引

## 问题

assembler 过去把符号只保存在`object::Object.symbols`顺序记录中。每次`.globl`、`.type`、
标签定义和`.size`都调用`find_symbol`全表扫描；fixup 解析阶段又按名字扫描一次。设符号数为
S、普通 fixup 数为 F，符号处理成本接近 O(S² + F×S)。`sem_funcs.s`这类大输入会放大该
路径。

`Object`仍是可序列化的被动对象记录，不应混入只在汇编期间有效的树节点、借用指针或
分配器状态。因此索引属于构建阶段，而不是 LUNAOBJ1 格式。

## 设计

`luna.compiler.x86.assembler.SymbolTable`是汇编期间的领域类：

- 通过泛型`map<SymbolName, SymbolBinding>`拥有按名字排序的 symbol index；
- `SymbolName`借用输入 assembly 中的 UTF-8 view，并用自然字节序实现`operator <`；
- `reference`只建立稳定 binding，声明/定义到来时才由`materialize`向 Object 追加符号；
- binding 使用“referenced → materialized”单向状态转换，保持旧实现的 Object 符号顺序；
- 析构自动释放 map/tree/pool 资源，不改变最终 Object 的所有权；
- 表只在`assemble`调用期间存在，所以借用的 source view 始终比索引活得更久。

普通 fixup 创建时调用`reference`并直接保存稳定的 binding 指针。前向标签在后续定义时
物化为 Object 符号；未声明的 call 在 fixup 解析时按原顺序物化为 external。解析阶段通过
binding 中的 index 取得符号，不再按名字重复查表；未声明的 branch、RIP-relative 和
absolute 引用仍被拒绝。

最终复杂度为 O((S + F) log S)，普通 fixup 解析本身为 O(F)。数字局部标签继续按 section、
number 和方向扫描独立的 numeric-label 序列；它们不是当前大对象的主要瓶颈，后续有基准
证据时再建立专用索引。

## 语言特性审查

- class、访问控制、构造和析构：用于保护 SymbolTable 的 map 与一致性；
- 泛型和组合：直接组合现有`map<SymbolName, SymbolBinding>`，不再维护另一份裸字节容器；
- operator：`SymbolName.operator <`表达 map 所需的自然全序；
- move：SymbolTable 不跨作用域转移，当前不提供无用途的 move surface；
- overload/default：索引操作各有明确职责，不需要默认参数或重载；
- bound method、friend、继承、virtual dispatch 和 RTTI：这里没有策略替换、协作访问或
  运行期类型层次，引入它们只会增加间接层；
- Object、Symbol、Relocation 和 Fixup 继续使用 struct，因为它们是透明记录。

SymbolTable 是 assembler 内部由 encoding 与 source 共同消费的构建状态，不是独立 import
边界，因此声明位于唯一的`luna.compiler.x86.assembler`接口，实现在`assembler/symbols.la`。
object 模块只提供具有“名字已经证明唯一”前置条件的`append_symbol`窄接口；依赖方向为
assembler → object/std containers，ELF reader 继续只依赖被动 object 模型。

## 验证

验证必须在 caw 的隔离工作区完成：

1. 使用旧工具构建包含新 SymbolTable 的下一阶段；
2. 比较旧/新 assembler 对同一`sem_funcs.s`的墙钟时间和输出哈希；
3. 运行 audit、refmt、固定点 verify 与完整 test；
4. 确认 LUNAOBJ1 格式版本和序列化字节没有变化。

caw x86-64 WSL2 初始同机结果：`sem_funcs.s`为 3,070,612 字节、97,647 行。旧 anchor
assembler 用时 133.64 秒，新 SymbolTable assembler 用时 0.75 秒，约快 178 倍；两份
LUNAOBJ1 输出的 SHA-256 均为`64b6973a97f10968555b474bee08ae67caf535307d2b938a049ce3dfb49bc405`。
