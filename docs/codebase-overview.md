# Luna 代码库总览

> **状态与权威性：** 本文是绑定提交 `39a2d87` 的非规范代码审计快照，用于导航、
> 现状分析和技术债发现。与 `AGENTS.md`、语言规范、架构契约或当前源码冲突时，
> 以后者为准；文中的行号和计数不会随重构自动更新。

本文件是 Luna 仓库的**导航性总览**，基于对整个代码仓的一次全量通读撰写：

- 基线提交包含 692 个 Luna 源文件（`.la` / `.lh`），60,829 行
- 基线提交包含 51 份 `docs/` 文档，11,937 行；本次审计另新增 6 份分册
- 412 个可执行测试用例 + 415 行期望表
- 仓库共 5 个 Python 文件，4,705 行；其中 `tools/` 下 4 个、4,460 行，另一个是
  245 行的 `tests/ffi/generate_fixtures.py`

**状态快照**：截至提交 `39a2d87 refactor: modernize semantic metadata ownership`。
本文件引用的行号会随重构漂移，涉及精确位置处均标注核对点，请以工作树为准。

配套分册：

| 分册 | 内容 |
|---|---|
| `codebase-frontend.md` | 词法、语法树、解析器 |
| `codebase-middleend.md` | 类型表、IR、语义分析（全仓最大子系统） |
| `codebase-backend.md` | 代码生成、汇编器、对象格式、ELF、链接器 |
| `codebase-runtime-tooling.md` | 标准库与运行时、驱动器、构建与格式化工具 |
| `codebase-tests.md` | 测试体系、被钉死的语言契约、覆盖薄弱点 |

---

## 1. 项目定位

Luna 是一门 C23 衍生的系统语言。它的**编译器、汇编器、链接器全部用 Luna 自身写成**——
编译器源码就是本仓库的 `compiler/`，它编译自身。

- 唯一目标：`x86_64-unknown-linux-gnu`，System V ABI，ELF64
- 生成的程序是 freestanding 的，不链接 libc
- 引导链上唯一的二进制信任根是 `anchor/` 下一个固定的 `luna` 可执行文件

Luna 语言本身的完整规范见 `docs/language.md`（1,376 行）。核心特性摘要：

- **模块**：`export module X;` / `module X;`，一接口多实现；import 只绑定限定符，不可重导出
- **类型**：无隐式提升、无 truthiness，`as` 是唯一转换通道
- **类**（M3）：显式 `pub`/`prot`/`priv`、隐式 `this`、单继承、`virtual`/`abstract`/`override`、
  vtable/vptr、opt-in `@rtti`、同模块 `friend`、`opaque class`、两字 `method fn` bound method
- **泛型**（M4）：`<T>` 跟在声明名后，全单态化、无运行时字典，无 CTAD/特化/SFINAE/概念
- **生命周期**（M5）：`T&` / `T const&` / `T&&`、`init`/`deinit`、copy/move 构造；
  `move` 是标准库的普通泛型函数，**不是关键字**
- **无预处理器、无宏、无 `_Generic`**

---

## 2. 不可协商边界

来自 `docs/architecture.md:3-44`，任何改动都不得违反：

1. **绝不把 Luna 翻译成 C**，也不经过任何宿主工具链。宿主的 `as`、`ld`、LLVM 都不在流水线里。
   唯一的外部编译器接触点是 `tests/ffi/` 的反向验证（Luna 产出 ELF，交给 gcc 链接）。
2. **生成的程序是 freestanding 的**。运行时与系统调用封装在 `library/`，不链接 libc。
3. **唯一目标是 `x86_64-unknown-linux-gnu`**。`isize` / `usize` 是目标字长。
4. **一个 Luna 模块最多一个接口（`.lh`）加若干实现单元（`.la`）**。路径不必同目录，
   分组由 `tools/selfhost.py` 的 `LIBRARIES` 显式指定。
5. **新模块必须登记进 `LIBRARIES`**，否则 `audit` 失败，固定点就覆盖不到它。

---

## 3. 仓库布局

