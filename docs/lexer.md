# Lexer 现代化设计

## 目标与冻结契约

当前 Lexer 是 723 行的 `luna.bootstrap.frontend.lexer` 单文件实现。它把源码游标、Token、诊断和错误放在
公开字段组成的 `LexerState` 中，再由一组接收 `*LexerState` 的自由函数推进。Token 和诊断存储是
`bytes::Buffer` 加 count 的浅拷贝记录，调用端必须维护成对 release；关键字表还会在每次标识符识别时
重新构造。

本批次把模块原子迁移为 `luna.compiler.lexer`，但冻结以下语言契约：

- `TokenKind` 与 `DiagnosticKind` 的成员、顺序和数值不变；
- 每个 Token 的 kind、offset、length、line、column 不变；
- 非法 Token、诊断种类、顺序、detail 和资源上限不变；
- UTF-8、NUL、注释、数字、宽字符串、转义和最长 Token 的接受/拒绝行为不变；
- 每个输入仍恰好以一个零长度 end Token 结束。

直接 import 旧模块的 28 个接口或实现单元全部在同一批次迁移，不保留 forwarding 模块。

## 领域对象与所有权

`SourceSpan`、`Token` 和 `Diagnostic` 是透明的被动值。非拥有访问由轻量 `TokenView` 和
`DiagnosticView` 表达，它们只保存只读指针和元素数，并集中完成形状与下标验证。

拥有资源使用类：

- `DiagnosticBuffer`：以 `vector<Diagnostic>` 保存诊断，提供 add、truncate 和只读 view；
- `LexResult`：move-only 地组合 Token 存储、DiagnosticBuffer 与 sticky error；
- `Lexer`：私有持有源码视图、位置游标、KeywordTable 和正在构造的 LexResult；
- `KeywordTable`：每个 Lexer 会话构造一次 67 项规则，所有标识符共享该表；
- Parser facade 的 `ParseResult` 与 `FrontendResult`：只迁移结果生命周期，不在本批重写语法算法；
- Tools 内部的 `FrontendStorage`：把多单元结果汇聚到连续 typed pools，向语义阶段返回稳定视图。

`lex()` 返回 RAII LexResult，不再公开 release 函数。Parser 借用 TokenView，Parser 自己的诊断使用同一
DiagnosticBuffer。单个 FrontendResult 只在一个源文件的加载作用域中存在；成功结果复制进入
FrontendStorage 后立即析构。FrontendStorage 是多单元前端数据的唯一所有者，CompileCommand 只保留
仍属迁移期的源文件 bytes 数组。

FrontendStorage 的汇聚会为 Token、诊断和 SyntaxNode 增加一次线性复制。复制只发生一次，四个 pool 在
追加前按累计大小 reserve，避免逐元素扩容；相较跨模块暴露 raw detach 描述符，这个方案减少所有权状态和
释放路径，并保证所有借用 view 只在最后一次 add 之后产生。当前 anchor 不能在一个导出类的私有字段中直接
实例化 `vector<外部模块类型>`，因此 Tools 内部用单字段本地记录保持强类型存储；该限制不进入公开 API。
当前最多 64 个输入、32 MiB 源码和既有限额下，这项成本可控；最终性能数据决定是否需要后续 arena 优化。

## 状态转换与失败传播

Lexer 保持一个 sticky `runtime::Error`。任一容器操作失败后，扫描方法不再改变输出。普通非法字符仍生成
invalid Token 和词法诊断，不等同于资源错误。

主流程是：

1. 校验 UTF-8、NUL 和源码形状；
2. 跳过空白或注释；
3. 捕获 Token 起始位置；
4. 按标识符、数字、引号字面量或封闭标点 switch 分派；
5. 追加 Token/诊断，直到 end Token 或 sticky error；
6. move 返回 LexResult。

有界字符、关键字、Token 和诊断遍历使用 `for`。标点使用 `switch`，关键字使用表驱动查找；不使用枚举
if 链。任何条件最多两个逻辑项，宽字符串前缀、数字指数和块注释终止条件由具名谓词收敛。

## 模块与文件

唯一接口是 `compiler/include/luna/compiler/lexer.lh`，正常保持在 250 行以内。实现都属于
`luna.compiler.lexer`：

| 文件 | 职责 |
| --- | --- |
| `facade.la` | 结果生命周期、公开 lex 入口与会话收尾 |
| `session.la` | 游标、span、trivia、sticky error 和主扫描循环 |
| `keywords.la` | ASCII 分类、关键字表构造与查找 |
| `literals.la` | 标识符、数字、字符和字符串扫描 |
| `token.la` | 单 Token 分类和标点 switch |

