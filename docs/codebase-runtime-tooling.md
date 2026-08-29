# 运行时、驱动器与工具链

> **状态与权威性：** 本文是绑定提交 `39a2d87` 的非规范代码审计快照，用于导航、
> 现状分析和技术债发现。与 `AGENTS.md`、语言规范、架构契约或当前源码冲突时，
> 以后者为准；文中的行号和计数不会随重构自动更新。

覆盖三个目录：`library/`（36 文件 3,812 行）、`drivers/`（8 文件 1,682 行）、
`tools/`（4 个 Python 文件 4,460 行）。仓库另有 1 个 245 行的
`tests/ffi/generate_fixtures.py`，所以全仓 Python 合计是 5 个文件、4,705 行。

行号截至提交 `39a2d87`。

---

## 1. 标准库与运行时 `library/`

36 个文件 = 22 个 `.lh` + 14 个 `.la`，对应 `selfhost.py` 中 **22 个 `LIBRARIES` 键**。

### 1.1 依赖分层（自底向上）

```
luna.linux.syscall     7 个 asm fn 裸桩，固定 ABI
  └─ luna.runtime      Error（errno 值）、File、结果结构
       └─ luna.std.memory    分配 / 扩容 / 释放
            ├─ luna.std.buffer (byte_buffer) ─┬─ luna.std.string
            │                                 └─ luna.std.vector
            ├─ luna.internal.pool → luna.internal.tree → luna.std.map
            ├─ luna.std.deque → luna.std.queue
            └─ luna.std.list
```

**`luna.linux.syscall`**（`library/src/linux/syscall.la`）是 7 个 `asm fn` 裸桩
`call0..call6`，用 `@export_name("luna_linux_syscallN")` 固定 ABI。文件头注释说明了两个
关键细节：SysV 第 4 参数在 `%rcx` 而内核要 `%r10`，故 `call4+` 有 `movq %r8, %r10`；
`call6` 因为无 prologue，第 6 参从 `8(%rsp)` 读。

**`luna.runtime`** 定义 `Error`（即 Linux errno 值）、`File` / `Memory` /
`IoResult` / `FileResult` / `MemoryResult` / `ProcessIdResult`，并用私有
`enum LinuxSystemCall`（`open_at=257` / `rename_at=264` / `exit_group=231`…）、
`LinuxOpenFlags`（`524288 = O_RDONLY|O_CLOEXEC`）、`error_from_raw()`
（`-4095..-1 → Error`）封装。

> 注意：运行时里**没有 `_start`**。`_start` 由 codegen 生成（backend `module.la:110-126`）。

**`luna.std.memory`** 的 `resize` 是**分配 + 拷贝 + 释放**（未用 `mremap`），
所以每次扩容 = 2 次 syscall + 全量拷贝。

### 1.2 与 C++ 标准库命名模型的吻合度

`AGENTS.md` 要求标准库公共 API 遵循 C++ 标准库命名模型。

**吻合的部分**：模块名 `luna.std.{vector,span,list,deque,map,queue,string,string_view,utility}`；
类型 `vector<Value>`、`span<Value>`、`map<Key,Value>`；方法
`empty` / `size` / `capacity` / `data` / `reserve` / `clear` / `push_back` /
`front` / `back` / `find` / `contains` / `insert` / `insert_or_assign` / `erase`。

**偏差清单**：

- **无 `expected<Value, Error>`**——`AGENTS.md` 与 `modernization.md` 都把它列入目标，
  但代码中不存在。取而代之的是约 **16 个 passive 结果结构体**：
  `runtime::{FileResult,MemoryResult,IoResult,ProcessIdResult}`、`memory::AllocationResult`、
  `bytes::BufferResult`、`string_view_result`、`path::PathResult`、`charconv::to_chars_result`、
  `pool::{storage_allocation,pool_allocation<Value>}`、
  `tree::{tree_insert_result,tree_erase_result}`、
  `map::{map_insert_result,map_erase_result}`、`FrontendViewResult`
- **`get(index)` 代替 `operator[]` / `at()`**，且**无边界检查**——`vector::get` 直接 `data[index]`
- **完全没有迭代器 / `begin` / `end`**——所有遍历都是
  `for (var index: usize = 0; index < size(); index += 1)`
- **无拷贝语义**——所有容器只有 `init()` / `init(&&)` / `operator=(&&)` / `deinit`，move-only。
  `init` 里 `assert(@is_trivially_relocatable(Value))`，
  负例测试见 `tests/cases/fail_{vector,list,deque,map}_nontrivial.la`