```
anchor/     单一固定点多命令 luna 可执行文件 + SHA256SUMS + PROVENANCE.md。唯一二进制信任根
library/    标准库与运行时：include/luna/ 是接口（镜像模块名），src/ 是实现
compiler/   编译器自身：include/luna/{bootstrap,compiler}/ + src/{frontend,middleend,backend}
drivers/    单一 luna 可执行文件的 CLI 服务与入口
tests/      可执行行为用例（退出码即判定）、FFI 固件、重定位数据
tools/      构建系统（selfhost.py / build.py）、格式化器（refmt.py）、发布工具
docs/       基线 51 份设计记录 + 本次 6 份审计分册。部分是归档 m0 种子的历史文档
out/        构建产物（stage-transition / stage-next / stage-fixed）
```

`compiler/` 的规模分布（行数）：

| 子树 | 行数 | 说明 |
|---|---:|---|
| `src/middleend/semantic/` | 22,514 | 语义分析，全仓最大子系统 |
| `src/backend/x86_64/` | 9,714 | 代码生成、汇编器、对象、ELF、链接器 |
| `src/frontend/` | 4,283 | 词法、语法树、解析器 |
| `src/middleend/types/` | 1,408 | 类型表 |
| `src/middleend/ir/` | 1,879 | 中间表示 |
| `src/middleend/sema.la` | 195 | 语义管道入口 |

`include/luna/bootstrap/` 是**正在被移除的历史前缀**，`include/luna/compiler/` 是契约化的新命名空间。
新模块不得再使用 `luna.bootstrap.*`。迁移进度见第 6 节。

---

## 4. 自举与门禁

### 4.1 三阶段固定点

`tools/selfhost.py` 驱动一个经典的三阶段自举：

```
anchor (受信任二进制)
  → out/stage-transition   用 anchor 构建
  → out/stage-next         用 transition 构建
  → out/stage-fixed        用 next 构建
  → 逐字节比对 assembly/ objects/ bin/
```

`verify --fresh` 要求 stage-next 与 stage-fixed 的**每一个产物字节相同**，打印
`FIXED POINT: all artifacts byte-identical`。这是任何 `library/`、`compiler/`、
`drivers/` 改动的**核心正确性门禁**。

### 4.2 命令

```sh
python3 tools/selfhost.py build            # 缓存化的 4-worker stage-next 构建
python3 tools/selfhost.py verify --fresh   # 冷固定点：transition → next → fixed，要求字节相同
python3 tools/selfhost.py test             # 用 out/stage-next/bin 编译并运行 450 个期望
python3 tools/selfhost.py audit            # 只读静态门禁
python3 tools/refmt.py --check             # 格式化门禁
```

`audit` 与 `refmt.py --check` 是**快速静态门禁**，应在冷 `verify --fresh` + `test` 之前跑。

### 4.3 迭代纪律

1. 新特性的第一版实现只能用当前工具链接受的语法（anchor 构建 stage-next）。
2. 先让改动在绿色 `verify --fresh` + `test` 下落地。
3. 之后编译器源码才可以使用该特性，再跑一次 `verify --fresh`。
4. 把验证过的工具链提升进 `anchor/`，刷新 `SHA256SUMS` 与 provenance。

### 4.4 锚点提升

只要用户要求提交并推送一个 `library/`、`compiler/` 或 `drivers/` 下的完成改动，
锚点提升就**属于同一次推送**，不必等单独提醒。

---

## 5. 编译流水线

```
源码 .la/.lh
   │
   ├─ frontend/lexer      token 流（122 种 TokenKind，67 个关键字）
   ├─ frontend/syntax     语法树（79 种 SyntaxKind，侵入式兄弟链表 + arena）
   ├─ frontend/parser     递归下降 + 优先级爬升，单个 class Parser
   │
   ├─ middleend/sema      29 步语义管道（sema.la 驱动）
   │    └─ 产出 ir::Module（move-only RAII 所有者）
   │
   ├─ backend/x86_64/codegen       ir::Module → 汇编文本（无寄存器分配）
   ├─ backend/x86_64/assembler     汇编文本 → 机器码 + 重定位
   │    ├─ object::serialize  → LUNAOBJ1（内部自有格式，默认）
   │    └─ elf::save         → ELF64 ET_REL（--emit elf，FFI 边界）
   │
   └─ backend/x86_64/linker        符号解析 + 节合并 + 重定位应用 → 静态 ELF64 可执行文件
```

