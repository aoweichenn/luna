# 中端：类型表、IR 与语义分析

> **状态与权威性：** 本文是绑定提交 `39a2d87` 的非规范代码审计快照，用于导航、
> 现状分析和技术债发现。与 `AGENTS.md`、语言规范、架构契约或当前源码冲突时，
> 以后者为准；文中的行号和计数不会随重构自动更新。

`compiler/src/middleend/` 是全仓最大的子系统。下表保留绑定快照时的规模；当前工作树规模见
“当前工作树更新”小节。

| 子树 | 行数 | 说明 |
|---|---:|---|
| `semantic/` | 22,514 | 语义分析（本篇主体） |
| `ir/` | 1,879 | 中间表示 |
| `types/` | 1,408 | 类型表（`luna.compiler.types`） |
| `sema.la` | 195 | 管道入口 |

**术语警告**：有两个名字里都带 "types" 的模块，不是一回事：

- `luna.compiler.types`（`compiler/src/middleend/types/`）——**类型表**，类型的存储与驻留
- `luna.bootstrap.middleend.semantic.types`（`compiler/src/middleend/semantic/types.la`）——
  **语义层的名字解析与布局驱动**，是类型表的使用者

行号截至提交 `39a2d87`；下文未特别标注的行号和计数属于该历史快照。

## 当前工作树更新：LoweringState 批次

当前工作树的中端实现共 27,649 行，其中 `semantic/` 为 24,142 行，
`ir/` 为 1,879 行，类型表为 1,408 行，`sema.la` 为 220 行。本批在同一
`luna.bootstrap.middleend.semantic.context` 模块下新增
`compiler/src/middleend/semantic/context/lowering.la`（721 行）；它不是
`context.lowering` 子模块。当前 Context 接口为 580 行，Context 主实现、
builder 和 lowering 实现分别为 613、478 和 721 行。

`LoweringState` 是 move-only class，私有拥有函数级游标、typed vector 局部与
临时值、label/goto 快照、loop label、控制目标及 sticky error。它以 const
projection 提供只读数据，以 deep validity 检查函数归属、快照覆盖、scope 和
control watermark；goto 快照明确区分已发布范围与可丢弃的未发布尾部。
Context 的 legacy raw storage registry 从 13 组收缩为 6 组；IR Builder、
const/type/call caches 仍不属于 LoweringState。`context.builder` 尚未删除，
只增加了窄的 `set_current_block` 首错桥接；旧 `clear_label_state` API 已删除。

`tests/relocation_data/ir_codegen.la` 当前为 1,004 行，新增 LoweringState
生命周期、真实两-local 快照、published/unpublished goto 尾部丢弃、move、
sticky error 和 watermark 拒绝的直接契约。独立 caw 验证已记录在
`docs/sema.md`；本文件后面的旧 Context 分析仍保留作为重构前对照，不应再
当作当前字段布局或行号依据。

---

## 1. 类型表 `luna.compiler.types`（6 文件 1,408 行）

已完成 RAII 重写（提交 `4198d24`，同时把 `luna.bootstrap.middleend.type.lh` 重命名为
`luna.compiler.types.lh`）。

### 1.1 表示

`class TypeTable`（`compiler/include/luna/compiler/types.lh:67-108`）私有
`records: vector<Record>` + `fields: vector<Field>` + `error_code`。

**type_id 就是 records 下标**。前 16 个槽位（`BuiltinType.count = 16`，types.lh:15）
在构造时预置：`init()` 循环 `builtin_record(builtin_kind(i), builtin_size(i), builtin_alignment(i))`
（storage.la:30-41），kind 由 `traits.la:20-26` 的定长表 `[16]Kind` 查得。
`va_list` 固定 24 字节、8 对齐（`TypeLimit.va_list_bytes`，storage.la:3-6；traits.la:32-35）。

### 1.2 驻留：线性扫描，无哈希

`pointer()`（construction.la:97-106）、`reference()`（140-149）、`array()`（178-184）、
`flexible_array()`（204-210）各自从 `BuiltinType.count` 起遍历已有 record，
按 `kind + element_type + 限定符（+ element_count）` 命中即返回旧 id。

`add_callable()`（255-274）**不做驻留**——函数指针与 bound method 一律追加。

### 1.3 等价性

- 对 pointer / reference / array / callable 是**结构化**等价：kind + element_type +
  限定符位 + element_count / 参数 id 序列（`callable_parameters_match` validation.la:280-292）
- 对 structure / union / enum / class 是**名义**等价：靠
  `declaration_unit` / `declaration_node` / `name_token` 三元组（types.lh:29-32）

唯一性不靠构造保证，而由**校验反证**：`has_prior_pointer` / `has_prior_reference` /
`has_prior_array` / `has_prior_callable`（validation.la:161/198/262/294）——O(n²) 全表扫描。

### 1.4 布局：只验证，不计算

`middleend/types/layout.la` 只做校验：从 `initial_record_layout`（10，处理基类起始偏移与
vptr 槽位）出发，`append_field_layout`（58-105）逐字段重算期望偏移（`packed` 时对齐降为 1、
union 要求 offset 为 0、柔性数组仅末位），`final_record_layout_is_valid`（107-127）校验
size 按 alignment 上取整、空结构为 1 字节。

**实际计算在 `semantic/types/layout.la`**（见 §7.7）。

### 1.5 所有权

move-only（`init(other: TypeTable&&)` storage.la:43-47，移动后把源 `error_code` 复位为 `none`）、
sticky `fail()`（53-58）、`deinit()` 空（126）依赖 `vector` 级联释放。

它采用了与 IR 相同的**非拥有指针视图**模式：`record_data() const -> *const Record` /
`field_data() const -> *const Field`（80-90）返回底层数组首址，供 layout / validation /
`ir/validation` 直接下标访问——目的是避开泛型视图的重复符号问题（见 §2.3）。

---

## 2. IR `luna.compiler.ir`（7 文件 1,879 行 + 接口 230 行）

### 2.1 Module / Builder / View 三件套

| 角色 | 定义 | 契约 |
|---|---|---|
| `Module` | ir.lh:170-198 | move-only RAII 所有者。12 个私有成员 = 9 个 `vector<T>` + 2 个 `byte_buffer` + `entry_function`。**没有拷贝构造**。 |
| `Builder` | ir.lh:200-227 | 持有构造中的 `storage: Module` + sticky `error_code` + `active` 标志 |
| `View` | ir.lh:129-153 | 纯 struct，23 个字段 = 11 组 `*const T` + `usize` 计数 + `entry_function`。**无方法、非泛型**。 |

