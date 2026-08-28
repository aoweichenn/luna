# Parser 现代化设计

## 目标与冻结契约

历史 Parser 由 `luna.bootstrap.frontend.parser`、`state`、`expression`、`statements` 和
`declarations` 五个模块组成。公开 `State` 同时暴露 Token 游标、当前/前一 Token、SyntaxBuilder、诊断、
错误码和嵌套深度，139 个自由函数再通过 `*State` 传递同一个会话。五个模块共 3372 行，状态指针或
`parser_*` 调用约 1061 处；实现边界只是为缩短文件建立的伪依赖边界。

本批原子迁移为 `luna.compiler.parser`，冻结以下可观察行为：

- Token 的消费顺序、invalid Token 跳过规则和最大 256 层嵌套限制不变；
- 每个 SyntaxNode 的 kind、span、token_index、flags、插入顺序和父子顺序不变；
- 表达式优先级、泛型参数试探/回滚、声明和语句恢复边界不变；
- Parser 诊断的 kind、顺序、span、expected、found 和 detail 不变；
- `parse` 与 `frontend_parse` 的错误传播、恢复后有效树和词法/语法诊断分离契约不变。

全部直接消费者同批切换，不保留 forwarding 模块。历史四个 Parser 子模块及其接口被删除。

## 领域对象与所有权

`Parser` 是唯一可变解析会话。它私有拥有源码视图、TokenView、下一个 Token 索引、当前/前一 Token、
SyntaxBuilder、DiagnosticBuffer、sticky error 和嵌套深度。构造函数建立完整状态，`run()` 执行一次解析，
`take_result()` 把语法树与诊断 move 给 ParseResult。Parser 自身不导出，外部只能调用窄 facade。

`ParserMark` 是被动回滚快照，只记录游标、Token、SyntaxBuilder mark、诊断数量和嵌套深度；它不拥有资源。
SyntaxBuilder 仍是唯一语法树 mutation 边界。ParseResult 与 FrontendResult 保持 move-aware RAII，ParseView 与
FrontendView 继续是透明非拥有视图。

输入形状验证和 Token kind/操作符/优先级映射是无会话状态的算法，保留为模块内自由函数。所有依赖游标、
诊断或建树状态的行为都是 Parser 绑定方法，不再存在“状态结构体 + 自由函数族”。

## 状态机、分派与恢复

Parser 使用 recursive descent 状态机：Token 消费型循环使用 `while`，明确有界的源码、属性和节点遍历使用
`for`。主表达式、语句、顶层声明和封闭 Token 规则使用 `switch`；内建类型、赋值/一元/二元操作符与访问
标记集中在 `rules.la`，不使用长 if 链。

泛型调用与小于号歧义通过 ParserMark 事务处理：保存 Token、树和诊断边界，试探泛型参数；只有合法后缀
跟随时提交，否则同时恢复三个边界。类、结构和 union 共用 `parse_aggregate_declaration`，成员差异由
`method fn(usize) -> usize` 绑定方法传入；绑定接收者属于 Parser 会话生命周期，不需要裸 State 指针。

任一资源操作失败后，sticky error 阻止后续 mutation。普通语法错误只追加结构化诊断并在块、聚合、switch
或顶层边界保证前进，不错误地提升为 runtime failure。

## 模块与文件

唯一接口是 `compiler/include/luna/compiler/parser.lh`，共 85 行。全部实现属于同一个模块：

| 文件 | 职责 |
| --- | --- |
| `facade.la` | Result 生命周期、Parser 构造/收尾和公开 parse 入口 |
| `session.la` | 输入验证、Token 游标、诊断、Builder 适配、嵌套和 mark/restore |
| `rules.la` | 封闭 Token 分类、操作符集合、访问标记和二元优先级 |
| `literals.la` | 整数与浮点字面量验证 |
| `types.la` | 限定名、泛型参数、函数/指针/数组/引用类型语法 |
| `expression.la` | initializer、primary/postfix/unary/binary/conditional 表达式 |
| `statements.la` | 属性、局部声明、控制流、block、switch 和跳转语句 |
| `classes.la` | 字段、类成员、聚合、opaque class 和 impl |
| `declarations.la` | 泛型参数、enum、函数、type alias、import 和 program |