驱动器把三个阶段包装成三个子命令：`luna compile`、`luna assemble`、`luna link`。

**两种对象格式并存的原因**：ELF 是给宿主工具链的 FFI 边界，LUNAOBJ1 是内部格式。
LUNAOBJ1 只有 4 个 section 概念、无 section header 表、无 strtab/shstrtab/program header，
解析器因此明显更小、更封闭，并且能被 `view_is_valid()` 一次性全量形式化校验。
详见 `codebase-backend.md`。

---

## 6. 模块注册表与依赖分层

`tools/selfhost.py` 的 `LIBRARIES` 是**唯一的模块注册表**。依赖图、链接顺序、
driver closure 全部从源码的 `import` 行推导出来（`library_order()` 是 Kahn 拓扑排序，
按 `LIBRARIES` 字典序做稳定 tie-break，成环则报错）。构建图是**推导出来的，不是手工维护的**。

当前规模（截至 39a2d87）：

| 类别 | 模块数 |
|---|---:|
| library | 22 |
| compiler | 43 |
| driver | 1 |
| **合计** | **66** |

其中 10 个模块是 `interface_only=True`（泛型体必须写在接口里才能跨模块单态化）：
`utility`、`span`、`vector`、`deque`、`list`、`tree`、`map`、`queue`、
`sem_consteval_model`、`sem_stmt_api`。

### 6.1 依赖分层（自底向上）

```
luna.linux.syscall          7 个 asm fn 裸桩，固定 ABI
  └─ luna.runtime           Error（errno 值）、File、结果结构
       └─ luna.std.memory   分配/扩容/释放
            └─ luna.std.buffer (byte_buffer)
                 ├─ luna.std.string
                 └─ luna.std.vector
            └─ luna.internal.pool → luna.internal.tree → luna.std.map
            └─ luna.std.deque → luna.std.queue
            └─ luna.std.list
```

前端与中端的两条向下链（无环）：

```
luna.compiler.lexer  → runtime, std.string_view, std.vector
luna.compiler.syntax → lexer, runtime, std.vector
luna.compiler.parser → lexer, syntax, runtime, std.ascii, string_view
luna.compiler.types  → runtime, std.vector              （完全独立）
luna.compiler.ir     → lexer, types, runtime, std.buffer, std.checked, std.vector
luna.compiler.sema.domain → types
```

已知的方向性瑕疵：`luna.compiler.ir` 为了 `Instruction.span`（`lexer::SourceSpan`）与
`Instruction.operation`（`lexer::TokenKind`）而 import 了 `luna.compiler.lexer`，
即 middleend 依赖 frontend 的 token 枚举。把这两个字段换成 IR 自有的枚举即可切断。

### 6.2 命名空间迁移进度

| 前缀 | 模块数（按注册表去重） |
|---|---:|
| `luna.bootstrap.*` | 32 |
| `luna.compiler.*` | 11 |
| 其他（22 个 library + `luna.tools`） | 23 |

按源文件计数则是 46 个 `.la` 声明 `luna.bootstrap.*`、63 个声明 `luna.compiler.*`——
按文件数已反超，按模块数仍偏 bootstrap。

已完成迁移的：`lexer`、`syntax`、`parser`、`types`、`ir`、`sema.domain`（即所有前端模块 +
类型表 + IR + 语义领域类型）。除 `sema.domain` 外，其余语义 pass 仍使用
`luna.bootstrap.middleend.semantic.*`——这是 `docs/roadmap.md` 未勾选项之一。

### 6.3 路径不必镜像模块名

`source_module()` 规定：**接口路径由模块名机械推导**（`a.b.c` → `include/a/b/c.lh`），
**实现路径则是自由填写的 stem**（`src/<stem>.la`）。所以
`luna.compiler.sema.domain` 的接口在 `compiler/include/luna/compiler/sema/domain.lh`，
实现却在 `compiler/src/middleend/semantic/domain/`——中间少两级是有意为之：
模块名走新的 `luna.compiler.sema.*` 命名空间，实现仍按物理分层摆放。

---

## 7. 代码风格实测

`AGENTS.md` 与 `docs/modernization.md` 规定了一套强制风格契约。全仓实测：