`Builder::ready()`（storage.la:156-159）要求 `active && error_code == none && storage.storage_is_valid()`。

`take_module()`（188-195）是一次性**阶段转换**：未 ready 则记错并返回空 Module；
否则置 `active = false` 并把 storage 移出。此后该 Builder 上所有增变操作经 `ready()` 全部失败，
二次 `take_module` 返回空 Module 并置错。

`Module` 侧在 `init(other: Module&&)`（storage.la:48-60）与 `operator=`（62-76）里
逐个成员 `as ...&&` 转移，并把源的 `entry_function` 复位为 `no_id()`。

`View` 由 `Module::view()`（storage.la:91-110）用 `empty() ? null : &get(0)` 装配，
按值传递，被 codegen / elf / linker / semantic 各方直接消费。

### 2.2 为什么 `View` 是非泛型的

`SESSION.md:104-107` 明确记载：早先的泛型 `const_span<T>` 方案在多个模块单态化后
**产生了重复的强符号方法，最终链接失败**。因此 `View` 刻意做成零代码生成的非泛型视图。

同样的理由解释了 `TypeTable::record_data()` 返回裸指针。**这是当前代码生成与链接模型的
约束，不是永久语言限制**：在泛型实例能去重或使用可合并链接语义之前，跨模块引入带方法体
的泛型视图会重现该问题。`GenericTable` 的 `IndexEntry` 可见性是另一项边界问题，见 §5.2。

### 2.3 存储的三种组织方式

1. **连续切片**——`Function` 用 `first_parameter/parameter_count`、`first_slot/slot_count`、
   `first_block/block_count`、`first_value/value_count` 拥有各自区间（ir.lh:48-72）；
   `Global` 用 `first_reference/reference_count`（27-34）。`add_*` 时用
   `function_can_extend_range`（functions.la:16-18）保证区间连续追加
2. **侵入式链表**——`Block` 拥有指令：`first_instruction/last_instruction/next_instruction` +
   `instruction_count` + `terminated` + `predecessor_count`（ir.lh:99-106；
   链在 `instructions.la:60-70` 维护）
3. **旁路表**——`call` 的参数不是 per-call 切片，而是全局
   `vector<Argument>{instruction_id, value_id}`（ir.lh:124-127），
   校验时全表线性扫描计数（verify.la:577-593，O(指令数 × 参数数)）

`Instruction` 是扁平 13 字段结构（`opcode`、`operation: lexer::TokenKind`、`type_id`、
`result`、`left`、`right`、`third`、`auxiliary`、`immediate: u64`、`floating: f64`、
`block_id`、`next_instruction`、`span`，ir.lh:108-122）——即"四操作数 + 两个辅助槽 +
双立即数"的统一编码，**38 个 opcode 共用**。

### 2.4 校验的三层分布

1. **准入校验（builder 时，廉价）**：`globals.la`（`global_reference_request_is_valid`
   102-115：占位 8 字节必须全零、必须 8 对齐、且必须紧接同 global 的前一条引用之后；
   `add_global` 23-47 失败时 `truncate` 回滚）、`functions.la`、`control.la`
   （`opcode_is_terminator` 3-12）、`instructions.la`（`emit` 43-92 在终结指令后标记 `terminated`）
2. **逐 opcode 的操作数/类型规则**：`validation.la`（545 行）。
   `instruction_operands_are_valid`（64-107）用三张 switch 表判定 left/right 该不该存在，
   并要求 SSA 式的 `definition < instruction_id` 支配性；
   `instruction_types_are_valid`（417-545）按 opcode 分派到 8 个特化校验器，
   递归下钻深度上限 `maximum_type_depth = 256`
3. **整模块校验**：`verify.la`（654 行）。文件私有（未导出）的 `class Verifier`（3-24），
   20 个私有字段缓存 `View` 的各路指针与 `TypeTable const&`；`run()`（626-643）依次检查
   input → globals → functions → function totals → entry → slots → blocks →
   predecessors → instructions → arguments

**入口是 `Module::is_valid(TypeTable const&)`（verify.la:646-654）**，不再有独立的
`luna.bootstrap.middleend.ir.verify` 子模块（该头文件已删除）。

---

## 3. 语义管道入口 `sema.la`（195 行）

`SemanticSession::run()` 是一条**固定顺序的 29 步命令式管道**，没有阶段对象或策略抽象：

| 步 | 行 | 调用 | 步 | 行 | 调用 |
|---:|---:|---|---:|---:|---|
| 1 | 133 | `modules::collect_modules` | 16 | 148 | `classes::collect_fields` |
| 2 | 134 | `modules::collect_imports` | 17 | 149 | `classes::validate_classes` |
| 3 | 135 | `modules::validate_import_graph` | 18 | 150 | `consteval::validate_bitfields` |
| 4 | 136 | `attributes::validate_attributes` | 19 | 151 | `const_engine::resolve_constants` |
| 5 | 137 | `types::collect_named_types` | 20 | 152 | `consteval::finalize_enum_constants` |
| 6 | 138 | `classes::collect_classes` | 21 | 153 | `functions::collect_functions` |
| **7** | **139** | **`classes::collect_bases`** | 22 | 154 | `classes::collect_methods` |
| **8** | **140** | **`classes::prepare_layouts`** | 23 | 155 | `classes::validate_methods` |
| 9 | 141 | `functions::collect_const_functions` | 24 | 156 | `consteval::check_module_asserts` |
| 10 | 142 | `consteval::collect_constant_declarations` | 25 | 157 | `modules::validate_imported_names` |
| 11 | 143 | `consteval::prescan_array_lengths` | 26 | 158 | `visibility::validate_public_types` |
| 12 | 144 | `consteval::resolve_alignment_attributes` | 27 | 159 | `function_ir::create_ir_functions` |
| 13 | 145 | `consteval::resolve_bitfield_attributes` | 28 | 160 | `classes::create_vtables` |
| 14 | 146 | `types::resolve_all_types` | 29 | 161 | `stmt::lower_functions` |
| 15 | 147 | `classes::collect_friends` | 尾 | 162 | `this->select_entry()` |

收尾（163-167）：`generics.is_valid()` 校验，然后 `finish()`。

**关键顺序约束**：`prescan_array_lengths`（步 11）必须早于 `resolve_all_types`（步 14），
因为 `types` 不能 import `consteval`；`validate_bitfields`（步 18）晚于 `resolve_all_types`，
因为位段总宽要等存储类型布局确定。

