# 测试体系

> **状态与权威性：** 本文是绑定提交 `39a2d87` 的非规范代码审计快照，用于导航、
> 现状分析和技术债发现。与 `AGENTS.md`、语言规范、架构契约或当前源码冲突时，
> 以后者为准；文中的行号和计数不会随重构自动更新。

Luna 的测试用例**本身就是 Luna 程序**，用**退出状态码**表达判定结果。
一次完整 `python3 tools/selfhost.py test` 跑 **450 个期望**：

```
415 行用例期望（tests/expectations.txt）
  + 五套专用协议约 35 项
  = 450
```

行号截至提交 `39a2d87`。

---

## 1. 规模与布局

| 位置 | 内容 |
|---|---|
| `tests/cases/` | **412 个 `.la` 用例** + 8 个 `.lh` + `embed_blob.bin` |
| `tests/expectations.txt` | **415 行**期望 |
| `tests/ffi/` | 手工编码的 ELF64 ET_REL 固件 + `generate_fixtures.py` |
| `tests/relocation_data/` | 重定位数据测试（`ir_codegen.la`、`object_roundtrip.la`、若干 `.s`） |
| `tests/callable_identity/` | 符号名与链接身份测试的辅助源码 |
| `tests/modules/` | 供 `UNITS` 显式指定模块序的辅助模块 |

412 个文件名与 expectations 中的 412 个不同用例名**严格一一对应**（无孤儿、无缺失）。
`415 − 412 = 3` 行来自同一文件的重复期望
（`multiple_implementation_units.la`、`class_opaque.la`、`fail_class_opaque_duplicate.la`，
见期望表 L185/186、L309/310、L323/324），目的是用**不同 `UNITS` 顺序验证模块序不变性**。

---

## 2. 用例分类全景（412 个）

| 类别 | 数量 | 代表文件（每类取 3） |
|---|---:|---|
| 负例：类 / OOP（构造、继承、override、super、RTTI、friend、绑定方法、opaque） | 86 | `fail_class_abstract_construct.la`、`fail_class_super_order.la`、`fail_class_rtti_nonroot.la` |
| 核心语言 / ABI / 属性（控制流、聚合、指针、intrinsic、字面量） | 67 | `structured_control_flow.la`、`goto_basic.la`、`bitfields_basic.la` |
| 负例：类型 / 属性 / intrinsic / 控制流 | 56 | `fail_bitfield_sum.la`、`fail_packed_field_address.la`、`fail_array_length.la` |
| 正例：类 / 方法 / 绑定方法 | 33 | `class_virtual_dispatch.la`、`class_rtti.la`、`class_bound_methods.la` |
| 整数与转换（含 16 个 SIGFPE 陷阱） | 27 | `i64_boundaries.la`、`signed_arithmetic.la`、`defined_i32_semantics.la` |
| 负例：模块 / 导入 | 21 | `fail_import_cycle.la`、`fail_unreachable_module.la`、`fail_ambiguous_qualifier.la` |
| 负例：泛型 | 19 | `fail_generic_recursive_type.la`、`fail_generic_expansion.la`、`fail_generic_parameter_limit.la` |
| 负例：默认参数 | 14 | `fail_default_placement.la`、`fail_default_repeated.la`、`fail_default_nonconstant.la` |
| 负例：重载决议 | 14 | `fail_overload_literal_ambiguous.la`、`fail_overload_no_match.la`、`fail_overload_nested_ambiguous.la` |
| 正例：重载决议 | 13 | `overload_calls.la`、`overload_nested_once.la`、`overload_function_values.la`、`overload_pointer_arithmetic.la` |
| 浮点（含 8 个 float→int 陷阱） | 12 | `floating_operations.la`、`scalar_conversions.la`、`float_to_integer_nan_trap.la` |
| 负例：生命周期 / 引用 / 析构 | 10 | `fail_unavailable_copy.la`、`fail_destructor_signature.la`、`fail_goto_lifetime.la`、`fail_nontrivial_union.la` |
| 模块 / 导入正例 | 9 | `qualified_basic.la`、`selective_basic.la`、`module_input_order.la`、`multiple_implementation_units.la`、`class_opaque.la` |
| 泛型正例 | 8 | `generic_functions.la`、`generic_class.la`、`generic_abi.la`、`generic_permutation.la` |
| 生命周期 / RAII / 引用 | 7 | `raii_destructors.la`、`references.la`、`move_only_resource.la`、`trivial_relocation.la`、`lifetime_temporaries.la` |
| 负例：标准容器（非平凡元素 static_assert 拒绝） | 5 | `fail_vector_nontrivial.la`、`fail_deque_nontrivial.la`、`fail_map_nontrivial.la` |
| 标准库正例 | 4 | `std_containers.la`、`container_foundation.la`、`type_table_validation.la`、`std_string_view.la` |
| 默认参数正例 | 4 | `default_arguments.la`、`default_overloads.la`、`default_interface.la`、`default_function_pointer.la` |
| 负例：值类别 / 左值 | 3 | `fail_temporary_member_address.la`、`fail_temporary_index_address.la`、`fail_temporary_member_assignment.la` |