### 7.1 达标项

| 指标 | 实测 |
|---|---|
| 超过 120 列的行 | **0**（`compiler/`、`library/`、`drivers/` 全扫） |
| 超过 2000 行的源文件 | **0** |
| `drivers/` 中的 `while` | **0** |
| 长 if 链（kind 分派） | backend / library / frontend 基本为 0，一律 `switch` 或数据表 + 循环 |

### 7.2 未达标项

| 指标 | 实测 | 分布 |
|---|---:|---|
| `while` 关键字 | 157 | compiler 117 / tests 25 / library 15 / drivers 0 |
| 其中"手动索引循环"（应写成 `for`） | 生产 54、全仓 71 | `compiler/src/middleend` 51 |
| 超过两个逻辑子句的条件 | **19** | 全部在 `compiler/src/middleend`，其余子树为 0 |
| 实现单元超过 800 行 | 8 | 7 个在 `middleend/semantic` |

**`while` 的分布**：`compiler/src/middleend` 97 处、`frontend` 16 处、`backend` 4 处。
对比 `for (` 出现次数：compiler 500 / library 26 / drivers 19。即生产代码里 `while` 仅占
循环总量约 18%，但 middleend 是重灾区（97/117）。

需要区分两类：语法树是 `first_child`/`next_sibling` 侵入式链表，遍历必须沿链接推进，
可以用按 `child_count` 有界的 `for`，也可以用表达状态推进的 `while`；真正违规的是那
54 处仅用手工下标模拟有界遍历的循环。
Top  offenders：`context/lookup.la` 8、`context/builder.la` 7、`expr/numeric.la` 7、
`consteval.la` 6、`intrinsics.la` 4、`types/layout.la` 4、`consteval/engine.la` 4。

**超两子句条件**最严重的一处是 `semantic/consteval.la:121`，单条 `if` 有 5 个 `||`。
完整清单见 `codebase-middleend.md`。

**超过 800 行的实现单元**：

```
1570  compiler/src/middleend/semantic/classes.la
1536  compiler/src/middleend/semantic/expr/access.la
1342  compiler/src/middleend/semantic/stmt.la
1096  compiler/src/middleend/semantic/expr/probe/call.la
1001  compiler/src/middleend/semantic/expr/operators.la
 952  compiler/src/middleend/semantic/functions/bindings.la
 934  compiler/src/middleend/semantic/intrinsics.la
 855  compiler/src/middleend/semantic/types.la
```

700–800 区间另有 6 个。注意 `audit` 对 >2000 行**只 WARN 不报错**，800 行上限是软约束，
不进门禁。

### 7.3 门禁盲区

`audit` 的两项结构化检查覆盖面比看上去窄：

- 目录"文件与子目录不混放"**只检查 `compiler/src/backend/x86_64` 一棵子树**
- 接口 ≤250 行**只检查 `compiler/include/luna/compiler/**.lh`**

审计在生产源码树中确认了多处历史目录仍同层混放普通源文件与子目录；backend 是当前
`audit` 唯一覆盖的子树。这个数字依赖扫描是否纳入 `tests/` 和不含源码的目录，因此本文
不把一个口径不明的总数当作门禁结论。该规则约束新建或重写目录，遗留目录应在迁移对应
模块时收敛。

---

## 8. 技术债总表

本节把事实、待证明风险和明确非目标分开，避免把静态观察直接写成已经发生的缺陷。

### 8.1 已确认的结构问题