**顺序风险**：`collect_bases`（步 7）写入基类关系，`prepare_layouts`（步 8）立即消费它，
而一致性校验 `validate_classes` 拖到**步 17**。见 §7.1。

### 3.1 `SemanticSession` 三态机

`enum SemanticPhase: usize { ready, complete, transferred }`（21-23）。

- **ready**：`init(input)` 用成员初始化列表直接构造 `Context`（36 行）
- **transient work 清理**：唯一出口是 `priv fn finish()`（113-119）——
  `context::context_release_work()` 释放 23 条 transient `bytes::Buffer`，
  用 `release_error` 合并错误后置 `phase = complete`。
  `finish()` 在两条路径上被调用：输入非法（126-132 行发 `invalid_input` 后立即返回）
  和 `run()` 末尾（166 行）
- **入口选择**：`priv fn select_entry()`（40-111）。非可执行模式直接做可达性校验；
  可执行模式线性扫 `function_count` 找 `main`（`token_equals_bytes(..., "main", 4)`，53-54），
  重复发 `duplicate_function`（57-63）、缺失发 `missing_definition`（69-71）；
  随后逐项校验签名：`parameter_count == 0` 或 `(usize, *[const] u8)`（76-93）、
  返回 `signed_32`、非 external（94-98），再 `ir.set_entry`（106）
- **一次性转移**：`take_result()`（170-183）。守卫 `phase != complete` 返回空壳；
  否则先 `this->state.ir.take_module()`（177）把 IR 抽走，置 `phase = transferred`，
  再用聚合字面量把四个成员逐个 `as &&` 移出（180-181）
- `deinit()`（185-189）**只在 `phase == ready` 时**释放 work——即 `run()` 从未走到 `finish()`
  的情况。`complete` / `transferred` 时 work 已释放或已转移，避免二次释放

### 3.2 `SemanticResult` 为什么是 transparent struct

定义在 `context.lh:133-139`，带注释说明"语义结果是一次性传输记录；三个资源成员各自通过
RAII 管理生命周期"。

它是 `struct` 而非 `class`，因为它只是一次性阶段结果：不维护独立不变量，也没有除成员
RAII 之外的行为。`sema.la:180-181` 对四个成员分别转移（`types`、`ir`、`diagnostics`
各 `as &&`，`error` 按值）。把它做成 class 也能实现移动，但会为一个被动结果记录增加
没有实际责任的封装，不是当前所需边界。

`result_is_success()`（`context/diagnostics.la:76-80`）是唯一的后置校验，同时检查
`error == none`、`diagnostics.empty()`、`types.is_valid(true)`、`ir.is_valid(types)`。

### 3.3 `Context`：62 字段透明聚合

`struct Context`（`context.lh:398-461`）是**扁平的 62 字段聚合**，不是 class。
它组合了多个 RAII owner，因此不能称为 POD；这里的 `struct` 表示透明的跨 pass 状态记录：

- **23 个 `bytes::Buffer`**：其中 20 个带显式 `*_count`，`callable_signatures`、
  `label_locals`、`goto_locals` 不带独立计数。字段包括 units、modules、imports、symbols、bindings、
  callable_candidates、generic_callable_candidates、call_selections、functions、parameters、
  locals、temporaries、alignment_overrides、bitfield_segments、const_functions、const_locals、
  array_lengths、labels、pending_gotos、loop_labels + 无计数的 callable_signatures /
  label_locals / goto_locals
- **5 个 RAII 成员**：`classes: ClassTable`、`generics: GenericTable`、`types: TypeTable`、
  `ir: ir::Builder`、`diagnostics: DiagnosticBuffer`
- **1 个 view 成员**：`input: InputView`

所有权切分清晰：

- `Input`（context.lh:77-93）是 owner：`vector<Unit>` + `byte_buffer paths`，
  带 sticky `error_code`、`priv ready()` / `fail()`（input.la:118-127）、
  `add()` 的路径追加失败回滚（167 行 `paths.truncate`）
- `InputView`（59-75）只是 5 个标量的非拥有视图。
  **`Context` 只存 view，driver 保留 `Input` owner**
- `DiagnosticBuffer`（120-131）持 `vector<Diagnostic>`，上限
  `maximum_diagnostics = 4096`（diagnostics.la:61-63）；`DiagnosticView`（109-118）
  是 `{data_pointer, element_count}` 的只读视图，供 driver 遍历

一致性校验用"数据表 + 循环"实现，是全目录最规范的一处表驱动代码：
`ContextStorageCount.records = 22`（35-37）、`ContextStorageRecord{buffer,count,record_size}`
（39-43）、`storage_record_is_valid`（53-60）、`context_record_storage_is_valid`（74-131）。
这 22 项包含 20 个显式计数 buffer 和两个按长度推导计数的 locals buffer；
`callable_signatures` 作为无记录大小的字节串单独校验。

生命周期收敛点：`context_init`（context.la:150-174）一次构造完所有成员；
`context_release_work`（176-204）**只释放 23 条 transient buffer**，
RAII 成员由各自析构负责。

---

## 4. `luna.compiler.sema.domain`（161 行接口 + 234 行实现）

全仓唯一完成命名空间迁移的语义模块。接口在
`compiler/include/luna/compiler/sema/domain.lh`，实现在
`compiler/src/middleend/semantic/domain/{callable,category}.la`——**路径不镜像模块名**，
这是 `source_module()` 允许的（接口路径由模块名机械推导，实现路径自由填写）。

被动领域类型包括 callable/value 类别（`CallableKind`、`ReceiverKind`、`CallableIdentity`、
`ValueCategory`、`ReferenceRank`）、类元数据（`ClassAccess`、`MethodDispatch`、
`ClassFlag`、`MethodFlag`、`ClassLayout`、`CallDispatch`、`DispatchPlan`、`ClassRecord`、
`ClassField`、`ClassMethod`、`ClassFriend`）以及泛型元数据（`GenericDeclarationKind`、
`GenericInstanceState`、`GenericDeclaration`、`GenericParameter`、`GenericInstance`、
`GenericActiveBinding`）。

无状态判定：

- `callable.la`（69 行）：`receiver_is_valid` 用 switch 列 5 个合法值（21-31）；
  `callable_shape_is_valid`（33-53）是核心 switch——free_function 要求无 owner 无 receiver、
  static_method 要求有 owner 无 receiver、instance/operator_method 要求两者皆有、
  constructor/destructor 要求 owner 且 `receiver == ReceiverKind.writable`
