# 后端：代码生成、汇编器、对象格式、ELF、链接器

> **状态与权威性：** 本文是绑定提交 `39a2d87` 的非规范代码审计快照，用于导航、
> 现状分析和技术债发现。与 `AGENTS.md`、语言规范、架构契约或当前源码冲突时，
> 以后者为准；文中的行号和计数不会随重构自动更新。

`compiler/src/backend/x86_64/` 共 31 个实现文件、9,714 行，分五个模块。
**全部由 Luna 自写**——宿主的 `as`、`ld`、LLVM 都不参与流水线。

| 模块 | 文件 | 行数 |
|---|---|---:|
| `codegen` | 10 | 3,787 |
| `assembler` | 6 | 2,088 |
| `elf` | 4 | 1,692 |
| `object` | 6 | 1,418 |
| `linker` | 5 | 729 |

后端是全仓**风格达标率第二高的区域**（仅次于前端）：`while` 仅 4 处，
超两子句条件**实际为 0**，长 if 链基本不存在，0 行超过 120 列。

行号截至提交 `39a2d87`。

---

## 0. 流水线

```
汇编文本
  │
  ├─ assembler::assemble → object::Object
  │     ├─ object::serialize  → LUNAOBJ1      （内部自有格式，默认）
  │     └─ elf::save          → ELF64 ET_REL  （--emit elf，FFI 边界）
  │
  └─ object::ObjectSet ── linker::link ── 静态 ELF64 可执行文件
```

linker **只消费一种内存表示**（`ObjectSet` / `ObjectView`）。
ELF 与 LUNAOBJ1 的格式分支归一在入口：driver 嗅探魔数 `0x7f 'E' 'L' 'F'`
分流 `elf::load` / `object::deserialize`，linker 自身不含任何格式分支。

---

## 1. Codegen（10 文件 3,787 行）

### 1.1 三阶段

`emit_assembly`（`codegen/facade.la:63`）构造 `CodeGenerator`，`run()`（facade.la:14-28）
依次执行 `AbiLayout.build` → `FramePlan.build` → `emit_module()`。

`emit_module`（module.la:392-405）顺序发射：全局数据 → `.extern` 声明 → `_start` →
各函数 → `.note.GNU-stack`。每个函数由 `emit_function`（module.la:275-390）处理。

### 1.2 没有寄存器分配——全部 spill 到 slot

**这是理解本后端的关键判断。**

- `frame.la:130-143 add_values` 为**每一个** `ir::Value` 无条件分配一个 8 字节、
  8 对齐的 "scalar home"
- `add_slots`（frame.la:109-128）为 slot 分配；`is_memory` 用类型大小/对齐，
  否则**同样退化为 8/8**（frame.la:120-122）
- `add_call_results`（frame.la:145-165）为聚合调用返回值分配 `aggregate_call_result`
- 变参函数额外分配 176 字节 register save area（frame.la:203-209）

寄存器只作 scratch：`%rax`/`%rcx`（support.la:445-451）、`%rdx`（value.la:100）、
`%r11`（聚合指针，callee.la:11/79）、`%r10`、`%xmm0`/`%xmm1`（conversion.la:22-33）、
`%xmm15`（callee.la:220）。

典型序列是 **load_rax → 运算 → store_rax**，即每条 IR 指令都经内存往返。
栈帧与代码体积随 IR 规模线性膨胀——这是有意取舍（correctness-first，no optimization），
但文件内缺少一句注释说明代价。

### 1.3 System V 分类在 `abi.la`

`classify`（abi.la:178-208）先判 >16 字节或对齐 >16 → MEMORY；否则 `merge_type`
（abi.la:128-176）按 array / record / class base / `bound_method` 递归展开
（`merge_leaf`，eightbyte 归并，`class_merge` 格见 abi.la:69-86）。

- 参数分配：`build_scalar_parameter`（324-350，INTEGER 占 6 个 GPR、SSE 占 8 个 XMM，
  溢出进栈）与 `build_aggregate_parameter`（352-413，按 eightbyte piece 计数，
  寄存器不够则**整体降级为 MEMORY** 走栈）
- 返回：`build_return`（284-322），MEMORY 类置 `hidden_return_pointer`
  并**占用第 1 个 GPR**
- 函数指针签名被**合成为额外的 Function ABI 记录**（abi.la:480-512），
  间接调用时用 `function_pointer_abi_index`（call.la:453-463）定位

### 1.4 浮点与 SSE

