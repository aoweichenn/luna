# 前端：词法、语法树、解析器

> **状态与权威性：** 本文是绑定提交 `39a2d87` 的非规范代码审计快照，用于导航、
> 现状分析和技术债发现。与 `AGENTS.md`、语言规范、架构契约或当前源码冲突时，
> 以后者为准；文中的行号和计数不会随重构自动更新。

`compiler/src/frontend/` 共 17 个文件、4,283 行，分三个模块：

| 模块 | 文件 | 行数 |
|---|---|---:|
| `luna.compiler.lexer` | `lexer/` 5 个 | 822 |
| `luna.compiler.syntax` | `syntax/` 3 个 | 399 |
| `luna.compiler.parser` | `parser/` 9 个 | 3,062 |

按构造顺序是 `lexer → syntax → parser`；按依赖方向则是 `parser → syntax → lexer`。
具体导入关系：

```
luna.compiler.lexer  → runtime, std.string_view, std.vector
luna.compiler.syntax → lexer, runtime, std.vector
luna.compiler.parser → lexer, syntax, runtime, std.ascii, string_view
```

前端是全仓**风格达标率最高的区域**：零个手动索引 `while`、零个超两子句条件、零个长 if 链。
它是 `docs/modernization.md` 契约执行得最彻底的部分，可作为其余子系统重构的参照。

行号截至提交 `39a2d87`。

---

## 1. Lexer（`compiler/src/frontend/lexer/`，5 文件 822 行）

### 1.1 文件划分

| 文件 | 职责 |
|---|---|
| `facade.la` | `LexResult` / `DiagnosticBuffer` 的构造与视图；`deinit`（108、156） |
| `session.la` | `Lexer` 的生命周期与驱动：`run()`（159-174）、`fail()`（72-77）、trivia 跳过（122-157） |
| `token.la` | `scan_token()`（4-163），主分派 `switch (byte as Ascii)` |
| `keywords.la` | `KeywordTable` 的 67 项字面量填充（66-133）与 `lookup()`（136-150） |
| `literals.la` | 数字/字符串/宽字符串扫描（`scan_number` 93、`scan_quoted` 107、`scan_wide_string` 151） |

### 1.2 类型

`enum TokenKind: usize`（`compiler/include/luna/compiler/lexer.lh:7-33`）有 122 个变体，分五组：
终结符、字面量、关键字、标点、运算符/复合运算符。末尾的 `keyword_opaque` 是上界哨兵，
被 `parser/session.la:92` 用于 token 种类合法性检查。

`enum DiagnosticKind` 14 种（lexer.lh:35-39）。`SourceSpan`、`Token`、`Diagnostic`
都是 passive struct——符合"被动值用 struct"的契约。

### 1.3 关键字映射：数据表 + 循环，不是 if 链

`class KeywordTable`（lexer.lh:153-158）持有定长数组 `entries: [LexerLimit.keyword_count]KeywordEntry`，
`keyword_count = 67`（lexer.lh:150）。`init()` 用**一条字面量聚合式**填满 67 项（keywords.la:66-133），
`lookup()` 是线性扫描 + `source_text_equals()` 逐字节比较（keywords.la:136-150）。

没有哈希、没有排序、没有 if 链。每次标识符识别 O(67)——对 67 个关键字而言是可接受的取舍，
且完全满足"枚举映射用数据表 + 循环"的强制规则。

### 1.4 扫描方式

单遍、无回溯的字节游标扫描器，不是显式状态机表。`class Lexer`（lexer.lh:160-172）私有状态：
`source / offset / line / column / keywords / result`，**全部 `priv`**。

核心循环 `Lexer::run()`（session.la:159-174）：`scan_token()` → 取末 token → 遇 `TokenKind.end` 停止。

`scan_token()`（token.la:4-163）对首字节做 `switch (byte as Ascii)`（`Ascii` 枚举在 lexer.lh:138-146）。
双字符运算符用 `match()` 前瞻（token.la:65-141），`>>` / `<<` 的三字符变体
（`shift_left_equal` 等）也在同一个 switch 里完成——是表状分派而非 if 链。

数字的进制前缀由 `number_base()` 的 switch 决定（literals.la:33-67）。

### 1.5 所有权与错误模型

- `LexResult` / `DiagnosticBuffer` 是 **move-only RAII 包装**（lexer.lh:93-130），
  移动构造逐个成员 `as T&&` 转移
