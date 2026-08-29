# TypeTable 现代化设计

## 目标与冻结契约

历史 `luna.bootstrap.middleend.type` 由一个 120 行接口和一个 1474 行实现组成。公开 `Table` 用两组
`bytes::Buffer + count` 表示 Record 与 Field，一组 `table_*` 自由函数在每次 mutation 后返回浅拷贝
TableResult，调用方再覆盖原 Table，最终手工 `table_release`。存储、内建类型、canonicalization、布局 mutation、
结构验证和目标布局验证全部混在同一实现单元。

本批原子迁移为 `luna.compiler.types`，冻结以下可观察契约：

- Kind、BuiltinType 与 Flag 的成员、顺序和值不变；
- Record 与 Field 的字段、含义、目标位宽和布局不变；
- 16 个内建 Type ID、用户类型追加顺序和 no_id 哨兵不变；
- 指针、引用、定长/柔性数组的 canonical identity 与首次出现顺序不变；
- function pointer、bound method 参数切片、class/opaque/base/vptr 和 alias mutation 规则不变；
- 完整验证继续独立检查记录形状、唯一 canonical 类型、字段切片和最终目标布局。

全部 29 个直接 import 点同批迁移，不保留旧模块、Table、TableResult、table_* 或 forwarding API。

## 领域对象与所有权

Kind、BuiltinType、Flag、Record 和 Field 保持透明被动值。`TypeTable` 是唯一资源 owner，私有组合
`vector<Record>`、`vector<Field>` 和 sticky runtime error。默认构造直接建立完整的 16 个内建类型；构造失败
保留可析构的部分状态并通过 `error()` 可观察，不存在构造后的 initialize 协议。

TypeTable 是 move-only RAII 类。Semantic Context 直接拥有它，Semantic Result 从 Context move 接收；结果释放
不再手工释放类型缓冲。IR verifier、ABI classifier、FramePlan 和 CodeGenerator 全部通过 `TypeTable const&`
借用，CodeGenerator 只在一次 emit 会话内保存 const 指针，不复制或延长所有者生命周期。

`record_data()` 与 `field_data()` 提供 C++ 容器风格的 typed contiguous data 边界。const overload 服务验证、IR
和 Codegen；mutable overload 只服务类型构造阶段与独立破坏性验证测试。调用方不能观察容量、分离底层分配
或释放存储，所有权始终留在 TypeTable。

## 构造、失败与验证

pointer/reference/array 在 typed Record 序列中完成 canonical 查找，未命中时通过同一 append_record 边界追加。
字段和 callable 参数通过 typed Field vector 追加，并同步维护 owner 的 first/count 切片。任一 vector、范围或
mutation 失败都会设置 sticky error，后续 mutation 不再假装成功。

验证仍与构造逻辑独立：`is_valid(require_complete = true)` 只读取 typed storage，检查每个内建/命名/alias/
pointer/reference/array/callable Record、每个 Field 的归属切片以及 aggregate/enum 的最终布局。测试直接破坏
Record 后要求 validator 拒绝，再恢复并重新接受，证明验证结果不是构造路径缓存。

Kind/bit-width/builtin 映射使用 switch 或固定数据表。有界 Record/Field 遍历全部使用 `for`；TypeTable 新实现
没有手写下标 while，所有条件最多两个逻辑项。

## 模块与文件

唯一接口是 `compiler/include/luna/compiler/types.lh`，共 120 行。全部实现属于同一个模块：

| 文件 | 职责 |
| --- | --- |
| `storage.la` | RAII 构造/move/deinit、typed data/query、sticky error 和 append 边界 |
| `traits.la` | 内建类型数据表、Kind 分类和 bit width |
| `construction.la` | named/opaque/pointer/reference/array/callable/field 构造与 canonicalization |
| `mutation.la` | layout、packed/header、enum/alias/base/vptr/resolving 状态转换 |
| `validation.la` | Record/Field 结构、canonical identity 与 completeness 验证 |
| `layout.la` | aggregate/enum 目标布局复算和 TypeTable 最终验证 facade |