- `category.la`（165 行）：构造器 `lvalue/xvalue/pointee`、投影 `project`（23-47，
  把 6 组 bitfield 折叠回非 bitfield）、`as_bitfield`（49-73）反向；
  谓词 `is_lvalue` / `is_addressable` / `is_assignable` / `is_volatile` 均用 switch；
  `reference_binding_rank`（146-165）是唯一的排序逻辑

**ClassTable / GenericTable 向下依赖 domain 值，`Context` 单独 import 两个 owner 模块，
所有更高层 pass 通过 `Context` 消费 `domain::` 值。** 这条分层是 `39a2d87` 刚建立的：
编译器直接 import 类/泛型 owner 的次数从 5/8 收缩到 1/1，直接 import domain 从 15 增长到 20。

`is_xvalue`（89-91）、`is_expiring`（93-95）、`is_temporary`（142-143）各只有两个
逻辑子句，用直接布尔表达式比 `switch` 更短且未超过复杂度预算；这里不构成风格债。

---

## 5. 两个 RAII 表：`ClassTable` 与 `GenericTable`

### 5.1 `ClassTable`（`classes/model.la`，185 行；接口 35 行）

move-only：`priv records / fields / methods / friends` 四个 `vector<>` + `priv error_code`。

- **slice 不变式**由三个自由谓词表达：`class_slice_is_empty`（7-9）、
  `class_slice_is_appendable`（11-16）、`record_slices_are_empty`（18-24）
- **append**：`append_record`（114-120）先过 `can_append_record`（43-47：ready + 容量未满 +
  该 record 的三个 slice 全空）；`append_field` / `append_method` / `append_friend`（122-180）
  结构对称——校验 `record_id`、`class_slice_is_appendable`、push、首次写入时设 `first_*`、
  `*_count += 1`
- **sticky 运行时错误**：`priv fn fail(error)`（49-54）只在 `error_code == none` 时写入，
  并**始终返回已存的那个错误**。一旦任一次 append 失败，后续所有 `append_*` 都短路返回首个错误
- **moved-from 状态**：`init(other: ClassTable&&)`（32-37）移动四个 vector 并把
  `other.error_code = runtime::Error.none`。结果是 **moved-from ClassTable 是一个"合法空表"
  而非中毒对象**——`storage_is_valid()` 仍为 true，`append_record` 仍能成功。
  与 `DiagnosticBuffer` 的移动语义一致，但也意味着"移动后误用"不会被校验捕获

**它没有 focused mutator**——只暴露 `*_data()` 裸指针。后果见 §9.2。

### 5.2 `GenericTable`（`generics/{storage,instances,validation}.la`；接口 61 行）

8 个私有 vector：declarations、parameters、instances、arguments（`IndexEntry`）、buckets、
type_instances、function_instances、active_bindings + 粘性 `error_code`；move-only
（storage.la:47-57）。开地址桶表，`hash_word` / `instance_hash`（instances.la:3-14），
75% 负载扩容（129-144），**先在替身 vector 上重建再整体移动发布**（103-127）。

这里有两个相关但不同的跨模块泛型约束：

- `IndexEntry` 必须 export（`generics/model.lh:12-15`，注释说明它不属于公共操作面），
  因为导出类的私有泛型成员类型也必须对消费其接口的模块可见；这是接口可见性约束
- 构造函数里 `assert(sizeof(IndexEntry) == sizeof(usize))`（storage.la:43）记录表示等价；
  历史上把某些跨模块实例直接改回 `vector<usize>` 会与其他消费模块生成的
  `vector<usize>` 强符号冲突，这是代码生成/链接去重约束

**为什么实现文件放在 `generics/` 而不是 `generics/model/`**：AGENTS.md 禁止同一层
同时放普通源文件和子目录。实际布局是 `semantic/generics.la` 与
`semantic/generics/{storage,instances,validation}.la`：前者是父目录中的 sibling facade，
不在 `generics/` 目录里。在 `generics/` 下再加单子目录 `model/` 会制造重复的单子路径，
并把同一 owner 的 cohesive method families 机械映射成文件路径。
三个文件都声明 `module ...semantic.generics.model`，在 `selfhost.py` 里合并注册为一个模块。

对照：`classes/model.la` 之所以独占 `classes/` 单子目录，是因为该层没有同级文件。

---

## 6. 各 pass

### 6.1 `classes.la`（1,570 行——全仓最大源文件）

横跨至少五个职责族：收集、校验、层次分析、vtable/descriptor 发射、访问控制与分派规划。

**收集**：`collect_classes`（236-254）先断言表空，再从 `BuiltinType.count` 扫 TypeTable，
对 `class_type` 先 `class_members_are_valid(diagnose=true)`（88-114）再 `append_class`（56-65），
flags 由 `class_flags`（46-54）从 `exported | final_class` 合成。

**基类**：`collect_bases`（196-210）两趟——先逐记录 `collect_class_base`（163-173），
再 `validate_base_chain`（175-194，深度/环检测）。`set_base_relation`（135-161）依次拒绝：
基类非 `class_type` 或无 ClassRecord、任一侧 opaque、基类 `final_class`。

**校验**：`validate_classes`（499-531）用 `RecordValidator` + `require()`（14-24）累积断言，
核心 `record_is_valid`（479-497）检查 `field_slice_is_valid`（406-427）、
`friend_slice_is_valid`（429-444）、`class_layout_is_valid`（446-477，vptr 范围/对齐/
与基类一致）以及 `flags == class_flags | layout_flags`。

**虚表 / RTTI / 多态**：`prepare_layouts`（774-793）后序 DFS，`prepare_class_layout`
（745-772）先递归基类，再用 `class_declares_virtual_contract`（726-743）决定 `polymorphic`。
`analyze_method_hierarchy`（1161-1181）为每个记录分配 `next_slot = hierarchy_slot_count(base)`，
`analyze_method`（1032-1054）对合法覆写复用继承槽，否则新开槽；
`override_is_valid`（1004-1030）要求 `override` 关键字**且**基类是虚**且**返回类型相同。
`update_hierarchy_flags`（1079-1099）把 `slot_count != 0` 提升为 `polymorphic`。
`update_rtti_flag`（1101-1130）：`@rtti` 显式开启或继承基类；非根类单独标注但基类未开启 →
`invalid_rtti`；开启但非多态 → `invalid_rtti`。
`create_descriptors`（1350-1372）DFS 建 8 字节 `descriptor_global` 并在偏移 0 挂一条
指向基类 descriptor 的引用；`create_vtable`（1374-1452）跳过非多态/抽象类，
有 RTTI 时前缀 8 字节，随后每槽挂一条函数引用。