- `deinit()` 是**空实现**（facade.la:108、156）——实际释放由 `vector<T>` 的析构承担。
  这是全仓一致的惯用法（组合而非手工释放），读代码时容易误判为泄漏
- **sticky 错误**：`Lexer::fail()`（session.la:72-77）只在 `error_code == none` 时写入，
  `ready()` 检查它。一旦失败，后续所有操作短路返回首个错误
- `add_token` 超额时发 `resource_limit` 诊断并截断（session.la:94-107），
  而不是无限增长

---

## 2. Syntax（`compiler/src/frontend/syntax/`，3 文件 399 行）

| 文件 | 职责 |
|---|---|
| `tree.la` | `SyntaxTree` 的构造、移动、节点访问；`no_index()`（16-18） |
| `builder.la` | `SyntaxBuilder`：`add_child`（116-145）、`mark`/`restore`（162-178） |
| `verify.la` | 结构校验：`children_are_valid`（27-44）、`SyntaxView::is_valid`（78-103） |

### 2.1 节点是 struct 值 + 索引化 arena

`struct SyntaxNode`（syntax.lh:35-45）9 个字段：
`kind, span, token_index, parent, first_child, last_child, next_sibling, child_count, flags`。

`enum SyntaxKind` 有 79 个变体（syntax.lh:7-22）；`SyntaxFlag` 是位域枚举，最高到
`destructor = 2^34`（syntax.lh:24-33）。

**为什么是 struct 而不是 class**：节点是被 arena 批量拥有的被动值，没有行为、没有不变量
需要保护、没有资源需要释放。恰好落在 `modernization.md` 的"passive value 用 struct"一侧。

### 2.2 布局：侵入式单链表，不要求连续下标

`class SyntaxTree`（syntax.lh:73-91）私有 `nodes: vector<SyntaxNode>` +
`source_token_count` + `root_index`，move-only（tree.la:67-72，移动后把源的 `root_index`
复位为 `no_index()`）。`no_index() = usize::MAX`（tree.la:16-18）。

孩子用**侵入式单链表**（`first_child` / `last_child` / `next_sibling`）连接。
`add_child`（builder.la:116-145）要求子节点 `parent == none && next_sibling == none`
（不可二次挂接），并校验 `child_count != usize::MAX`；它没有要求兄弟节点的 arena 下标连续。
孩子之间可以夹着其后代或其他节点，消费者不得把 `first_child + offset` 当成第 offset 个孩子。

`verify.la:27-44` 的 `children_are_valid` 会走一遍链表，确认恰好 `child_count` 个节点
且尾节点是 `last_child`。

> 遍历必须沿 `next_sibling` 推进。已知 `child_count` 时可用有界 `for`，开放式状态遍历也可用
> `while`；关键是不假设兄弟节点下标连续，也不把所有链式遍历机械判成风格违规。

### 2.3 Builder / Tree / View 三分

这是全仓反复出现的**所有权三分模式**（IR、对象格式、诊断都是同一套路）：

- `SyntaxBuilder`（syntax.lh:93-114）持有 `storage: SyntaxTree` + sticky `error_code`，
  是 `SyntaxTree` 的 `friend`（syntax.lh:78）以便直接改 `root_index`
- `mark()` / `restore()`（builder.la:162-178）靠 `vector::truncate` 实现，供 parser 回溯
- `SyntaxView`（syntax.lh:57-71）是**非拥有的指针 + 计数视图**

`SyntaxView::is_valid()`（verify.la:78-103）逐节点跑 `node_is_valid`（46），
外加 `reaches_root` 全量可达性检查（67）——O(n·depth)，仅在测试与断言路径使用。

---

## 3. Parser（`compiler/src/frontend/parser/`，9 文件 3,062 行）

### 3.1 单个 class，9 个同模块实现文件

`class Parser`（parser.lh:66-82）有 10 个私有字段：`source`、`tokens`、`next_token_index`、
`current_token`、`current_token_index`、`previous_token`、`syntax_builder`、`diagnostics`、
`error_code`、`nesting_depth`。

**9 个 `.la` 全部是同一个 `impl Parser` 的横向切分**，共享私有声明——
这正是 AGENTS.md 偏好的"同模块实现拆分"而非"开子模块"。对照
`middleend/semantic/context/{lookup,builder}.la` 被写成了真子模块并反向 import 父 facade
（见 `codebase-middleend.md`），parser 的做法是正确范例。