`float_move`（support.la:480-485）在 movss/movsd 间选择；`float_binary_mnemonic`
（value.la:459-488）选 SSE 算术；比较用 `ucomiss`/`ucomisd` + `setcc`，
并对 `==`/`!=`/`<`/`<=` 追加 `setnp`/`setp` **处理 NaN**（value.la:554-591）。

整数↔浮点转换带显式范围检查，越界 `ud2` trap（conversion.la:94-179）；
无符号 64 位转浮点走 `shrq/orq/addss` 修正路径（conversion.la:74-90）。

### 1.5 栈帧

`module.la:339-357` 发射 `pushq %rbp; movq %rsp,%rbp`；
若 `frame.dynamic_alignment > 16`（frame.la:218-220）追加 `andq $-N,%rsp`；
再 `subq $frame_size`。此时所有 frame 引用改为 rsp 相对：
`append_stack`（support.la:328-342）用 `call_frame_depth + frame_size - offset`，
其中 `call_frame_depth` 补偿调用期间尚未回收的 outgoing 参数区（codegen.lh:173-174）。

尾声统一 `jmp .Lbf<N>_return` → `leave; ret`（module.la:383-388），即**单一返回点**。

`asm fn` 是 naked 函数：body 字符串直接贴到 label 后，不生成 prologue/epilogue
（module.la:328-338）。

`_start`（module.la:110-126）手工写 16 字节对齐、`ldmxcsr` 载入 `0x1f80`、exit syscall。

### 1.6 变参

`callee.la:74-123 emit_register_save` 保存 6 个 GPR 到 +0..+40，`testb %al,%al`
门控后保存 xmm0-7 到 +48..+175；`emit_va_start` / `emit_va_arg`（callee.la:125-197）
操作 gp_offset / fp_offset / overflow / save area；调用侧 `call.la:526-607`
分类额外实参并把 vector 总数写进 `%al`。

### 1.7 correctness-first 的具体体现

- `emit_overflow_flag`（value.la:518-526）直接读紧邻 `add`/`sub`/`imul` 留下的标志，
  注释（value.la:516-517）明示"其间只发射不改标志的 mov"
- 除法对 `INT_MIN/-1` 主动发射 `divl %ecx`（ecx=0）触发 `#DE`（value.la:385-392）
- `memory_copy` 用 `rep movsb` 并做重叠方向判断（value.la:124-151）
- **无任何窥孔优化或指令调度**

---

## 2. Assembler（6 文件 2,088 行）

### 2.1 流程

`run()`（facade.la:12-49）逐行扫描 → `assemble_line`（source.la:376-388）三分派：
`name:` → `define_label`；`.` → `directive`；否则 `instruction`。
收尾 `resolve_fixups()` + `validate_symbols()`。

### 2.2 编码表是数据表 + 线性查表，不是 if 链

`rules.la:17-136 initialize_rules` 用 `encoding_rule(...)` 填充 **87 条 `EncodingRule`
与 30 条 `ConditionRule`**（容量见 assembler.lh:108-110）；
`find_encoding`（rules.la:138-146）与 `condition`（148-160）都是 `for` + `equal`。

`jcc` / `setcc` 在 `encode`（encoding.la:722-741）按前缀 `j` / `set`
合成 `0F 8x` 与 `0F 9x`。

### 2.3 编码器结构

| 函数 | 位置 | 职责 |
|---|---|---|
| `emit_prefix_opcode` | encoding.la:138-157 | legacy prefix（0x66）+ REX（W/R/B/X，`rex_for_rm` 124-136）+ opcode |
| `emit_modrm` | encoding.la:159-215 | mod=00/01/10、SIB 合成、scale 编码、RIP-relative（mod=00/rm=101） |
| `emit_rm` | encoding.la:217-222 | 组合两者 |
| `encode_rule` | encoding.la:656-720 | 按 `EncodingKind` 用 `switch` 分派到 18 个特化发射器 |

字节操作时有 `force_byte_rex`（encoding.la:294-296）处理
`spl`/`bpl`/`sil`/`dil` 需要 REX 前缀的细节。

### 2.4 重定位的产生与解析

**产生**：

- `emit_relative`（encoding.la:224-240）——jmp/call 写 4 字节占位，记 branch/call
- `append_symbol_address`（encoding.la:114-122）——`.quad sym` 写 8 字节占位，
  absolute64，且只允许在 rodata
- `emit_modrm` 的 RIP 分支（encoding.la:166-179）——写 4 字节占位，rip_relative

**解析**：`resolve_fixups`（source.la:442-523）