**访问控制**：`access_from_flags`（26-44）用 `switch` 匹配 `AccessFlagEncoding`。
`access_is_available`（553-580）：`public` 直接放行；`private` 要求
`current_owner_type == declaring_type`，否则线性扫 friend 切片；`protected` 退化为
`is_base_of(declaring_type, current)`。注意 `method_is_accessible`（1555-1570）在函数
没有方法记录时返回 **false**，语义上是"非成员即不可访问"。

### 6.2 `consteval`（391 + 658 + 330 行，三层）

| 文件 | 职责 |
|---|---|
| `consteval.la` | 属性与断言驱动，8 个管道入口 |
| `consteval/engine.la` | 表达式折叠 + 常量符号不动点 |
| `consteval/engine/execute.la` | `const fn` 解释器 |

共享 `consteval/model.lh` 的 `Value{is_constant, is_boolean, value:u64, error}`——
**值本身不带类型**，类型符合性由 `const_integer_fits`（engine.la:10-24）/
`const_value_conforms`（567-587）另行判定。

**折叠能力**：整/字符/布尔字面量、括号、`Enum.member`（54-102）、具名常量（104-153，
按需解析 + `constant_resolving` 断环）、一元 `- ~ !`（155-177）、二元 13 种（205-287）、
带短路的 `&& ||`（179-203）、`?:`（289-306）、`sizeof/alignof/offsetof`（308-352）、
整数 cast（354-378）、`const fn` 调用（execute.la:230-330）、`@line()` 与
`@is_trivially_relocatable`（451-466）。

算术细节：`+ - *` 是 u64 回绕；`/ %` 走 i64 并有 `INT64_MIN / -1` 保护（238-239）；
移位拒绝 ≥64 且右移为算术移位（253-256）；`< <= > >=` 按有符号比较；
除零与越界移位返回 `error()` 而非 `not_constant()`，在使用点成为硬诊断。

**const fn 解释器**：环境是 `Context` 上一个扁平栈 `const_locals`，
`env_find`（execute.la:29-41）**从栈顶向下**查找实现内层遮蔽。预算由 `step()`（21-27）
扣 `const_steps`、`const_call_depth` 对 `Limit.maximum_const_call_depth`（247）；
**耗尽一律退化为"非常量"，不是错误**。

**bitfield** 两趟：`resolve_bitfield_attributes`（consteval.la:142-162）折叠每个具名宽度，
拒绝 0 / >64 / 重名；`validate_bitfields`（166-196）在布局之后按
`(declaration_unit, declaration_node)` 分组，要求 `Σ bit_width == bit_width(存储类型)`。

**enum**：`resolve_enum_layout`（types/layout.la:449-526）只内联折叠带符号的整数字面量，
其余全部打 `enum_value_pending`；`finalize_enum_constants`（consteval.la:373-391）
再按符号序强制 `engine::resolve_enum_constant_values`（471-565）用真正的折叠器重扫。

### 6.3 `generics`（facade 197 行 + GenericTable 实现 682 行）

**注册**：`register_declaration`（generics.la:113-140）——`declaration_mount_is_valid`
（30-45，enum 不可用；函数不得 external/variadic/asm，不得 virtual/abstract/override）→
`validate_parameters`（61-91，1..`maximum_generic_parameters` 且无重名）→
`append_declaration` → `register_parameters`（93-111）。

**类型实例化**：`instantiate_generic_type`（types.la:399-467）：arity 检查 →
`find_instance` 去重（`failed` 实例返回 `no_id`）→ 实例数与 `type_depth` 上限 →
泛型类契约 `generic_class_features_are_supported`（types.la:133-158：**不得有基类、
不得 opaque、不得 `@rtti`、不得有虚/抽象/覆写方法或泛型方法、不得有 friend**）→
`append_instance` → `types.add_named` → `set_instance_result(resolving)`。

**函数实例化**：`instantiate_generic_function`（generics.la:436）→ `activate_instance`（470，
把泛型参数名绑到实参）→ `add_function_declaration`（484），并**清掉
exported / interface_declaration 标志**（493-494，故单态体恒为 internal linkage）→
`set_instance_result`（500）→ `restore_active_bindings`（502）。

**解析顺序是"先普通后泛型"**：`resolve_call_target`（probe/call.la:795）仅当
`resolution.status == no_match && target.kind == free_function` 才回落
`resolve_generic_call`（generics.la:537）。后者通过 `ArgumentTypeProbe` 函数指针
（`probe/call.la:883`）回调 `probe::expression` 拿实参类型，再对每个声明做
`inferred_inference`（392，`unify_pattern` 299 结构化推断）或 `explicit_inference`（360），
要求**恰好 1 个**匹配。

**`call_selections` 缓存（call.la:639）在泛形体上下文整体禁用（801）**，
故泛型体内每个调用点每次重算。

### 6.4 `expr`（8 文件 4,626 行 + `probe/` 1,450 行）

**`expr.la:41-154` 是 20 个串行 `if (node.kind == ...)`**，覆盖 23 种 `SyntaxKind`，
分派到：`numeric`（整/字符/浮点字面量）、内联（boolean、null）、`strings`（string）、
`access`（name / this / call / index / member）、`operators`（unary / cast / binary /
conditional / sizeof-alignof-offsetof / va_arg）、`initializer`（zero/aggregate）。

回传机制是 AGENTS.md 点名的函数指针下传：`expr/api.lh:5-6` 定义 `Lowerer` / `IntoLowerer`，
`lower_into` / `lower_into_destination` 作为实参传给 access/operators/initializer，
**子模块因此不必 import 父 facade**。

**文件职责**：

| 文件 | 行数 | 职责 |
|---|---:|---|
| `base.la` | 12 | 仅 `invalid_expression()` |
| `api.la` | 17 | 仅 `invoke` / `invoke_into` 两个适配器 |
| `numeric.la` | 786 | 真正降字面量的只有 12-78 与 762-786；**80-743 行是一整套软件大数运算 + 十进制浮点→二进制二分转换** |
| `strings.la` | 380 | UTF-8/16/32 字面量解码 + `@embed`（276-380，`EmbedLimit.maximum_bytes = 16777216`） |
| `operators.la` | 1001 | unary/cast/binary/conditional/va_arg/布局查询 + `address_chain_type`（35）取地址静态对齐分析 |
| `initializer.la` | 424 | zero/aggregate 初始化器；含范围里唯一的 class `InitializerIndexSet`（26-63） |
| `access.la` | 1536 | name/this/枚举成员/位段/member/index/call/bound method/特殊构造 |
| `probe.la` | 470 | 不 emit IR 的纯类型试解 |