---

## 3. 三种期望形式

| 形式 | 行数 | 典型 |
|---|---:|---|
| 普通退出码 | 187 | `return_42.la 42`（L1）；`division_by_zero.la -8`（L16）；`aggregate_parameter.la 0`（L59） |
| `FAIL <kind>` | 228 | `fail_class_private_field.la FAIL inaccessible_member`（L274） |
| 带 `UNITS` | 54（FAIL+UNITS 33、正例+UNITS 21） | `type_table_validation.la 0 UNITS ...`（L406，18 个源码单元，一次编入 6 个 `luna.compiler.types` 实现单元） |

退出码分布：`42` 出现 129 次（成功）、`0` 出现 21 次（正例内部用 `return 1/2/3` 细分失败点）、
负信号 30 次。

**`UNITS` 的三种用途**：

1. 链接真实库源码——`std_string_view.la`（L405）拉入 `library/src/std/string_view.la` 等 5 个单元
2. 显式单元顺序——`module_input_order.la 42 UNITS tests/modules/mathx.lh tests/cases/module_input_order.la`
   （L196，接口在前）
3. 模块图诊断——`fail_unreachable_module.la`（L183）、`fail_duplicate_interface.la`（L184）
   两次喂同一 `.lh`

---

## 4. 被钉死的语言契约

这批用例的真正价值不是"功能可用"，而是**把 C 语言里的未定义行为钉成确定性行为**。

### 4.1 整数语义

`defined_i32_semantics.la:4-7` 要求：

- `2147483647 + 1` 回绕成 `-2147483648`
- `1 << 32 == 1`——**移位按位宽取模，不是 UB**
- `-1 >> 32 == -1`——算术右移
- `0 - -2147483648 == -2147483648`——二补数回绕

`signed_arithmetic.la:4-6` 钉住 C99 截断除法：`-17/5 = -3`、`-17%5 = -2`。

### 4.2 陷阱而不是 UB

30 个负退出码期望 = **16 个 `-8`（SIGFPE）** + **14 个 `-4`（SIGILL，即 `ud2`）**。
后者覆盖：空指针解引用（`null_dereference.la`）、数组越界读写
（`array_out_of_bounds.la`）、7 个 float→int 越界 / NaN / Inf
（`float_to_i64_upper_trap.la` 等）、空函数指针调用
（`function_pointer_null_trap.la:8-9`）、类空接收者（`class_null_receiver.la`）。

**编译器把 UB 编译成确定性陷阱**——这是全体系最重的契约之一。

### 4.3 求值顺序

`short_circuit.la:6,10` 用 `false && (1/0==0)` 证明右操作数不求值；
`structured_control_flow.la:95-97` 进一步在三目 `?:` 中验证（`false ? 1/0 : 5`）。

### 4.4 goto 的子集规则

`goto_basic.la:40-51,67-80` 仔细区分合法（跳转不跨越活跃局部变量初始化）与非法；
`fail_goto_initialization.la:5-8`（跳过 `let skipped`）报 `invalid_goto`；
`fail_goto_lifetime.la:14-16` 用带 `deinit` 的类证明**跳转绕过非平凡对象生命周期**
也属 `invalid_goto`。

### 4.5 对象模型

- `class_bound_methods.la:55` 断言 `sizeof(Callback)==16 && alignof==8`——
  **bound method 是 16 字节胖指针**（offset 0 是 receiver，offset 8 是 entry）
- `class_rtti.la:44,47` 验证 `@type_is` / `@type_cast`（含 `volatile` 限定与 null 安全）
- `class_virtual_dispatch.la:63-77` 验证三级链的值语义传参后 vptr 仍正确，
  以及 `super.read()` 是**非虚**调用
- `raii_destructors.la:106-126` 用"把数字拼进 `*target`"的技巧**精确编码析构顺序**：
  嵌套 `21`、组合 `921`、数组 `21`、继承 `954`、成员初始化列表
  `init(...): first=..., second=...`

### 4.6 负例覆盖的拒绝场景