- 数字标签走 `numeric_target`（408-440，前向取最小大者、后向取最大小者）
- 同 section 的符号直接 `patch`（390-406）回填 disp32
- 跨 section 或未定义则生成 `object::Relocation`
  （pc32 addend −4 / plt32 / absolute64 addend 0）

`validate_symbols`（525-539）拒绝无 linkage 符号。

### 2.5 操作数解析

`parse_operands`（operands.la:443-477）带括号深度的逗号切分，最多 3 个；
`parse_operand`（413-441）按 `$` / `%` / `(` / `*` 前缀分派；
**`parse_register`（273-341）的 32 个传统寄存器名是 `names` / `lengths` / `widths`
三张表 + 循环（220-271）**，xmm0-15 与 r8-r15 走数字解析（上限 15，低位寄存器拒绝走 `r` 形式）；
`parse_memory`（343-411）解析 `disp(base,index,scale)` 与 `sym(%rip)`，
index 禁止 `%rsp`（operands.la:393）；`parse_bits`（139-185）是溢出安全的十/十六进制解析。

---

## 3. Object：LUNAOBJ1 自有对象格式（6 文件 1,418 行）

### 3.1 格式

```
magic "LUNAOBJ1" (8) + version u32 + header_size u32 + 12×u64 = 112 字节
随后裸 payload 依次：text / rodata / data / names
symbol 记录     7×u64 = 56 字节（含 1 个保留字）
relocation 记录 5×u64 = 40 字节
```

全定长 u64、无 padding、无 section header 表 → `expected_size`（reader.la:87-113）
能**精确**算出应有长度并与 `input_size` 严格相等（reader.la:230）。

### 3.2 为什么不用 ELF 做自举格式

ELF 是给宿主工具链的 **FFI 边界**（`--emit elf`），LUNAOBJ1 是**内部格式**：

- 只有 4 个 section 概念，没有 section header 表 / shstrtab / strtab /
  program header / `e_shnum` 扩展 / COMDAT / TLS / REL 等分支
- object reader（276 行）比 ELF reader + format（约 967 行）明显更小，且能被
  `view_is_valid`（object.la:170-217）一次全量形式化校验；差距约 3.5 倍，不是一个数量级
- 格式保留"保留字段必须为 0"（reader.la:65、177）这类封闭协议检查。当前 ELF reader
  同样会拒绝其支持子集之外的类型和 flags；差异在于 LUNAOBJ1 的允许表面更窄，而不是
  ELF 天生无法拒绝未知位

### 3.3 `ObjectSet`

固定容量 128 个 `ObjectDescriptor`（object.lh:32-34、233），每个 descriptor 把所有权
拍平为 `ObjectContent`（4 组 `data/size/capacity`）+ `ObjectRecords`（2 组裸数组）
+ `ObjectLayout`。

`add(Object const&)`（225-241）深拷贝；`add(Object&&)`（243-254）通过
`take_content` / `take_records`（184-209）detach 接管；`get(index)`（256-280）
现场组装 `ObjectView`；`deinit`（323-331）用 `release_content` / `release_records`
（23-42）释放。

它让 linker 能跨多个对象随机访问，且生命周期不受源 `Object` 的移动/析构影响。

### 3.4 不可信输入的防御分层

`luna.std.checked` 在本后端共 **29 处**，集中在 `object/{reader,writer}.la`（8）、
`linker/layout.la`（11）、`elf/{reader,format}.la`（3）——正好是解析不可信
ELF/LUNAOBJ 输入的地方。

防御层次：

1. header 字段逐项上界（`fields_are_valid` 64-85）
2. **精确长度相等**
3. `append_range` / `append_names` 每次过 `checked::range_is_valid`（136-158）
4. `read_symbol` 校验保留字为 0、flags 高 27 位为 0、function/object 互斥（177-187）
5. `import_*` 只做容量检查，**语义校验延后到 `builder->is_valid()`**（reader.la:265）
6. `view_is_valid` 逐条过 `symbol_is_valid` / `relocation_is_valid`（object.la:132-168）

---

## 4. ELF（4 文件 1,692 行）

### 4.1 load（reader）

`format.la:150-245 parse_header` 校验 magic、`ET_REL=1`、`EM_X86_64=62`、
`e_version=1`、`e_phnum=0`、section 表范围，并支持 `e_shnum==0` 的扩展编号。