**没有通用隐式转换序列表**，只有三处离散 rank：引用绑定
（`domain::reference_binding_rank`）、调用实参（`BindingRank`，call.la:480-482：
exact=0/qualified=1/temporary=2/value=3）、候选总分（各实参 rank 累加 + receiver rank，
`consider_candidate` call.la:600 取最小，并列则报 ambiguous）。

算术上**没有任何隐式提升**：`operators.la:627` 直接要求 `left.type_id == right.type_id`，
否则 `type_mismatch`。唯一的"隐式"是 `expected_type` 自上而下传播
（字面量按需取型）以及 pointer ↔ usize 的 `convert_value`。

**候选只按签名字节排序，不按匹配质量排序**（`signature_is_before` + `function_order`）；
并列 rank 直接报 ambiguous，**没有 tie-break**（非模板优先 / 更特化优先）。

### 6.5 `functions`（10 文件 4,765 行）

**收集**：`collect_functions`（functions.la:602）遍历每模块 interface + 各 implementation
unit → `collect_functions_in_unit`（575）；泛型走 `collect_generic_function`（527），
否则 `validate_asm_function`（294）+ `collect_function_declaration`（463）；
再 `collect_class_methods`（623 → methods.la:735）；收尾
`build_function_signatures` + `build_function_bindings`（696）。

**重载键与去重**：`overloads.la:28 function_overload_key_matches`（identity + 参数个数 +
variadic/extern 一致 + 逐参数类型）；`classify_function_declaration`（141）输出 5 种 action。
`lookup_function_declaration`（83）对每个声明 O(n) 全表线性扫描。

**签名**：`signature.la:436 append_function_signature` 把 (version, convention, identity,
flags, [generic origin], owner, 参数…, 返回) 编码为字节串（tag 表 254-267）。
用途有二：给 IR 做符号改写（ir.la:67），以及作为 binding 内候选的排序键。

**绑定**：`bindings.la:412` 先 `function_order`（162，堆排序）→ 把"同模块 + 同 owner +
同名"的连续段聚成 Binding → 回填 `function.binding_id`。
**普通候选与泛型候选数据完全分离**：前者存 `function_id` 于 `callable_candidates`，
后者存 `declaration_id` 于 `generic_callable_candidates`。

**默认参数折叠不在重载解析期展开，而在实参收集期**（access.la:1360-1362）。
`defaults.la:145 function_default_profile` 先算 `required_parameter_count`，
并在 166-171 校验"默认参数必须尾部连续"；默认值**只支持 integer/boolean/null_pointer
三种常量**，且 `default_expression_is_source_independent`（44）禁止引用其他参数名。

**bound method**：`Kind.bound_method` 是 `{receiver, entry}` 两字结构（16 字节胖指针）。
构造在 `access.la:481 lower_bound_method`：偏移 0 写 receiver 地址、`sizeof(usize)` 写入口；
虚调用入口由 `load_virtual_callee`（1035）给出。调用端在
`lower_target_call_with_receiver`（1283-1285）读出两字。

**特殊成员没有隐式合成**。`special.la:59 resolve_special_constructor` 从 constructor
binding 里挑：`special_parameter_rank`（3）只接受 `&owner`（拷贝）或 `&&owner`（移动）
两种单参构造；无匹配时 trivially-copyable → `trivial`（100），否则 `unavailable`。
所谓"合成"实际是**校验 + 隐式调用插入**：`destructor_contract_is_valid`（methods.la:106）、
`base_constructor_call_is_valid`（176，要求有基类时构造函数体**首条语句必须且只能是**
`super.init(...)`）、`constructor_initializers_are_valid`（371）。

### 6.6 `stmt`（`stmt.la` 1,342 行 + `stmt/labels.la` 341 行）

`stmt.la:942-1035` 是 **18 个顶层串行 `if (node.kind == ...)`**（另一种数法把 break/continue
内的嵌套 if 也算进去得 20），分派到 `lower_variable` / `lower_constant` /
`lower_assignment` / `lower_block` / `lower_if` / `lower_while` / `lower_for` /
`lower_switch` / `lower_return` / `lower_va_start` 等，全部落空则发
`DiagnosticKind.internal_invariant`。

**`stmt/api.lh`（9 行）是 AGENTS.md 认可的优秀范例**：它只含三个函数指针别名
`Lowerer` / `Cleanup` / `NeedsCleanup`，使 `labels` 子模块**无需 import 父 facade**。

**`stmt/labels.la`（341 行）是当前仍保留的历史子模块**（`export module
...semantic.stmt.labels`，注册为 `sem_stmt_labels`）。它通过窄函数指针接口避免反向
import 父 facade，但边界仍应随语义模块收缩一起复核。goto 语义：

- `jump_is_valid`（86-97）= `label_is_subset_of_source`（58-68，标签处的活跃局部变量
  必须都是 goto 处也活跃的，即**禁止跳进作用域**）**且**
  `!source_crosses_nontrivial_lifetime`（70-84，不得跨越需要析构的局部变量）
- 向前 goto 先建一个未定义 Label 与新块并登记 `PendingGoto`（266-300）；
  标签被定义时（174-222）逐个校验挂起的 goto
- 函数体结束后 `validate_labels`（303-314）报未定义标签

`switch` 是三趟扫臂（735-751 分配块、756-805 生成比较/跳转链并用 `seen_values` 查
`duplicate_case`、814-824 下臂体）。break/continue 双模：`child_count == 1` 视为带标签，
否则走 `context->break_block/continue_block` 的保存/恢复。

### 6.7 `types`（855 行 + `types/` 子目录）

与类型表不是一回事——这是**名字解析 + 布局驱动层**，由 `types.la` 与 `types/layout.la`
组成同一模块。

- **收集**：`collect_named_types`（248-271）按模块先接口单元后实现单元；
  `collect_types_in_unit`（192-233）四路分支：重名 → `duplicate_name`；
  opaque 重定义 → `try_define_opaque_class`（73-100）；泛型 → `append_generic_type`；
  普通 → `append_named_type`
- **解析**：`resolve_type_node`（524-784）是唯一入口，覆盖 `type_generic` /
  `type_builtin` / `type_named` / `type_function` / `type_reference` / `type_pointer` /
  `type_array` / 内联 struct/union。**整个函数体是一条约 50 分支的 if / else-if 链**