共 **90 种诊断种类**被覆盖（`enum DiagnosticKind` 共 122 项，定义在
`compiler/include/luna/bootstrap/middleend/semantic/context.lh:17-43`）。

代表性场景：访问控制（`fail_class_private_field.la:8`）、切片
（`fail_class_value_slicing.la:28` → `no_matching_overload`）、非虚 override
（`fail_class_override_nonvirtual.la:8`）、`super.init()` 位置
（`fail_class_super_order.la:18-19`）、friend 单向性（`fail_class_friend_reverse.la:20-22`）、
RTTI 只能在根类（`fail_class_rtti_nonroot.la:8`）、容器非平凡元素 static_assert
（`fail_vector_nontrivial.la:19` → `assertion_failed`）、泛型无限展开
（`fail_generic_expansion.la:4` → `generic_resource_limit`）、33 个类型参数超限
（`fail_generic_parameter_limit.la:3-5`）、`@export_name("_start")` 保留名
（`fail_export_name_reserved.la:3`）、临时量取地址
（`fail_temporary_member_address.la:12` → `invalid_lvalue`）。

---

## 5. 五套专用协议

这些契约断言的是"**编译产物的符号形状 / 字节确定性 / 链接器拒绝**"，
退出码协议根本无法表达，所以必须独立。

### 5.1 `execute_tool_cli_tests`（selfhost.py:1109-1266）

10 项纯 CLI 契约 + 1 项 fixed protocol。

前 10 项**逐字节比对 `(returncode, stdout, stderr)`**：`--help` / `--version` 的精确文本
（1120-1127）、无子命令与未知子命令均退出 125 并打 root usage（1128-1129）、
三个子命令各自的 usage/version（1130-1171）。

第 11 项 `tool-fixed-protocol`（1192-1262）在 `out/tests/tool-fixed-protocol/` 手写
`bootstrap-stage-version`（`LUNA-STAGE/1 LUNA/1`）、`bootstrap-stage-mode`（`E`）与
`bootstrap-stage-unit-0.luna`，然后**无参数**调用 `compile` / `assemble` / `link` 三次
（每次必须以 42 退出、stdout/stderr 全空），硬编码断言产物长度 + SHA-256：

```
bootstrap-stage-output.s     951 字节  aa5023c1…
bootstrap-object-output.lo   597 字节  d7c2ff72…
bootstrap-link-output       4190 字节  09a52ee7…
```

最后执行产物确认返回 42。**这是对"自举固定点字节确定性"的端到端锁死。**

### 5.2 `execute_callable_identity_tests`（1269-1463）

5 项：

1. **`callable-signature-symbols`**：编译 `tests/callable_identity/signatures.la`
   （`module ci.s;`），从 `expectations.txt` 读 13 行 `<函数名> <签名 hex>`，
   拼 `_L{module_hex}_{name_hex}__{sig_hex}:`（1305-1307，`"ci.s".encode().hex()`）
   在汇编中精确查找。签名 hex 编码了参数类型树；`init` / `inspect` / `mutate` /
   `associated` 的前缀 `010003010100` / `010002010200` 编码了
   **ctor / const / normal / static 的 callable kind**
2-4. **三组"单元顺序置换后汇编字节完全一致"**：`order`（自由函数重载 i32/u32）、
   `method_order`（`Alpha` / `Zed` 两个类的 impl 单元）、
   `inheritance_order`（`Base` / `Derived` 含 `override final`），
   均走 `require_two_unit_order_identity`（1088-1106）
5. **`class-dispatch-shape` + `callable-link-identity`**：前者编
   `class_devirtualization.la` 断言恰好 1 处 `call *%r11` 与 3 个 `.quad` 虚表槽
   （1384-1393）；后者用 `mismatch/` 下同名不同签名的 `callee_i32.la` / `callee_u32.la`
   分别链接 `caller.la`，要求匹配时运行返回 42、签名不匹配时**链接器以 3 退出**（1453-1456）

### 5.3 `execute_frontend_tests`（1475-1534）

3 项。消费端（`lexer.la` / `syntax.la` / `parser.la`）**只 import 接口独立编译**，
再链接 stage 中已构建的真实 `*.lo` 与依赖闭包（1510-1523）。
这样既验证公开 ABI 又验证 RAII 与模块链接。
Lexer 用例断言 67 关键字 / 46 标点 / 12 字面量的 token 序列与 span（`lexer.la:7-9,32-36`）。

### 5.4 `execute_relocation_data_tests`（1537-1663）