**`facade.la`（137 行）**——`ParseResult` / `FrontendResult` 的构造与视图；
`Parser::init` / `run` / `take_result`；自由函数 `parse()`（119）、
`frontend_parse()`（128，lex→parse 串联）。

**`session.la`（327 行）**——游标原语 `advance` / `check` / `match` / `expect` /
`peek_kind` / `token_at`（26-260）；节点原语 `new_node` / `node` / `set_node_span` /
`add_child`（147-206）；`mark` / `restore`（291-325）；
`enter_nesting` / `leave_nesting`（271-289，上限 256）；
`input_is_valid()`（75-118）；`join_spans`（120）。

**`rules.la`（124 行）**——纯判定与优先级表：`is_builtin_type`、
`is_assignment_operator`、`binary_precedence`（55-91，1–10 级）、`access_flag`、
`operator_token_is_supported`——**全部 switch**。

**`literals.la`（191 行）**——字面量形状校验并给出 detail 码：
`parse_integer_token`（5）、`validate_float`（154）、`validate_hex_float`（108）、
`scan_float_digits`（60）。

**`types.la`（296 行）**——`parse_type`（292）/ `parse_type_active`（124）、
`parse_generic_arguments`（50）、`match_generic_greater`（29，把 `>>` 拆成两个 `>`）、
`finish_type`（76，尾部 `const` / `&` / `&&` 后缀）、`parse_qualified_name`（7）。

**`expression.la`（554 行）**——primary / postfix / unary / cast / binary / conditional；
聚合初始化式、call、`@intrinsic`、`sizeof/alignof/offsetof`、`va_arg`、switch 标签。

**`statements.la`（662 行）**——block / if / while / do / loop / for / switch / assert /
`va_start` / goto / label / return / break-continue；`parse_attributes`（40）、
`parse_variable_declaration`（115）、`parse_const_declaration`（147）、
`parse_asm_body`（177）。

**`classes.la`（281 行）**——`parse_field`（11）、`parse_class_member`（99）、
`parse_operator_declaration`（67）、`parse_friend_declaration`（83）、
`parse_aggregate_declaration`（168）、`parse_opaque_class_declaration`（204）、
`parse_impl_member`（221）、`parse_impl_declaration`（255）。

**`declarations.la`（490 行）**——顶层分派：`parse_program`（401，module + 循环 import
含别名与选择性 import）、`parse_top_level_declaration`（303）、
`parse_enum_declaration`（49）、`parse_parameter`（101）、
`parse_function_with_name`（178）、`parse_type_alias_declaration`（249）、
`parse_generic_parameters`（5）、`at_type_alias_declaration`（285）。

### 3.2 递归下降与优先级爬升

```
parse_program → parse_top_level_declaration → parse_aggregate_declaration(member_parser)
  → parse_class_member → parse_field / parse_function_declaration
    → parse_function_with_name → parse_block → parse_statement
      → parse_expression_statement → parse_expression → parse_conditional_expression
        → parse_binary_expression(1) → parse_cast_expression → parse_unary_expression
          → parse_postfix_expression → parse_primary_expression
```

二元表达式是**优先级爬升**（precedence climbing）：`parse_binary_expression(minimum_precedence)`
（expression.la:459-489）在 `precedence < minimum_precedence` 时中断，右操作数递归
`parse_binary_expression(precedence + 1)`（473 行）。同一级运算符由一个循环持续消费，
右操作数递归处理更高优先级，由此得到左结合；无需为每一级 precedence 写独立 parser。

`parse_statement`（statements.la:588-661）、`parse_primary_expression`（expression.la:250-315）、
`parse_top_level_declaration`（declarations.la:317-390）都是 `switch (token.kind)` 分派。

### 3.3 绑定方法指针避免开子模块

`classes.la:3` 定义了 `type MemberParser = method fn(usize) -> usize;`。
`parse_aggregate_declaration` 接受一个 `member_parser` 参数（declarations.la:329/335/341/347
分别传入 `parse_field` / `parse_class_member` 等），于是 struct / class / union / impl
共用一个聚合解析骨架，**不需要为每种聚合开一个子模块或复制代码**。

这是 AGENTS.md 明确认可的写法，与 `middleend/semantic/stmt/api.lh` 用函数指针
（`Lowerer` / `Cleanup` / `NeedsCleanup`）让 `labels` 子模块免于 import 父 facade 是同一技巧。

### 3.4 错误处理与三层恢复