- **`move` 重名**——`luna.std.utility::move<Value>`（移动转换）与
  `luna.std.memory::move`（memmove）同名不同模块。前者**只被 tests 使用**，
  library/compiler 自身从不调用（`utility.lh` 仅 5 行）
- **偏离 C++ 标准库模块边界的名字**——`bytes`、`buffer`、`binary`、`io`、`checked`、
  `internal.pool`、`internal.tree`。`memory`、`charconv` 都有直接的 C++ 标准头/概念对应，
  `path` 对应 `std::filesystem::path`；它们的 Luna 模块边界不同，但不能归为非 C++ 命名
- **`string` 偏 C 风格**——`append` / `push_back` / `view()` / `detach()`，无 `substr` /
  `compare` / `operator+` / `c_str()`；`size()` 不含结尾 NUL，
  `data()` 空串时退回 `&this->empty_sentinel`

**其他缺件**：`optional`、`variant`、`array`、`tuple`、`unordered_map`、allocator、拷贝语义——全部不存在。

### 1.3 passive struct 与 RAII class 的划分

**passive struct**：`runtime::{File,Memory,IoResult,…}`、`memory::{Allocation,AllocationResult}`、
`bytes::{Buffer,BufferResult}`、`string_view` / `string_view_result`、`path::{Path,PathResult}`、
`charconv::to_chars_result`、`vector_storage<Value>`、`pool::{storage_allocation,pool_allocation<Value>}`、
`map::{map_insert_result,map_erase_result}`、`tree::{tree_insert_result,tree_erase_result}`。

**RAII class**：`byte_buffer`（`deinit` → `release()`）、`string`、`vector<Value>`、
`deque<Value>`、`list<Value>`（`deinit` → `clear()`）、`queue<Value>`、`map<Key,Value>`、
`slot_storage`（`deinit` → `release_blocks()`）、`slot_pool<Value>`、`ordered_tree`。

> **阅读陷阱**：`vector` / `deque` / `queue` / `map` / `slot_pool` 的 `deinit()`
> **函数体是空的**，靠成员（都是 `byte_buffer` 或 `slot_storage`）的 `deinit` 级联释放。
> 组合是正确的，但读代码时容易误判为泄漏。这个模式在全仓一致（lexer、parser、
> TypeTable、ClassTable 都是如此）。

### 1.4 泛型与单态化成本

泛型体**写在 `.lh` 接口里**（`vector.lh` 127 行、`deque.lh` 239 行、`list.lh` 228 行、
`map.lh` 96 行、`span.lh` 111 行、`tree.lh` 518 行、`pool.lh`），因为跨模块单态化需要
导出泛型体；这些模块在 `LIBRARIES` 中都是 `interface_only=True`，**没有 `.la`**。

代价是当前每个消费模块都会生成所需泛型方法体。文本扫描中 `vector<usize>` 出现 16 次、
`vector<Parameter>` 9 次、`vector<Diagnostic>` 6 次；这些是源码引用次数，不等于 16/9/6
种类型，也不能直接等同于最终强符号数量。`map` 有 2 个具体类型组合
（`map<GlobalName,Global>`、`map<SymbolName,SymbolBinding>`）。

这是"泛型代码生成去重"被列为独立未来编译器任务的原因（见 `SESSION.md` 的 IR 批次记录）。

### 1.5 三个容器零生产使用

| 容器 | import 数 | 限定使用数 | 生产消费者 |
|---|---:|---:|---|
| `map` | 2 | **6** | linker 与 assembler 的符号表，见下 |
| `deque` | 0 | 0 | 无 |
| `list` | 0 | 0 | 无 |
| `queue` | 0 | 0 | 无 |

`map` 的 6 处生产使用分布在：`compiler/include/luna/compiler/x86/linker.lh`、
`compiler/include/luna/compiler/x86/assembler.lh`、
`backend/x86_64/linker/facade.la`、`backend/x86_64/linker/symbols.la`、
`backend/x86_64/assembler/symbols.la`。

**`map` 在链接器与汇编器的符号表里有真实生产使用**；`deque` / `list` / `queue` 才是
零生产使用——唯一消费者是 `tests/cases/std_containers.la` 加 4 个负例测试。
它们是 `interface_only` 模块；没有消费者时不会各自产生 stage object，也不会作为独立
library target 编译。当前成本主要是注册表、文档与测试表面积。

另外 `span<Value>`（**可变**）实际未被工具链使用：`compiler` / `drivers` 中只有
`span::const_span<u8>` 出现 12 次；`vector::as_span()` 返回的 `span<Value>` 无人消费。

---