| # | 问题 | 位置 | 分册 |
|---|---|---|---|
| 1 | `base_type` 同时存在于 TypeTable 与 ClassTable，形成双来源不变量；当前未证明具体错误路径，但任何新增写者都可能令两份元数据分歧 | `classes.la:156-159` | middleend |
| 2 | `ClassTable` 暴露可写裸指针；当前 `classes.la` 中 `record_data()` 43 次、`method_data()` 15 次、`field_data()` 2 次、`friend_data()` 3 次，约 11 处直接成员写入 | `classes.la`、`types/layout.la` | middleend |
| 3 | probe（纯类型）与 access/operators（生成 IR）存在规则重复；应共享纯判定，不应合并两条具有不同副作用的流程 | `expr/probe/` | middleend |
| 4 | 5 个历史子模块 import 父 facade；修复必须合并真实边界、抽取更低层契约或下传入口，不能让同模块 `.la` 互相 import | `functions/ir.la:3` 等 | middleend |
| 5 | 语义管道忽略 29 个 `bool` 返回值，同时通过 `context->error` 传播错误；必须先明确 pass 契约并证明每个 `false` 都对应 sticky error，再决定短路或删除返回值 | `sema.la:133-161` | middleend |
| 6 | `luna.std.bytes` 与 `byte_buffer` 两套字节缓冲并存，别名检测逻辑也有重复 | `library/` | runtime-tooling |
| 7 | `luna.runtime` 尚未按 `modernization.md` 的边界完成分解 | `library/` | runtime-tooling |
| 8 | 诊断种类覆盖 90/122（73.8%），若干已支持语义没有对应负例 | `tests/` | tests |
| 9 | `expr/` 是待迁移的遗留混放目录；应在该模块重写时收敛，而不是为满足行数机械增层 | `semantic/expr/` | middleend |
| 10 | drivers 三个命令实现的缩进与其余实现不一致，当前 formatter 不会纠正行首结构缩进 | `drivers/` | runtime-tooling |

### 8.2 需要证据再排优先级的风险

- `FramePlan::find_storage`、`function_pointer_abi_index` 和若干线性查找具有二次复杂度上界；
  在基准证明它们进入真实热点前，不应抢占正确性与架构重构。
- `deque`、`list`、`queue` 当前没有生产消费者，但它们是 `interface_only` 模块，
  没有消费者时不会各自产生 stage 编译对象；实际成本是注册表、文档与测试表面积。
- `base_type` 双写的结构风险已确认，具体的 `vptr_offset` 错误结果仍需最小复现证明。

### 8.3 明确延期，不作为当前缺陷

- 当前 codegen 不做寄存器分配是 correctness-first 后端的明确策略。
- `expected<V,E>`、`optional`、`variant`、完整迭代器和更大的容器 API 属于后续标准库表面，
  不能仅因 C++ 已有就列为当前回归。

### 8.4 建议顺序

1. 先保持文档权威边界清楚，避免历史设计反向约束当前实现；本轮已完成第一遍清理。
2. 证明并消除 `base_type` 双来源，同时用 focused mutator 收紧 ClassTable 的其他可变状态。
3. 设计 `sema.session` 的最小状态/错误协议，再迁移 Context、lookup、builder；不要先造巨型 facade。
4. 用同样的 import-graph 证据收缩其余父子模块边界。
5. 在触及对应 pass 时清理长 kind 分派、复杂条件和遗留目录；性能优化必须由基准触发。

---

## 9. 现有 `docs/` 导航

基线提交已有 51 份文档、11,937 行；加上本次 6 份代码审计分册后是 57 份。
下面 9.1–9.8 分类的是原有 51 份设计记录。

### 9.1 语言规范（3）

| 文件 | 行数 | 内容 |
|---|---:|---|
| `language.md` | 1,376 | **权威表面语法规范**：模块、类型、类、泛型、语句、表达式、属性与 intrinsic、编译期面 |
| `syntax-plan.md` | 553 | 语法补全计划 + 8 条设计决策 + 逐特性的 C23 处置表 |
| `execution-semantics.md` | 373 | 求值顺序、整数环绕与陷阱、IEEE-754、System V 调用 ABI |

### 9.2 里程碑设计文档（4）

`m2-callable-infrastructure.md`（811，签名/mangling/重载/默认值/值类别）、
`m3-oop-design.md`（871，类/继承/虚分派/RTTI/friend/opaque）、
`m4-generics-design.md`（677，原生泛型与单态化）、
`m5-lifetime-design.md`（168，引用/析构/copy-move/临时对象）。

### 9.3 子系统现代化契约（8）

`sema.md`、`ir.md`、`types.md`、`lexer.md`、`syntax.md`、`parser.md`、`codegen.md`、`tools.md`。
均为"冻结契约 + 领域对象 + 语言特性审查 + caw 验证结果"四段式。

### 9.4 后端与对象格式（7）

`backend-modules.md`、`assembler-design.md`、`assembler-symbol-index.md`、`object.md`、
`elf.md`、`linker.md`、`ffi.md`。