- **布局**：`require_complete_type`（layout.la:140-153）→ `resolve_layout`（598-618，
  泛型感知：激活绑定 → `resolve_layout_active` → `set_instance_result` → 恢复绑定）→
  `resolve_record_layout`（226-401）/ `resolve_enum_layout`（449-526）/ `resolve_alias`；
  `extend_class_layout`（13-71）插入 vptr
- **查找**：`types/lookup.la`（89 行，历史子模块）——`lookup_field`（51-89）先本记录，
  再匿名提升（`lookup_promoted_field` 24-49），最后沿基类链。被 consteval 的 `offsetof` 复用
- **可见性**：`types/visibility.la`（146 行，边界待复核的历史子模块）——`public_type_is_valid`（8-49）
  先剥掉 pointer/reference/array 包装，再分两支：泛型实例 → 声明须带 `exported`
  **且**每个实参自身 public（递归）；具名类型 → 符号须带 `SymbolFlag.exported`

---

## 7. 技术债

middleend 贡献了全仓**全部 19 处超两子句条件**和**51/54 处手动索引 `while`**。
但它同时也是新代码纪律最严格的地方：`classes.la`、`types.la`、`types/layout.la`、
`types/visibility.la`、`stmt.la`、`stmt/labels.la` 与三个 generics model 文件
**超两子句条件数为 0**；`classes.la`、`classes/model.la`、`generics.la`、
`generics/{instances,storage,validation}.la`、`stmt/labels.la`、`types/visibility.la`
**完全无 `while`**。债务集中在**尚未重构的旧文件**，不是新写的。

### 7.1 `base_type` 双写镜像——双来源结构风险

两个副本：`type_info::Record.base_type`（TypeTable，写者 `types/mutation.la:108-124`）
与 `domain::ClassRecord.base_type`（`compiler/include/luna/compiler/sema/domain.lh:62`，
注释明写"与类型表中的单一 base_type 镜像"）。

```
唯一的成对写点：classes.la:156-159
  :156  context->error = context->types.set_base(...);   // TypeTable 副本
  :159  writable[record_id].base_type = base_type;       // ClassTable 副本
```

读者则**分裂**：

- TypeTable 副本被 `types/layout.la:178/181/245/246/256`、`types/lookup.la:69/86`、
  `types/visibility.la:102`、`context.la:569-570`、`stmt.la:1113`、
  `semantic/lifetime.la:25/139/142/143`、`backend/x86_64/codegen/abi.la:160-161`、
  `middleend/ir/validation.la:204-206`、`middleend/types/layout.la:12/14/15/19/24`、
  `middleend/types/validation.la:103/109/112`、`middleend/types/mutation.la:116/120` 读取
- ClassTable 副本被 `classes.la:176/192/219/464/759/851/901/947/980/1074/1148/1152/1324`
  与 `types/layout.la:32` 读取

**唯一的同步检查是 `validate_classes` 里的
`require(&validator, record.base_type == type_record.base_type)`（`classes.la:487`）**，
而它运行在**第 17 步**——远晚于第 7 步 `collect_bases`（写入）与第 8 步 `prepare_layouts`（消费）。

**已确认的是双来源不变量，不是上述具体失败链。** `extend_class_layout`
（types/layout.la:32）读取的是 `domain::ClassRecord.base_type`，因此不能据此声称它会用
TypeTable 副本查错记录或必然产生不同 `vptr_offset`。真实风险是两组消费者可能各自看到
不同继承关系；在把它升级为 correctness bug 前，应先构造最小失配状态并证明可达。

较稳妥的收敛方向是让 TypeTable 成为基类关系的唯一事实来源，删除 ClassRecord 镜像；
如果阶段边界暂时要求缓存，也必须由一个协调入口原子更新并立即校验，而不是仅为镜像字段
增加一个 mutator 后继续双写。

### 7.2 `ClassTable` 缺少 focused mutator

实测（截至 39a2d87）：

```
classes.la:  record_data() 43 次   method_data() 15 次
             field_data()   2 次   friend_data()  3 次    合计 63
```

由于 `record_data()` 返回指向 vector 内部的裸指针，**任何交错的 append 都可能使其失效**，
于是代码里遍布"重取指针"惯用法：`classes.la:205 / 763 / 1045 / 1112 / 1118 / 1336`、
`types/layout.la:60`。**漏掉一次就是悬垂写。**

约 11 处直接写内存（`classes.la:159 / 769 / 1047 / 1051 / 1084 / 1095 / 1128 / 1345 / 1450`、
`types/layout.la:45 / 65`）应替换为 focused mutator：
`set_polymorphic` / `set_abstract` / `set_rtti` / `set_virtual_slot` /
`set_vptr_offset` / `set_descriptor_global` / `set_vtable_global`。
同时移除可写的 `*_data()` overload，保留 const projection。`base_type` 不应通过新增
ClassTable mutator 固化镜像，而应按 §7.1 先决定唯一事实来源。

### 7.3 5 处子模块 import 父 facade

AGENTS.md 明令禁止："A submodule may never import its parent facade"。实测 5 处：

| 文件:行 | 自身模块 | import 的父 facade |
|---|---|---|
| `compiler/src/middleend/semantic/functions/ir.la:3` | `…semantic.functions.ir` | `…semantic.functions` |
| `compiler/include/.../semantic/context/lookup.lh:7` | `…semantic.context.lookup` | `…semantic.context` |
| `compiler/include/.../semantic/context/builder.lh:3` | `…semantic.context.builder` | `…semantic.context` |
| `compiler/include/.../semantic/types/lookup.lh:4` | `…semantic.types.lookup` | `…semantic.types` |
| `compiler/include/.../semantic/types/visibility.lh:4` | `…semantic.types.visibility` | `…semantic.types` |

**这 5 处没有构成构建图环**——`library_order()` 成功返回 66 个节点，两两互查无环。
被违反的是"子模块不得 import 父 facade"这条更严的命名层规则。

`functions/ir.la` 的具体用途只有四处，全是纯函数：
`functions::function_parts`（33）、`function_export_name_token`（37）、
`function_receiver_type`（89）、`ordered_function_id`（126/136）。

同模块 `.la` 不能互相 import，因此“再建一个同模块文件供两边 import”不是合法修复。
可选方向是把 `functions.ir` 合回父 `functions` 模块、抽取确有独立消费者的更低层契约，
或像 `stmt/api.lh` 一样把必要入口作为函数指针下传。`context` / `types` 的收缩还需要先缩小
公共状态表面，不能把实现简单塞进一个巨型 facade 接口。

### 7.4 管道丢弃全部 `bool` 返回值