**关键设计在 `format.la:22-23` 的注释**：五个 `decode_*`（format.la:37-148）
在边界把不可信整数收敛成**封闭 enum**，下游只用 `switch`。
这样"非法输入"在解析边界就变成"不可能的枚举值"，而不是让非法整数流进后续逻辑。

`classify_section`（reader.la:141-187）拒绝 TLS / `SHF_GROUP` / W+X / `SHT_REL`
（隐式 addend），丢弃未分配的 note/debug；对齐必须 2 的幂。
`layout_sections` / `place_section`（206-254）把 section 合并进
text / rodata / data / bss 四个 region 并记录 `region_offset`。

`load_symbol`（516-563）分派 section / undefined / common / defined；
**local 符号撞名时用 `synthetic_name`（281-308，内嵌 NUL 保证不与输入重名）改名**；
COMMON 在 bss 分配（399-445）。
`load_relocations`（607-684）转 `object::Relocation` 并校验 `symbol_index`
与 `offset+width` 落在目标 section 内。

### 4.2 save（writer）

`save_plan`（144-255）算 section 编号；`save_symbols`（320-354）两趟发射
（先 local 后 global，`sh_info = first_global`）；`save_relocations`（356-410）；
`save_sections`（412-496）建 `.text` / `.rodata` / `.data` / `.bss` / `.symtab` /
`.strtab` / `.shstrtab` / `.rela.{text,rodata,data}` 与空的 `.note.GNU-stack`；
`save_layout`（498-529）对齐排布；`emit_header`（570-590）。

**头部注释（writer.la:4-9）明确输出严格落在 reader 支持子集内，可 round-trip。**

---

## 5. Linker（5 文件 729 行）

`run()`（facade.la:51-83）六步：

```
initialize_placements → collect_globals → layout_regions → layout_image
  → apply_relocations → resolve_entry → emit_executable
```

| 文件 | 行数 | 职责 |
|---|---:|---|
| `facade.la` | — | `run()` 六步编排、`take_result()` |
| `symbols.la` | 152 | `collect_globals`（21-53，建 `map<GlobalName, Global>`，重复定义 → `Error.exists`）、`resolve_symbol`（105-126）、`resolve_entry`（128-151） |
| `layout.la` | 146 | `layout_regions`（52-87，把各输入的四段顺序拼进四个 buffer）、`layout_image`（89-126，定基址与分页） |
| `relocation.la` | 167 | `apply_relocation`（117-148）分派 pc32/plt32（S+A−P，32 位有符号范围检查）、absolute64、absolute32/signed32 |
| `writer.la` | 158 | `build_segments`（84-114，三个 `PT_LOAD`）、`emit_executable`（140-157） |

细节补充：

- `resolve_entry` 找 `_start` 并校验其位于 text 且 kind 为 function
- `layout_regions` 中 text 用 `0x90`（`nop`）填充
- `layout_image` 定 image_base `0x400000`、text_offset `0x1000`，
  rodata/data 各按 4K 分页，bss 紧接 data 之后
- `build_segments` 建三个 `PT_LOAD`：R+X / R / R+W，
  **第三个的 `memory_size` 含 bss**（故文件大小小于内存大小）

---

## 6. 类与所有权

后端是 RAII 重写完成度最高的区域（assembler、ELF object I/O、linker、codegen、
object model 都已做过）。

**class（私有状态 + 方法）**：`AbiLayout`、`FramePlan`、`CodeGenerator`、`Assembler`、
`SymbolTable`、`SymbolName`、`ElfReader`、`ElfWriter`、`Object`、`ObjectBuilder`、
`ObjectReader`、`ObjectWriter`、`ObjectSet`、`StaticLinker`、`GlobalName`。

**passive struct**：`Classification` / `Piece` / `Parameter` / `Function`、`Storage` / `Frame`、
`Register` / `Memory` / `Operand` / `Operands` / `Fixup` / `NumericLabel` /
`EncodingRule` / `ConditionRule`、`InputSection` / `OutputSection` / `SavePlan` /
`SymbolMapping`、`Symbol` / `Relocation` / `ByteView` / `ObjectView` / `ObjectContent` /
`ObjectRecords` / `ObjectLayout` / `ObjectDescriptor`、`Placement` / `Global` /
`RelocationRegion` / `LoadSegment`。

**RAII 已落地**：

- `Object` 有移动构造 + `take_metadata`（object.la:226-231、267-278）
- `ObjectBuilder::take_object` 用 `this->storage as Object&&`（builder.la:309）
- `ObjectSet::deinit` 释放全部（323-331）
- `ElfWriter` 的 `release_scratch`（42-58）在 `take_result`（692-704）与 `deinit`（706-709）
  **两处都调用**——正确的双路径清理