### 9.5 标准库（4）

`container-foundation.md`、`standard-containers.md`、`string-charconv.md`、
`minimum-standard-library.md`。

### 9.6 构建、测试与计划（4）

`build-system.md`、`test-architecture.md`、`roadmap.md`、`next-steps.md`。

### 9.7 架构治理（2）

- `architecture.md`（666）：五条不可协商边界、流水线、双对象格式
- `modernization.md`（353）：**AGENTS.md 指定的强制架构契约**，所有新代码与重写代码必须先读它

### 9.8 历史 m0 种子契约（19）

`bootstrap-{frontend,middleend,x86-64-backend,reproducibility,seed,toolchain,language-versions}.md`、
`module-metadata.md`、`freestanding-runtime.md`、`linux-syscall-abi.md`、`machine-ir.md`、
`liveness.md`、`register-allocation.md`、`instruction-rewrite.md`、`abi.md`、`elf-object.md`、
`elf-linker.md`、`debug-information.md`、`instruction-differential-testing.md`。

### 9.9 历史性与时效性清理

**带显式历史横幅（9 份，可安全忽略细节）**：`bootstrap-frontend.md`、
`bootstrap-middleend.md`、`bootstrap-x86-64-backend.md`、`machine-ir.md`、`elf-object.md`、
`elf-linker.md`、`module-metadata.md`、`freestanding-runtime.md`、`linux-syscall-abi.md`。

**本轮补上历史横幅的（7 份）**：`abi.md`、`liveness.md`、`register-allocation.md`、
`instruction-rewrite.md`、`debug-information.md`、`instruction-differential-testing.md`、
`bootstrap-language-versions.md`。它们完整记录已归档的
`lunac --emit mir/liveness/allocation/rewrite/abi` 管线，不再作为当前命令或实现说明。

**其他具体失效点**：

- `next-steps.md` 是停在 443 项测试和三工具打包阶段的计划日志，本轮明确标为历史快照
- `minimum-standard-library.md` 已有 m0 历史横幅；其中手工所有权规则不是当前容器契约
- `execution-semantics.md` 的运行时边界已改为当前 move-only RAII 所有权语义
- `architecture.md` 开篇已改为纯 Luna 分支与单一 `anchor/luna` 信任根
- 各文档的 audit 模块数快照互相矛盾且全部过期（80 / 86 / 74 / 75 / 72 / 68），
  **当前实际是 66 个模块**
- `roadmap.md` 的 backend 收缩计数已从 six 更正为 five
- `backend-modules.md` 是后端收缩批次快照；后续 codegen 文件族以 `codegen.md` 和源码为准

### 9.10 路线图状态

`docs/roadmap.md` 的"Current whole-project modernization"共 15 项，**3 项未勾选**：

```
L315  [ ] standard-library RAII migration and luna.runtime decomposition
L326  [ ] semantic-pipeline state classes and module contraction
L328  [ ] remaining luna.bootstrap.* namespace/module contraction
```

已完成的相邻项：ClassTable / GenericTable / domain 元数据抽取（对应 `39a2d87`）。
`SESSION.md` 记录的**下一批**是把 `Context` / `lookup` / `builder` 收缩进计划中的
`luna.compiler.sema.session` 边界——即 roadmap L326 的未勾选项。

---

## 10. 一次改动的典型路径

以修改语义层某个 pass 为例：

1. 读 `AGENTS.md`，再读 `docs/modernization.md`（强制架构契约）
2. 若 `SESSION.md` 有未完成批次，先读它并与 `git status` / `git diff` 对账
3. 改代码，遵守：条件最多两个逻辑子句、有界遍历用 `for`、kind 分派用 `switch`、
   枚举映射用数据表 + 循环、列宽 120、新模块必须进 `LIBRARIES`
4. `python3 tools/selfhost.py audit`（快）
5. `python3 tools/refmt.py --check`（快）
6. `python3 tools/selfhost.py verify --fresh`（慢，核心正确性门禁）
7. `python3 tools/selfhost.py test`（450 个期望）
8. 若用户要求提交推送，同一次推送里提升 `anchor/` 并刷新 `SHA256SUMS` 与 provenance