最大实现单元为 662 行，所有单元都处于 150--800 行正常范围；目录只含文件，不再混合子目录。

## 当前 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | Parser 私有保护游标、sticky error、嵌套与建树不变量；构造后立即可运行 |
| composition/RAII | Parser 组合 SyntaxBuilder 和 DiagnosticBuffer；Result 组合并 move 交付资源 |
| generics | 沿用 vector-backed SyntaxTree/DiagnosticBuffer；解析算法没有新的可复用类型参数轴 |
| copy/move | Parser 不复制；SyntaxTree、诊断和两个 Result 只在完成边界 move，moved-from 对象可安全析构 |
| overload/default | parse 与 frontend_parse 是不同层次入口，不用装饰性 overload/default 隐藏所有权差异 |
| operator | Parser、Token 和索引没有自然运算符语义，不定义 operator |
| bound methods | 聚合成员策略使用非拥有 method fn，递归下降行为全部使用隐式 this |
| friend | 现有 SyntaxBuilder 和 Result 窄接口足够，不为 Parser 打开私有存储逃逸口 |
| inheritance/virtual/RTTI | 文法和 Parser 策略是封闭静态集合，enum + switch 比运行期层次更直接 |

## 成本与基线

提交前 anchor 在隔离 caw x86-64 Linux 工作区顺序编译历史五个 Parser 模块三次，耗时分别为
1.379200、1.385370、1.379638 秒，中位数 1.379638 秒。五份汇编总计 2,176,417 字节，按模块顺序组合后的
SHA-256 均为 `bd613136bd9696efe3c91dd8618cdbd8803ebb414527d6ed89603689f31992c8`。

同一 anchor 编译最终单模块源码三次为 2.409001、2.523174、2.482031 秒，中位数 2.482031 秒；汇编均为
1,993,549 字节，SHA-256 为
`e3c97c2d4c58526fad2e375408ef01310af2a2aab07d62be6215935f8d34db69`。模块汇编总量减少约 8.4%，但当前
无优化编译器对一个较大声明域的处理使中位编译时间增加约 79.9%。本批记录该结构成本，不为了基准拆回伪
模块，也不提前改变语义算法；后续以真实全图构建数据决定是否优化同模块名称查找。

## 验证门禁

最终必须通过：

- 独立编译并链接真实 lexer.lo、syntax.lo、parser.lo 的三个 frontend 契约；
- Parser 契约中的完整树形、泛型/小于号回滚、精确诊断、非法 TokenView 和 move/moved-from 生命周期；
- 相关文件两逻辑项条件扫描、audit 与 formatter；
- `verify --fresh` 的 transition→next→fixed 全 artifact 逐字节一致；
- 完整行为测试和最终 Parser 汇编/object/executable 的大小与 SHA-256 记录。

## 验证结果

最终在隔离的 caw x86-64 Linux 工作区完成全部门禁：

- audit 为 68 modules、1 driver、58 library objects；formatter 为 0 reflow/0 token drift；
- 对 Parser 接口、九个实现单元和专用契约共 11 个文件执行条件扫描，所有 `if`、`for`、`while` 均不超过
  两个逻辑项；
- Lexer、Syntax、Parser 三个消费端分别独立编译，并链接真实模块对象；Parser 契约覆盖完整树形、泛型与
  小于号回滚、精确恢复诊断、非法 TokenView、move 和 moved-from 析构；
- 完整测试为 450/450，0 failed、0 skipped，用时 5.34 秒，峰值 26,560 KiB；
- `verify --fresh` 用时 56.05 秒，峰值 33,384 KiB，stage-next/stage-fixed 的全部 assembly、object 和
  executable 逐字节一致。

stage-fixed `parser.s` 为 1,993,549 字节，SHA-256 为
`e3c97c2d4c58526fad2e375408ef01310af2a2aab07d62be6215935f8d34db69`；`parser.lo` 为 756,785 字节，
SHA-256 为 `23f32b330d098d95719aabf9b9241d528992e7dd022972b1540f74d7ed0fabf0`。最终 `luna` 为
4,998,995 字节，SHA-256 为 `1b10d3581f344af84de632990460e8479fe384e8d26245ede01376c203335694`。