- `CodeGenerator::take_result` 用 `assembly.detach()`（facade.la:36）；
  `StaticLinker::take_result` 用 `executable.detach()`（facade.la:91）

**`friend` 仅用于 `Object` ↔ `ObjectBuilder` / `ObjectSet`**（object.lh:98-99）——
用法克制且理由充分。

**泛型复用良好**：`vector<T>`、`map<SymbolName, SymbolBinding>`、`map<GlobalName, Global>`、
`copy_record_buffer<Value>`（collection.la:62）。

**非拥有借用只有一处**：`ObjectReader` 持 `ObjectBuilder&`（reader.la:4），语义正确。

---

## 7. 技术债

### 7.1 需要基准确认的二次复杂度路径

- `FramePlan::find_storage`（frame.la:283-290）每次寻址都线性扫描，
  而 `append_value` / `append_slot`（support.la:344-352）在**每条指令发射时**被调用
- `function_pointer_abi_index`（call.la:453-463）对每个间接调用线性扫描全部 `type_id`

它们具有二次复杂度上界，但本文没有运行 profile 或规模基准，不能直接称为真实热路径。
先保留清晰的线性实现；只有基准证明其主导编译时间时才引入索引。

### 7.2 变参分类重复三遍

`call.la:526-540`（预算 frame_size）、`554-570`（栈参数）、`585-607`（寄存器参数）
各自重算 `variadic_extra_parameter`。分类规则一旦改动需同步三处。

### 7.3 隐式标志契约无静态校验

`emit_overflow_flag`（value.la:518）依赖"上一条算术指令的标志未被破坏"。
这个契约只写在注释里（value.la:516-517），没有任何静态校验。

### 7.4 死代码

`object/reader.la:60-62` 的 `field_is_usize` 对 `u64` 判 `<= 2^64-1`，**恒真**；
`fields_are_valid:68-72` 的循环因此无效。

### 7.5 names blob 是长度定界的二进制身份

`append_names`（reader.la:148-158）不检查 NUL 终止或 UTF-8，仅靠每个 symbol 的
`name_offset` / `name_length` 落在 names 范围内来间接约束。
这是当前对象格式的有意契约，不是已确认缺陷：符号身份按字节切片比较，ELF local 撞名处理
还会故意生成内嵌 NUL 的 synthetic name。只有公共格式未来要求文本语义时，才应新增
UTF-8/NUL 约束并同步修改该机制。

### 7.6 `ObjectSet` 固定 128 容量是资源合同

object.lh:32-34，超限返回 `out_of_memory` 而非扩容。
当前 CLI 也把 link 输入限制在 128，这是明确且可测试的有界资源合同。只有解除公共输入
上限时，ObjectSet 动态扩容才成为实现任务。

### 7.7 指令表每实例重建

`encodings: [87]EncodingRule` 是 `Assembler` 的成员数组（assembler.lh:142），
在 `init()` 里逐条赋值（rules.la:18-104）。它是否能安全改成模块级常量取决于当前 Luna
对全局常量聚合与初始化的支持；在语言能力和收益都被证明前，这只是候选简化，不是结论。

### 7.8 窄 facade

`elf/facade.la`（14 行）与 `object/facade.la`（17 行）只做 `run()` + `take_result()`
编排。文件短不等于边界错误：它们把公开阶段入口与 reader/writer 私有实现隔开，是有真实
phase responsibility 的 facade。只有出现无意义的二次转发时才应合并。

### 7.9 嵌套三元（可读性债，不违反"两逻辑子句"规则）

`support.la:455-456`（4 层）、`value.la:239-240`、`value.la:439-442`。

### 7.10 两处短分派

- `abi.la:69-86 class_merge`——5 段合并格，本质是查表
- `assembler/encoding.la:571-577 encode_repeat`——movsb/stosb 两分支字符串比较；两分支本身
  不构成长 if 链，无需为表格化增加额外结构

### 7.11 状态驱动的 `while`（仅 4 处）

`assembler/facade.la:18`（源扫描）、`:20`（找 `\n`）；
`codegen/module.la:177`（asm 字符串转义扫描）、`:210`（`\u{...}` 十六进制扫描）。

这四处都在表达开放式或非均匀推进的扫描状态。只有能给出天然边界且不会隐藏游标语义时
才改用 `for`；不因出现 `while` 就机械重写。