同时把已有 `frontend/parser.la` 和 `frontend/syntax.la` 移入各自目录，使 `compiler/src/frontend/` 这一层
只包含 lexer、parser、syntax 三个目录，不再混放文件与子目录。Parser 子模块本批不合并，后续 Parser
现代化再原子处理它们。`drivers/src/frontend.la` 是 Tools 模块内部的多单元存储边界，不创建新的公开模块。

## 当前 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | Lexer、KeywordTable、结果和资源容器保护游标、形状、sticky error 与释放不变量 |
| composition/RAII | Lexer 组合结果与表；FrontendResult 组合词法/语法结果；FrontendStorage 统一拥有多单元 pool |
| generics | Lexer 使用 `vector<Token>`/`vector<Diagnostic>`；Tools pool 使用本模块 typed record 的 vector |
| move | LexResult、DiagnosticBuffer、ParseResult、FrontendResult 在构造/返回边界显式 move，不提供浅拷贝 |
| overload/default | view 的只读 get 与结果查询没有同名语义变体，不增加装饰性 overload/default |
| operator | Token、span 和诊断没有自然算术或全序，不定义 operator |
| bound methods | 所有依赖会话状态的扫描、追加、查询和释放行为绑定所属对象 |
| friend | 只在 Lexer 构造私有 LexResult 存储时使用同模块定向 friend，不扩大跨模块访问 |
| inheritance/virtual/RTTI | Lexer 只有一种封闭策略，不存在运行期替换或类型识别需求 |

## 基线与验证门禁

提交前 anchor 在隔离的 caw x86-64 Linux 工作区对旧 Lexer 连续编译三次，得到
0.261732/0.263503/0.265768 秒，中位数 0.263503 秒。三次汇编均为 490,491 字节，SHA-256 均为
`88d856eba2556a5c633e6356c69f5d297cab4db45fadb4a8409b54d639a89eb3`。

最终必须通过：

- 独立编译消费端并链接真实 lexer.lo 的关键字、标点、字面量、trivia、span/diagnostic 与 RAII 回归；
- 现有 CLI、固定协议和完整行为测试；
- audit 与 formatter；
- `verify --fresh` 的 transition→next→fixed 全 artifact 逐字节一致；
- 三次 Lexer 模块编译的大小、SHA-256 和中位耗时记录。

## 验证结果

最终在隔离的 caw x86-64 Linux 工作区完成全部门禁：

- audit 为 72 modules、1 driver、62 library objects；formatter 为 0 reflow/0 token drift；
- 对 Lexer、必要的 Parser/Context 迁移、Tools 前端存储和契约测试共 16 个文件执行控制条件扫描，所有
  `if`/`for`/`while` 均不超过两个逻辑项；
- 专用 frontend 协议独立编译消费端，再链接真实 `lexer.lo` 与依赖对象，完整覆盖 67 个关键字、46 个
  标点/复合操作符、数字、字符/窄/宽字符串、trivia、精确 span、非法字符、未终止注释/字符串、NUL、
  非法 UTF-8 和 RAII 析构；
- 完整测试为 448/448，0 failed、0 skipped，用时 4.90 秒，峰值 27,200 KiB；
- `verify --fresh` 用时 61.59 秒，峰值 34,368 KiB，stage-next/stage-fixed 的全部 assembly、object 和
  最终 executable 逐字节一致。

同一旧 anchor 对最终 Lexer 源码连续编译三次为 0.525948/0.559194/0.511968 秒，中位数 0.525948 秒；
三次均生成 805,721 字节汇编，SHA-256 为
`4b69e57ee46d0d6ef10e9b96d54801d5807fa0e21d06ad9990b55d242ed4f3f8`。相较 0.263503 秒、
490,491 字节的过程式基线，编译中位耗时约增加 99.6%，模块汇编约增加 64.3%；这是类生命周期、typed
vector 和公开 view/result 边界的无优化代码生成成本，不代表 Lexer 运行吞吐。本批不引入提前优化，后续
以真实编译工作负载决定是否优化关键字查找或泛型实例合并。

stage-fixed `lexer.lo` 为 280,701 字节，SHA-256 为
`6d0b203f92b94d605681c77233264022142d2154e9035a4820cd95df71b84f38`。最终 `luna` 为 4,962,131 字节，
SHA-256 为 `ed19edea6b21ffd169850d079b674e866a61088249bb227ff5fbd0061fbc0f37`。