`sema.la:133-161` 的 29 个外部调用**全部以裸语句形式出现，返回值全丢弃**。
逐一比对接口确认 29 个函数签名**全部返回 `bool`**，无一是 void。

这证明当前同时存在两套错误信号，但尚不能直接推出 `bool` 约定空转：某些 pass 可能在
诊断后继续做清理或收集。下一步应逐个证明“返回 `false` 必然设置 `context->error`”以及
“失败后是否允许继续”，再选择统一短路、显式状态对象或删除冗余返回值。

### 7.5 probe 与 access/operators 双轨 lowering

`probe`（纯类型试解）与 `access` / `operators`（生成 IR）对同一语法树**各写一遍规则**。
`probe/operators.la` 重复实现了 `operators.la` 已有的 5 个判定函数
（`cast_is_valid`、`binary_is_comparison`、`binary_is_arithmetic`、`binary_is_bitwise`、
`pointer_arithmetic_is_allowed`，见 `probe/operators.la:3/101/109/117/124`）——
只是把多子句条件拆成 named boolean。

**这是最易漂移的地方**：两处 `cast_is_valid` 写法已经不同，当前行为等价，
但任何规则变更需同步两处。应抽取无副作用的共享谓词；probe 不发 IR、lowering 会发 IR，
两条流程本身不应为消除重复而强行合并。

### 7.6 `expr/` 目录混放文件与子目录

`compiler/src/middleend/semantic/expr/` 同时放着 8 个 `.la` 文件和 `probe/` 子目录
（不符合新建或重写目录的迁移目标）。`audit` 的混放检查**只看
`compiler/src/backend/x86_64` 一棵子树**（selfhost.py:521），所以 `expr/` 当前不被检查。
这属于应随 expr 重写收敛的遗留布局，不是要求立刻机械搬文件的全仓门禁失败。

### 7.7 长 if 链清单

| 位置 | 规模 | 应改为 |
|---|---:|---|
| `expr.la:41-154` | 20 个串行 `if (node.kind == ...)` | switch |
| `stmt.la:942-1035` | 18 个串行 `if (node.kind == ...)` | switch |
| `types.la:524-784` `resolve_type_node` | 约 50 分支 if/else-if | switch + 职责函数 |
| `types.la:273-319` `builtin_type` | 15 个 `if (kind == ...)` | **教科书式的枚举映射**，AGENTS.md 明令须为数据表 + 循环 |
| `stmt.la:282-311` `assignment_operation` | 10 个 `if (operation == ...)` | 数据表 + 循环 |
| `consteval/engine.la:223-286` `eval_binary` | 16 个 `if (operation == ...)` | switch |
| `consteval/engine.la:401-468` `expression` | 13 个 `if (node.kind == ...)` | switch |

**做对了的反例**（说明语言能力是够的）：`classes.la:30-43` `access_from_flags`、
`types.la:12-31` `declaration_type_kind`、`stmt.la:32-44` `memory_initializer_kind_is_valid`、
`functions.la:41-70` `function_part_kind`、`intrinsics.la:423-459` 与 `896-926`、
`generics/instances.la:21-34`、`domain/*.la` 全篇。

### 7.8 超两子句条件清单（19 处）

```
5 个算符  semantic/consteval.la:121
4 个算符  semantic/attributes.la:178
          semantic/consteval/engine/execute.la:285
          semantic/expr/numeric.la:294
          semantic/expr/operators.la:597
          semantic/intrinsics.la:485
3 个算符  semantic/attributes.la:197            semantic/attributes.la:217
          semantic/consteval.la:27              semantic/consteval.la:378
          semantic/consteval/engine.la:546      semantic/consteval/engine/execute.la:92 (while)
          semantic/consteval/engine/execute.la:174
          semantic/context/lookup.la:6          semantic/context/lookup.la:95
          semantic/context/lookup.la:219        semantic/context/lookup.la:330
          semantic/expr/numeric.la:155          semantic/expr/strings.la:79
```

此外 `intrinsics.la:850-851` 与 `884-886` 是 `||` 套 `&&` 的三段式复合条件，
虽顶层只有两个 `||`，实质上仍超预算。

### 7.9 其他

- **三处重复的 DFS 状态缓冲样板**：`classes.la:774-793`（`prepare_layouts`）、
  `1161-1181`（`analyze_method_hierarchy`）、`1350-1372`（`create_descriptors`），
  各自 `bytes::with_capacity` → `states.length = record_count` → `memory::fill` →
  循环 → `bytes::release`。三种 DFS 的访问语义并不完全相同，不应先造一个大而泛的
  `HierarchyWalk`；若进一步核对后确认共享的是同一资源不变量，可抽取一个只拥有状态缓冲的
  小型 RAII class，遍历策略仍留在各 pass。三处对 `visiting` 的处理还**不一致**：
  `prepare_class_layout:753-755` 与
  `analyze_class_hierarchy:1140-1142` 静默返回，`create_descriptor:1312-1315` 视为错误
- **`enum AccessFlagEncoding`（classes.la:18-20）把 `syntax::SyntaxFlag` 的数值
  （524288 / 1048576 / 2097152）硬编码为 switch 目标**，而紧邻的 27-29 又从枚举重算同一掩码。
  语法位一旦移动，switch 静默失配，所有成员退化为 `invalid`
- **`RecordValidator` + `require()`（classes.la:14-24）** 是手写累加器，失败后仍会求值完
  所有谓词（无短路）；改用命名布尔值 + 早返回更符合"早返回/命名谓词"要求
- **`functions.la:661-695` 的 `@export_name` 碰撞检测是 O(n²) 双重循环**；先以基准和规模
  证明它是热点，再决定是否引入索引
- **`numeric.la` 的 660 行浮点解析与大数运算**与"表达式 lowering"概念边界较弱；
  可在 expr 目录迁移设计中按十进制转换职责拆成同模块实现族，避免额外制造单子目录
- **`expr/api.la`（17 行）与 `expr/base.la`（12 行）** 都很短，但行数本身不构成违规；
  `api` 若维持真实的下传入口边界可以保留，`base` 若长期只有一次转发再考虑并入相邻职责
- **`classes/model.la:183-184` 与 `generics/storage.la:212-213` 的空 `deinit`** 是组合成员
  自动析构的既定 RAII 惯用法；应在架构契约中统一说明，不必在每个空函数旁重复注释
- **`context/lookup.la` 与 `context/builder.la` 被写成了真子模块**，而 AGENTS.md 与
  `docs/sema.md` 都把它们列为 `context` 的**同模块实现拆分**。
  这正是 `SESSION.md` 记录的下一批要处理的对象