## 2. 驱动器 `drivers/`（8 文件 1,682 行）

### 2.1 结构

**单一模块 `luna.tools`** = 1 个接口 `drivers/include/luna/tools.lh` + **6 个同模块实现单元**，
由一个注册表项描述：

```python
driver_module("luna.tools", "cli", "frontend", "compile", "assemble", "link", "facade")
```

程序入口是独立的 `drivers/src/entry.la`（`module luna.entry;`，7 行，只调 `tools::run`），
登记在 `DRIVERS = {"luna": "drivers/src/entry.la"}`。

`ToolDriver`（facade.la）持有 `CommandLine`，`execute()` 用 `switch (selected)` 分发到
`run_compile` / `run_assemble` / `run_link(argc - 1, argv + 1)`。

`CommandLine`（cli.la）是**不拥有 argv** 的光标类，提供 `advance` / `take_next` /
`matches` / `consume_end_of_options` / `current_starts_with_dash`；
命令识别是 `entries[5]` **数据表 + 循环**。

共享工具函数也在 `cli.la`：`argument_equals`、`first_error`（错误折叠）、`print`、
`append_literal`、`append_usize`。

### 2.2 三大命令的职责边界

| 命令 | 文件 | 行数 | 流程 |
|---|---|---:|---|
| `compile` | `compile.la` | 632 | `FrontendStorage` arena → `context::Input` → `sema::check` → `codegen::emit_assembly` → 写文件 |
| `assemble` | `assemble.la` | — | `assembler::assemble` → `elf::save`（`--emit elf`）或 `object::serialize`（默认 LUNAOBJ1） |
| `link` | `link.la` | — | `parse_object` 嗅探魔数分流 → `object::ObjectSet` → `linker::link` → `io::write_executable_file_atomic` |

`FrontendStorage`（frontend.la）是**扁平 arena**：4 个
`vector<StoredToken/StoredDiagnostic/StoredNode/FrontendRecord>`，`add()` 先
`reserve_for()` 再追加，失败走 `rollback(record)` 逐段 `truncate`；
`get(index)` 用 `lexer::TokenView` / `syntax::SyntaxView` 重建指向池内的视图。
这样前端产出的 token 与语法树节点拥有稳定的索引。视图只在对应 arena 不再追加、扩容或
析构的借用期内有效；不能把它描述成可跨任意后续 mutation 安全持有。

### 2.3 错误传播与诊断格式

一切返回 `runtime::Error`，用 `first_error(current, next)` 折叠；对外映射为
`CompileStatus` / `AssembleStatus` / `LinkStatus` 的 `i32`。

- **成功码内部是 `success = 42`**，在 CLI 边界翻成 0
- 语义诊断是 `semantic_diagnostic_base(64) + kind`

诊断是**机器可解析的单行**，全部写 stderr：

```
frontend:lex:<kind>:<offset>:<detail>\n
semantic:<kind>:<unit>:<offset>:<detail>\n
resource:<label>:<unit>:<limit>\n
```

### 2.4 fixed protocol——一个隐藏契约

当子命令只带 1 个参数（`argc == 1`）时，**三个命令全部切到"bootstrap 约定"模式**：

- `compile` 读 `bootstrap-stage-version`（必须等于 `LUNA-STAGE/1 LUNA/1\n`）与
  `bootstrap-stage-mode`（`E` / `L`），枚举 `bootstrap-stage-unit-N.luna` 直到
  `not_found`，写 `bootstrap-stage-output.s`
- `assemble` 读 `bootstrap-assembly-input.s`，写 `bootstrap-object-output.lo`
- `link` 读 `bootstrap-link-input-N.lo`，写 `bootstrap-link-output`

固定协议不是为了绕过 CLI 的任意长度限制：compile 与 link 的公开上限本来就是 64 / 128，
两条路径一致。它是 selfhost 保留的稳定、无参数、文件名约定式阶段协议，使构建器无需重新
编码完整命令行并能对固定输入/输出字节做独立回归。

这套文件名在 `compile.la` / `assemble.la` / `link.la` 与
`selfhost.py::execute_tool_cli_tests` 中**各写一遍**，后者还钉死了三个产物的
长度与 SHA-256（951 / 597 / 4190 字节）。**任何 codegen 改动都会按设计打断该测试。**

---

## 3. 构建系统 `tools/selfhost.py`

### 3.1 注册表

`LIBRARIES: dict[str, RegisteredModule]`，由 `library_module` / `compiler_module` /
`driver_module` / `interface_module` 四个助手生成，底层 `source_module()` 机械映射：

