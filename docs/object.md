# x86-64 Object 模型现代化设计

## 目标与格式边界

`luna.compiler.x86.object` 是 Assembler、ELF Reader/Writer 与 StaticLinker 之间唯一的内存对象模型。
本批次只重构所有权、构建和访问边界，不改变 LUNAOBJ1 v1 的任何字节：112 字节 header、四个
region 尺寸/对齐、名字表、56 字节 symbol 和 40 字节 relocation 记录均保持原契约。

`Symbol`、`Relocation` 以及 ObjectSet 的 descriptor 只是透明记录，继续使用 `struct`。持有映射、
维护构建不变量或管理阶段状态的类型使用 class。

## 所有权与视图

move-only `Object` 私有组合：

- text、rodata、data 与名字表的四个 `byte_buffer`；
- `vector<Symbol>` 与 `vector<Relocation>`；
- BSS 尺寸和四个 region 对齐。

构造函数建立唯一空状态；move construction 转移全部资源并把源对象恢复为空状态；析构由组合成员
完成。没有公开 copy 或 move assignment：当前所有权只在结果槽和 `take_object()` 边界构造转移，
增加未使用的赋值协议只会扩大状态空间。

`ObjectView` 是刻意透明、可平凡复制的只读 `struct`，包含 `ByteView`、`SymbolView`、
`RelocationView` 与布局元数据。ELF Writer 和 Linker 只消费 view，不接触 Object 私有存储，也不
可能释放资源。查询和验证是对透明 view 的无状态算法。

这里不把 `const_span<Value>` 直接放进跨模块公开记录：当前 Luna 会在每个消费者内导出同一泛型
实例，而 LUNAOBJ1 尚无 COMDAT/weak ODR 合并，最终会形成重复全局符号。领域 view 保持同样的
typed pointer/count 契约；`vector` 与 `span` 仍用于 Object 模块内部的 typed storage/algorithm。

## 构建、读取与写入

`ObjectBuilder` 是唯一可变入口。Assembler 与 ELF Reader 通过它完成：

- region append、定宽小端写入与可写 span；
- alignment 和 BSS 分配；
- symbol 唯一查找、受控修改与 relocation 验证；
- sticky first-error 和最终 `take_object()`。

私有 `ObjectReader` 借用 facade 栈上的 Builder，保存 LUNAOBJ1 输入、cursor 与 first-error。Builder
是唯一清理 owner，因此失败没有 release tree。私有 `ObjectWriter` 借用 `Object const&`，拥有一个
RAII output buffer，并按 header、payload、symbol、relocation 顺序粘性写入。

## 多输入所有权

Linker 不再接收浅复制的 owning Object 数组。`ObjectSet` 把已验证 Object 内容复制进共享的四个
byte pool 和两个 typed vector，并为每个输入保存 typed descriptor；StaticLinker 借用 ObjectSet，
按索引取得 `ObjectView`。

输入数量沿用 Linker 的真实上限 128，因此 descriptor 使用 `[128]ObjectDescriptor`：它比动态分配
更便宜，保持明确上界，也规避当前 anchor 对大型 descriptor vector 单态化的已知缺陷。普通 driver
通过 `add(Object&&)` 零拷贝接管四个 byte buffer 与两个 typed vector；const overload 只为确实需要
重复同一 Object 的调用执行深复制。失败不会留下半个 view，ObjectSet 析构按精确 allocation size
释放全部接管存储。

## 实现单元

所有文件实现同一 `luna.compiler.x86.object` 模块：

| 文件 | 职责 |
| --- | --- |
| `object.la` | Object/ObjectView 生命周期、只读查询和完整验证 |
| `builder.la` | region、BSS、symbol、relocation 的受控构建 |
| `collection.la` | ObjectSet pooled ownership、事务追加与 view 生成 |
| `reader.la` | LUNAOBJ1 header/record 解码与范围验证 |
| `writer.la` | LUNAOBJ1 确定性序列化 |
| `facade.la` | `serialize`/`deserialize` 入口 |

这些是 method family，不新增 object 子模块。接口保持在 250 行以内；实现文件按责任拆分，不使用
`common`、`helpers` 或 forwarding layer。

## 当前 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | Object、Builder、Reader、Writer、ObjectSet 各自保护真实所有权或阶段不变量 |
| generics | typed vector/span 用于模块内部存储与算法；公开领域 view 避免跨模块重复单态化 |
| composition/RAII | Object 与操作类组合 byte_buffer/vector，失败自动清理 |
| copy/move | Object 仅显式 move construction；ObjectView/记录保持平凡复制；ObjectSet 不移动 |
| friend | 仅 Object 授权同模块 ObjectBuilder 访问私有构建存储，避免公开可写逃逸口 |
| bound methods | Reader/Writer/Builder 操作直接绑定各自 session；没有需要保存的 callback |
| overload/default | 当前构建动作没有同一语义族的可选参数，不添加装饰性重载 |
| operator | Object 没有自然值运算；唯一所有权不提供复制式赋值语法 |
| inheritance/virtual/RTTI | 格式与 section/relocation 是封闭 enum，`switch` 比运行期层次准确 |

## 验证契约

旧 anchor 的 Object 模块三次编译为 0.327/0.322/0.321 秒，输出恒为 512,262 字节，SHA-256 为
`85a30329947adff5b3273c5c71311e6986c108174b8facd7e3d463bfe65115da`。

行为基线：

- LUNAOBJ1 `dispatch-first.lo`：429 字节，SHA-256
  `c5b3ebe6c59c33489e2088d41626437eb89237f5d2b3b10a16688471108ca62d`；
- ELF `dispatch.o`：944 字节，SHA-256
  `c86f305ce26f4c66ee9ff1c2afe36b81d37942607019ee059daaca4c335cfc76`；
- executable `dispatch`：8,200 字节，SHA-256
  `8f7b2e1fbda758462d5f709993c2dc53fb477db491cccecdb71a3e479816d592`。

新实现必须逐字节复现三者，并通过 Object/ELF/relocation 直接探针、443 项测试、audit、refmt 与
`verify --fresh` 固定点。性能数据只用于记录真实成本，不在本批次继续扩展容器或格式范围。

## 验证结果

最终在隔离的 caw x86-64 Linux 工作区验证通过：

- audit 为 75 modules、1 driver，formatter 为 0 reflow/0 token drift；
- Object/ELF/relocation 直接探针 6/6 通过；
- 完整测试 443/443 通过，最终用时 4.34 秒；
- `verify --fresh` 用时 52.78 秒，stage-next/stage-fixed 的全部 assembly、object 与 executable
  逐字节一致；
- 最终 LUNAOBJ1、ELF 与 executable 分别保持 429、944、8,200 字节，SHA-256 与本文件记录的
  旧基线完全一致。
- 对同一组完整工具链输入，旧/new link 三次分别为 0.509/0.500/0.507 秒和
  0.742/0.737/0.742 秒；本批次按既定范围记录该成本，不继续扩展容器优化。