实现单元为 128--449 行；目录只包含同模块实现文件，不创建 storage/validation 等伪子模块。

## 当前 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | TypeTable 私有保护两套存储、sticky error、构造与释放不变量 |
| composition/RAII | 两个 typed vector 自动释放；Semantic Result move 接收唯一 owner |
| generics | vector<Record>/vector<Field> 取代重复字节长度、转换和 append 逻辑 |
| copy/move | TypeTable 禁止资源浅拷贝，只提供 move constructor；moved-from 表为空且可析构 |
| overload/default | record/field data 提供 const/nonconst overload；is_valid、pointer、reference 使用自然默认值 |
| operator | Type ID 和表没有自然算术或比较 DSL，不定义 operator |
| bound methods | mutation/query/验证依赖 TypeTable 状态并使用绑定方法；不存在回调策略轴 |
| friend | 公开 typed data 与窄 mutation 已满足协作，不增加私有存储逃逸 friend |
| inheritance/virtual/RTTI | 类型表只有一种封闭存储和构造策略，不需要运行期替换或类型识别 |

## 成本与基线

提交前 anchor 在隔离 caw x86-64 Linux 工作区编译旧 type 模块三次，耗时为 0.857522、0.857127、
0.855345 秒，中位数 0.857127 秒；汇编均为 976,009 字节，SHA-256 为
`528ffb49644c4fa27a03f0d2bc6772bdb04af2d1d23ca9d9a86146091525b215`。

同一 anchor 编译最终 types 模块三次为 1.124548、1.227933、1.122349 秒，中位数 1.124548 秒；汇编均为
1,275,674 字节，SHA-256 为
`c3242742e302e84c837fbd208f747279612b691fa5fdc7780e62f6f7d293f535`。中位编译时间约增加 31.2%，汇编体积
约增加 30.7%；增长来自两个 generic vector 实例、RAII/move 方法和 typed mutation/validation 边界。本批
记录无优化结构成本，不回退到裸字节存储，也不在没有真实全图热点数据时提前增加索引结构。

## 验证门禁

最终必须通过：

- 独立 TypeTable 行为程序覆盖 builtins、canonicalization、callable、layout、move 和破坏性验证；
- 所有 Semantic、IR、Codegen、CLI 与 relocation 消费端的完整行为回归；
- 相关文件两逻辑项条件扫描、audit 与 formatter；
- `verify --fresh` 的 transition→next→fixed 全 artifact 逐字节一致；
- 最终 types 汇编/object/executable 的大小与 SHA-256 记录。

## 验证结果

最终在隔离的 caw x86-64 Linux 工作区完成全部门禁：

- audit 为 68 modules、1 driver、58 library objects；formatter 为 0 reflow/0 token drift；
- 对 Types 接口、六个实现单元和专用契约共 8 个文件执行条件扫描，所有 `if`、`for`、`while` 均不超过
  两个逻辑项，且新实现没有手写下标 while；
- TypeTable 契约覆盖 builtins、pointer/reference/array canonicalization、callable 参数、layout、move、
  moved-from 状态与独立破坏性验证；Semantic、IR、Codegen、CLI 和 relocation 回归保持通过；
- 完整测试为 450/450，0 failed、0 skipped，用时 5.25 秒，峰值 27,200 KiB；
- `verify --fresh` 用时 54.88 秒，峰值 33,740 KiB，stage-next/stage-fixed 的全部 assembly、object 和
  executable 逐字节一致。

stage-fixed `types.s` 为 1,275,674 字节，SHA-256 为
`c3242742e302e84c837fbd208f747279612b691fa5fdc7780e62f6f7d293f535`；`types.lo` 为 425,966 字节，
SHA-256 为 `c7f2ba9e3495e6bf51624399b17095219d0428b3d963bb67451e63f297e2aaed`。最终 `luna` 为
4,994,899 字节，SHA-256 为 `c8e6dbac2dac2b97c146efb761943fbd2da70634731daa59f3e1817afc0e4f5a`。