```
a.b.c → {tree}/include/a/b/c.lh        （接口路径，由模块名推导）
a.b.c → {tree}/src/<stem>.la           （实现路径，stem 自由填写）
```

共 **22 个 library 键 + 43 个 compiler 键 + 1 个 driver 键 = 66 个模块**，
注册源文件 196 个、48,520 行。其中 `interface_only=True` 的模块 10 个。

### 3.2 从 import 推导构建图

`IMPORT_PATTERN` 正则扫 `import x.y.z [as a] [::{...}];`；
`source_dependency_keys()` 对**未注册 import 直接 fail**。

`library_order()`（selfhost.py:432-455）是 Kahn 拓扑排序，按 `LIBRARIES` 插入序做
稳定 tie-break，成环则报错。

- `dependency_closure(..., implementation=False)` = 接口闭包（只给 `.lh`）
- `implementation=True` = 链接闭包
- `library_units(key)` = `[实现们…, 自身接口, 接口闭包]`
- `driver_units(entry.la)` = `[entry.la, 接口闭包]`

**构建图是推导出来的，不是手工维护的。** `audit` 会在注册表、源码与 anchor 三者不一致时失败。

### 3.3 缓存（`tools/build.py`）

`ArtifactCache` 内容寻址，路径 `<cache>/<scope>/<action>/<target>/{artifact,manifest.json}`，
版本号 `luna-build-cache-v1`。

编译指纹 = 版本 + `"compile"` + **工具链自身的 sha256** + runner 摘要 + 模式 +
每个 unit 的（逻辑路径, sha256）。assemble 用汇编文本 sha256；link 用各对象名 + sha256。
**runner（qemu）二进制本身也参与哈希。**

`restore()` 校验 `version` / `input` / `size` / `sha256` / `mode` 后才 `atomic_copy`；
`store()` 用 tmp + `os.replace` 原子写清单。`--fresh` 强制全部 miss 但**仍写缓存**。

### 3.4 三阶段自举（`verify`）

```
verify_anchor()   sha256sum --check --strict anchor/SHA256SUMS
  → out/stage-transition   tools = anchor
  → out/stage-next         tools = transition/bin
  → out/stage-fixed        tools = next/bin
  → compare_stages()       对 assembly / objects / bin 做 filecmp
```

全等则打印 `FIXED POINT: all artifacts byte-identical`。
三个阶段用**独立的 cache scope**（transition / next / fixed）。

子命令：`build`（anchor → stage-next，走缓存）、`verify`（冷固定点）、
`test`（不构建，要求已有 `out/stage-next/bin`）、`audit`（只读）。

### 3.5 `audit` 的完整检查清单

1. 模块名唯一
2. `interface_only` 与实现**双向**校验：interface-only 不得有实现；
   **非 interface-only 不得没有实现**
3. 每文件恰好一条 module 声明，且 `export module` / `module` 的导出性与角色必须匹配
4. 单文件内重复 import
5. **同模块跨文件重复 import 同一模块**
6. import 未注册模块
7. 孤儿文件：`discovered_paths - registered_paths`，**扫描范围只有
   `compiler/` / `library/` / `drivers/`，不含 `tests/`**
8. `audit_migrated_layout()`：目录混放检查**只覆盖 `compiler/src/backend/x86_64` 一棵子树**；
   接口 ≤250 行**只覆盖 `compiler/include/luna/compiler/**.lh`**
9. 逐文件：结尾换行、无 tab、无行尾空格、≤120 列（**>2000 行只是 `WARN` 不是 ERROR**）
10. driver 专项：文件存在、恰好一条非 export 的 module 声明、无重复 import、无未注册 import
11. 先跑 `sha256sum --check --strict anchor/SHA256SUMS`，结尾再跑一次 `library_order()` 环检测

### 3.6 并发

`ThreadPoolExecutor(max_workers=min(jobs, len(targets)))` 并行编译所有 library，
**driver 在所有 library 完成后串行链接**；输出靠全局 `PRINT_LOCK`。
测试同样用线程池跑 suite + 孤立期望，默认 `--jobs 4`。

---

## 4. 格式化器 `tools/refmt.py`

`BUDGET = 120`。两阶段：`format_text()` 合并物理行 → `split_long_lines()` 折行，
外层 `while lines != previous` 跑到不动点。

**合并规则**：行尾是 `(` 时，用 `paren_group_end()` 尝试把整个括号组压成一行（≤120 才压）；
否则贪心合并，条件是 `tail_continues(current) or head_continues(nxt)` 且合并后 ≤120。