2 个大探针 + 1 个确定性链接 + 3 个汇编拒绝。前两者要求退出 42；
`dispatch.s` 双次汇编要求 `.lo` 字节相同（1596-1602）；
三个 `fail_*.s` 要求 `luna assemble` 以 2 退出且 stderr **精确**为
`assembler:<行号>\n`（1648-1656，行号 3/2/2）。

### 5.5 `execute_ffi_tests`（1666-1828）

8 行期望，三种 mode。`ffi_compiler()`（808-841）用 `-dumpmachine` 校验三元组须匹配
`^(x86_64|amd64)(-|$)`，否则 **SKIP 而非 FAIL**。

---

## 6. FFI 与重定位固件

### 6.1 `tests/ffi/`

`generate_fixtures.py` 用**纯 Python `struct.pack` 逐字节编码 ELF64 ET_REL**
（`build_object` 64-148），不依赖任何宿主工具链，产物已签入。

`answer.o`（1080 字节）含 `.text` / `.data` / `.rela.text` / `.rela.data` / `.symtab` /
`.strtab` / `.shstrtab` / `.note.GNU-stack` 八节，本地符号 `add7` / `lucky` / `lucky_ptr`
与全局 `ffi_answer` / `ffi_answer_plus`，三类重定位 `R_X86_64_PC32`（188）、
`PLT32`（188）、`64`（189）。

**四个损坏变体**（验证 linker 对不可信输入的拒绝）：

| 固件 | 损坏方式 | 期望 |
|---|---|---|
| `missing.o` | 无全局符号 | 链接 exit 3 |
| `bad_class.o` | `EI_CLASS` 改为 1 | exit 2 |
| `bad_reloc.o` | 首条 RELA 改为 `R_X86_64_GOT32` | exit 2 |
| `truncated.o` | 砍掉两节头 | exit 2 |

`shims.lh` / `shims.la` 是 freestanding libc 填充层，每个定义带 `@export_name`
以匹配 gcc 目标文件引用的 C 符号名。

`c_calls_luna.la` + `cmain_c_calls_luna.c` 是**反向验证**：Luna 编译 → `--emit elf` →
宿主 `gcc -no-pie` 链接 → 运行返回 42。这是整个仓库唯一让宿主工具链接器接触产物的地方，
且它验证的是 FFI 方向，不参与自举。

### 6.2 `tests/relocation_data/ir_codegen.la`（当前 1,004 行）

**它本身是一个 Luna 程序，import 了真实的编译器模块**
（`luna.compiler.x86.codegen`、`luna.compiler.ir`、
`luna.bootstrap.middleend.semantic.context` 等），
因此能在**一次既有工具链启动内**直接构造并验证内部数据结构——
这正是它存在的意义：**不增加额外的工具链启动**。

同一 relocation probe 中已有的 ClassTable、GenericTable、SymbolTable 与
CallableTable 契约保持不变；当前工作树在这些检查后新增
`lowering_state_lifecycle_is_valid()`。该大用例直接覆盖：

- `LoweringState` 的空状态、函数/unit 生命周期、typed local/temporary
  存储和只读计数/投影；
- 真实两个 local 的 label 快照与 pending-goto 快照，含连续 published
  range 及显式 unpublished tail discard；
- loop label、break/continue control targets 与 local-count watermark；
- move 后目标状态及 moved-from 空状态、`clear_function()`，以及首个
  `invalid_argument` 的 sticky error 和禁止越过 watermark 的截断。

**ClassTable 部分**：

- `class_table_move_is_valid()`——依次 `append_record` / `append_field` /
  `append_method` / `append_friend`，move 后断言目标
  `record_count()==1 && field_count()==1 && method_count()==1 && friend_count()==1`，
  源为全 0 且 `storage_is_valid()`，三个成员切片连续、元素值正确，且层级/RTTI
  元数据通过 focused mutator 发布。
- `class_table_failure_is_sticky()`——传入无效 type id，
  要求 `append_record` 返回 `invalid_argument`、`record_count()==0`，
  且**后续合法 append 仍被拒绝**（首错粘滞）。

**GenericTable 部分**（`generic_table_move_is_valid()`）：参数连续切片、实例去重
（同参数两次返回同一 `instance_id`）、`additional_instances = 13` 个实例
（含 16→32 bucket rehash）、type/function 反向映射、active-binding 深验证与
`truncate_active_bindings(0)` 回滚、move/moved-from。

`symbol_table_move_is_valid()` 和 `callable_table_move_is_valid()` 位于同一大用例中，
分别覆盖 typed symbol/callable stores、发布顺序、绑定切片、move/moved-from 和
sticky failure。