**诊断不中断解析**。`add_diagnostic`（session.la:135-145）把 `diagnostics.add()` 的返回值
直接赋给 `error_code`——成功时为 `none`，因此单条诊断**不会**让解析中止，可以连续报多个错误。
真正的运行时错误（OOM、越界）才通过 `fail()` / `new_node` 的 `error_code` 让整条递归
快速返回 `no_index()`。

恢复策略有三层：

1. **回溯**——仅用于泛型实参歧义。`parse_postfix_expression`（expression.la:322-353）先 `mark()`，
   试解析 `<...>`，仅当"无新增诊断且后随 `(` / `.` / `->`"才接受，否则 `restore(mark)`
2. **跳过单 token**——所有列表循环都记 `index_before`，若
   `current_token_index == index_before && kind != end` 就 `advance()`
   （declarations.la:481、statements.la:227、classes.la:194、expression.la:85）
3. **跳过到右括号/右花括号**——`parse_asm_body`（statements.la:201-203）对非字符串体
   一路 `advance()` 到 `at_right_brace_or_end()`

`enter_nesting` / `leave_nesting`（session.la:271-289，上限 256，parser.lh:63）
防止深表达式爆栈。

节点失败统一返回 `syntax::no_index()`，父节点用 `if (child != none)` 跳过挂接，
所以**错误语法树仍然结构良好**——语义层可以信任树的形状。

### 3.5 输入前置校验

`input_is_valid()`（session.la:75-118）在解析前对 token 流做全量校验：种类范围、
span 单调、行列号重算一致、末 token 必须是 `end`。它是"数据表 + 循环"风格的校验样板。

---

## 4. 前端的风格达标情况

前端是全仓唯一在三项硬指标上全部达标的区域。

| 指标 | 前端 | 全仓对照 |
|---|---:|---:|
| 手动索引 `while` | **0** | 生产代码 54 处，其中 middleend 51 处 |
| 超两子句条件 | **0** | 19 处，全在 middleend |
| 长 if 链（kind 分派） | **0** | middleend 有多处 15–20 个串行 if |

`while` 只出现在开放式 token 循环（没有天然上界的位置），这是恰当的：
`types.la:11,144,178`、`declarations.la:33,189,428,453,475`、
`statements.la:47,66,439`、`expression.la:106,131,180,435,465`。

---

## 5. 技术债

### 5.1 构造/析构用字符串比较识别（软关键字）

`classes.la:151-160` 与 `237-246` 用 `identifier_text_is(this->current_token, "init", 4)` /
`"deinit", 6` 识别构造函数与析构函数——是字符串比较而不是关键字。

`init` / `deinit` 不是 lexer 级保留字，但在 class / impl 成员位置是上下文保留的特殊名字；
它们在其他标识符位置仍可使用，不能据此推出 class 字段也可同名。真实技术债是两处字符串
识别逻辑重复。是否提升为全局关键字需要语言表面决策，不能只为改成 `switch` 扩大保留字集合。

### 5.2 七处短的 `else if` 链

都是 2–3 分支，可接受但理论上可表格化：

- `parser/literals.la:10-19`——`0b` / `0o` / `0x` 前缀，与 `number_base()` 的 switch 版本重复
- `expression.la:152-157`——sizeof / alignof / offsetof → kind 三分支
- `types.la:134-139`——`method fn` / `fn`
- `types.la:242-247`——named / builtin
- `declarations.la:223-243`——bodyless / asm / body
- `statements.la:199`
- `syntax/verify.la:55`

### 5.3 文件体量

`declarations.la`（490）、`statements.la`（662）、`expression.la`（554）处于
"150–800 行"目标区间的中上段，但切分边界清晰（顶层 / 语句 / 表达式），
符合"按 pass 边界切分"而非按行数机械切分的要求。三个文件都在 2,000 行天花板内。

### 5.4 可读性问题

`parser/types.la:244-245` 的 `is_builtin_type(\n this->current_token.kind)` 是
120 列预算挤压出的机械折行，可读性略差。

### 5.5 与后端的方向性瑕疵

`luna.compiler.ir` 为了 `Instruction.span`（`lexer::SourceSpan`）与 `Instruction.operation`
（`lexer::TokenKind`）而 import 了 `luna.compiler.lexer`（`compiler/include/luna/compiler/ir.lh:3`）。
即 middleend 依赖 frontend 的 token 枚举——这是全仓依赖图上唯一的方向性瑕疵。
把这两个字段换成 IR 自有的 span / 运算符枚举即可切断，前端本身无需改动。