- `tail_continues` = `bracket_depth() > 0`（**只算 `()` 与 `[]`，忽略 `{}`**）
  或行尾落在 `TAIL_OPS`（`&& || -> == != <= >= << >>` 复合赋值、`? : = + - * / % & | ^ < > , (`）；
  行尾是 `;` 或 `{` 一律不合并
- `head_continues` = 下一行以 `) ] . ? : && ||` 开头
- `merge()` 默认粘一个空格，除非右端首字符是 `) ] , . ; :` 或左端以 `(` 结尾

**折行**：`split_candidates()` 在引号/转义感知下收集 preferred 断点（`,` 之后、
`&&` / `||` 之后）与 fallback（空白），取 `max(position)`，约束
`indent+8 <= position <= 120`，续行缩进 = 原缩进 + 4。

**token 流不变式**：`mask()` 把字符串字面量替换为等长占位 `"\x00<len>\x00"`；
`TOKEN_RE` 先匹配多字符运算符，再匹配标识符/数字，再匹配单字符标点。
`main()` 比较原始与格式化后的 `token_stream`（**排除 `import ` 行**），
不一致就打印 `SKIP token-mismatch` 且**不写盘**。

**覆盖范围**：`compiler/**` 与 `library/**` 的 `.la` / `.lh`；
`drivers/**` **只含 `.la`**——即 `drivers/include/luna/tools.lh` 不受格式化管辖，
`tests/` 完全不管。当前状态 `0 need reformatting, 0 token-mismatch`。

---

## 5. 技术债

### 5.1 两套并行的字节缓冲

遗留 `luna.std.bytes`（passive `Buffer` + 自由函数，被 **22 个文件**使用，含
`io` / `path` / `binary` 与全部 driver 命令）与 RAII `luna.std.buffer::byte_buffer`
（**19 个文件**：除 `string` / `vector` / `deque` / `pool` 外，IR、semantic Input、
object 与 linker 也已采用）。

更糟的是**别名检测逻辑被复制了两份**：`byte_buffer::resolve_source` 与
`bytes::resolve_append_source`，结构完全同构。

`modernization.md:196-199` 与 `container-foundation.md:197` 都称 `luna.std.bytes` 是过渡模块、
最后一个调用者迁移后删除——但 22 个文件仍在用。

### 5.2 `luna.runtime` 未按计划分解

`modernization.md:211-214` 与 `218-230` 的目标目录树要求拆成 `luna.linux.*` 后删除
`luna.runtime`；实际只拆出了 `luna.linux.syscall`，`luna.runtime` 仍在。
这是 `docs/roadmap.md:315` 的未勾选项之一。

### 5.3 drivers 缩进异常

`drivers/src/compile.la`（331-491 行）、`link.la`（72-151）、`assemble.la`（59-97）中
`impl` 里的方法体与签名**同为 4 空格缩进**，与全仓其余文件（8 空格）不一致。

`refmt.py` 保留行首缩进，因此格式化门禁通过但风格是坏的。

### 5.4 `Path::is_valid()` 重复扫描候选

每次 `as_c_string` / `view` / `release` 都要重扫 UTF-8 + 查内嵌 NUL；
`io::read_file_limited` 等热路径会被反复调用。
这是可测量的重复工作，但尚无 profile 证明其主导工具耗时；在引入缓存状态及其失效不变量前，
先用基准决定是否值得优化。

### 5.5 历史维护表面

- `anchor/PROVENANCE.md` 顶部已改为区分最初 m0 三工具集合与当前单一 `luna`
- `tools/release/bootstrap_seed.py`（1,422 行，占 tools Python 代码近 1/3）
  面向已归档的 `lunac` / `luna-as` / `luna-link` 三元组。它应明确标为历史 seed 发布工具；
  在没有正式废弃发布流程前，不能仅凭当前构建不调用就认定为死代码

### 5.6 门禁盲区

`audit` 的目录混放检查只覆盖 backend 子树、接口行数检查只覆盖
`include/luna/compiler/`；孤儿文件扫描不含 `tests/`。
`refmt.py` 不覆盖 `drivers/include/**.lh` 与整个 `tests/`。

### 5.7 其他

- `library/src/std/checked.la:15` 的 `let mask = alignment - 1;` 使用类型推断，是当前 Luna 的
  现代特性，不是风格债
- checked 算术用得很到位：`add` / `align_up` / `range_is_valid` / `is_power_of_two`
  几乎只被 `backend/x86_64/object/{reader,writer}.la` 与 `linker/{layout,symbols}.la`
  使用——正好是解析不可信 ELF/LUNAOBJ 输入的地方