**之后** `main()` 继续走真实的 lex → parse → `sema::check` →
`ir::Builder.add_global` → 两次 `emit_assembly` 比对字节，并统计
`"    .quad dispatch_target\n"` 出现 2 次。
当前退出码 37 定位 LoweringState 失败，32 / 33 / 35 / 36 分别定位
ClassTable / GenericTable / SymbolTable / CallableTable 失败，34 定位
语义诊断流失败，主路径成功返回 42。

`object_roundtrip.la`（421 行）则是 assembler → `object::serialize` → `deserialize` →
再 serialize → `elf::save` → `elf::load` → 再 save 的**六段字节等价链**（32-63）。

---

## 7. 批处理与隔离

`TestSuitePlanner` 把最多 `TEST_SUITE_CASE_LIMIT = 6` 个合格用例合并进一个可执行文件。
合并时把 `main` 改名为 `run_case_NNNN`，并用词法感知正则 `LUNA_LEXEME_PATTERN`
给所有顶层声明加前缀 `case_NNNN_`，生成 `suite.la` 调 `suite.EXPECT_EXIT(...)`；
suite 返回非 0 时 `_suite_failure_name()` 用序号定位失败用例，并回退到逐例重跑。

所有测试链接都会注入 `syscall_object()` = `objects/syscall.lo`。

---

## 8. 薄弱点

### 8.1 诊断种类覆盖 90/122（73.8%）

32 项从未被测。语义上重要的缺口：

- **`private_type_exposure`(11)**
- **`invalid_enum_underlying`(17) / `invalid_enum_value`(19) /
  `enum_value_overflow`(20) / `duplicate_enum_member`(21)**——
  **整个 enum 子系统零负例**
- `duplicate_local`(28)、`invalid_condition`(31)、`invalid_call`(35)、`unknown_field`(39)
- **`missing_return`(46)、`invalid_switch`(47) / `duplicate_case`(48) /
  `duplicate_default`(49)**——switch 与返回路径检查为零
- `return_type`(45)、`break_outside_control`(43)、`continue_outside_loop`(44)、
  `non_loop_label`(73)、`floating_overflow`(51)、`resource_limit`(52)、
  `internal_invariant`(53)、`object_too_large`(16)、`invalid_member`(41)、
  `invalid_layout_query`(42)、`duplicate_parameter`(26)、`invalid_external`(25)、
  `invalid_class_composition`(97)、`duplicate_implementation`(2)、`invalid_qualified_path`(82)

### 8.2 合并套件的准入代价

实测：187 条非负期望中仅 **121 条（64.7%）** 可进入 suite，形成 21 个 suite；
**294 条必须隔离执行**（228 条 FAIL + 66 条被排除的正例）。

排除原因（多标签）：负退出码 30、含 import 29、带 `UNITS` 21、名字重复 4、
`extern fn` 2、`asm fn` 2、`@export_name(` 1、`@file(` 1、`@embed(` 1。

**`has_imports`（L1010）是最狠的一条**：它把 29 个**正例**永久排除——
所有 `qualified_*` / `selective_*` / `class_opaque` / `std_*` / `move_*` /
`generic_import`，因为合批需要为它们额外生成成百上千个接口单元。

另注：`process_exit(` 在 `SUITE_UNSAFE_SOURCE_MARKERS`（L67-74）中但**当前命中 0 次**
（信号用例已先被 `expected_exit >= 0` 拦下），属于冗余条件。

### 8.3 其他空白

- `docs/instruction-differential-testing.md` 描述的是归档 m0 指令差分管线，当前分支没有
  对应实现；本地空目录或 `__pycache__` 不属于 Git 跟踪的仓库事实，不列入代码债
- 标准库只有 4 个正例入口，**全部靠 `UNITS` 一次性拉入十余个真实源码单元**。
  `std_containers.la`（187 行）、`container_foundation.la`（177 行）内部用
  `test_*() -> bool` 函数链把 pool / deque / list / map / queue / tree
  全部塞进**一个退出码 0**——一旦失败只能得到 `main` 里的一个粗粒度返回编号，
  定位成本高于 suite 的 `case_id` 机制（而这两者恰好被 `UNITS` 排除，
  拿不到 `TestSuite.EXPECT` 的 per-case 编号）
- 浮点当前主要覆盖 f32/f64 四则与转换。`sqrt` / 舍入 intrinsic 若属于已支持表面，应补
  行为用例；`fma` 等未承诺特性应列为未来功能面，而不是当前测试回归
- `wide_strings.la` 之外**没有 UTF-16/32 的库级测试**
