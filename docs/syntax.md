# Syntax 现代化设计

## 目标与冻结契约

当前 `luna.bootstrap.frontend.syntax` 以公开 `bytes::Buffer + count + token_count + root` 记录同时表达
拥有型语法树、可变建树状态和只读消费视图。Parser 直接追加裸字节、截断 buffer、修改节点指针和根索引；
ParseResult 再通过 `syntax_tree_release()` 手工释放。这让树的形状、所有权和 mutation 不变量分散在 Syntax、
Parser State、Tools 与 semantic context 四处。

本批次把模块原子迁移为 `luna.compiler.syntax`，冻结以下可观察契约：

- `SyntaxKind`、`SyntaxFlag` 的成员、顺序和值不变；
- `SyntaxNode` 的字段、索引含义、父子顺序、span 与 token_index 不变；
- 空树、program root、节点/Token 上限和 `no_index()` 哨兵不变；
- 完整树继续验证 kind/flag、span 算术、token 范围、父子/兄弟一致性、唯一父节点和 root 可达性；
- Parser 生成的节点顺序和最终树逐节点不变。

直接 import 旧模块的 32 个接口或实现单元全部同批迁移，不保留 forwarding 模块。

## 领域对象与所有权

`SyntaxKind`、`SyntaxFlag` 和 `SyntaxNode` 保持透明被动值。三个有状态边界使用类：

- `SyntaxView`：只读、非拥有，保存节点指针、节点数、Token 数和 root，供 semantic、Codegen 和 Tools 查询；
- `SyntaxTree`：move-only RAII owner，以 `vector<SyntaxNode>` 保存完成树，并只公开 view/get/shape 查询；
- `SyntaxBuilder`：唯一 mutation 边界，集中负责 add、set span/token、add flags、add child、set root、
  mark/restore 和最终 move 交付。

Parser State 组合 SyntaxBuilder，不再保存公开 SyntaxTree。Parser 辅助接口只返回 SyntaxNode 值，不再把
`SyntaxNode*` 暴露给 expression/statements/declarations；唯一现存的 token_index 修改改为 Builder 的具名
方法。ParseResult 直接拥有 SyntaxTree，析构自动释放；ParseView 和 semantic Unit 只保存 SyntaxView。

Tools 的 FrontendStorage 仍按多单元连续 pool 汇聚节点，但直接构造 SyntaxView，不再伪造带 capacity 的
借用型 bytes::Buffer。所有 view 只在 pool 最后一次增长后供 semantic 阶段长期保存。

## Builder 状态与失败传播

SyntaxBuilder 构造时固定 Token 数并建立空 typed vector。它保存 sticky `runtime::Error`：首次非法索引、
资源上限或 vector 失败后，后续 mutation 不再改变树。Parser 将 Builder 错误同步到自己的阶段错误；节点
上限仍先保留 `resource_limit` 诊断，再传播 out-of-memory 风格错误。

Builder mutation 保持以下不变量：

1. 新节点从 detached 状态开始，所有 absent index 都是 `no_index()`；
2. child 只能连接一次，parent 不能等于 child；
3. sibling 链按 add_child 调用顺序构造，first/last/count 同步更新；
4. mark/restore 只截断 mark 后尚未挂入既有树的 speculative 节点；
5. root 只设置为现有 program 节点；
6. `take_tree()` 只移动存储，不复制节点，moved-from Builder 保持可析构。

最终完整验证沿用现有确定性算法。其祖先可达性检查最坏为 O(n²)，本批保持语义和无额外分配特性；不在
没有真实前端运行基准的情况下引入颜色表或第二套 arena。

## 模块与文件

唯一接口是 `compiler/include/luna/compiler/syntax.lh`，正常保持在 250 行以内。实现都属于
`luna.compiler.syntax`：

| 文件 | 职责 |
| --- | --- |
| `tree.la` | no-index、只读 view、move-only tree 与查询 |
| `builder.la` | 节点创建、字段 mutation、父子连接、mark/restore 与交付 |
| `verify.la` | 完整结构验证和具名节点不变量 |

不新增 `syntax.storage`、`syntax.builder` 或 `syntax.verify` 子模块；三者没有独立消费者或依赖边界。

## 当前 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | View 保护形状，Tree 保护资源，Builder 保护 mutation/sticky-error 不变量 |
| composition/RAII | Builder 组合 Tree；ParseResult 组合 Tree 与 DiagnosticBuffer，全部自动析构 |
| generics | `vector<SyntaxNode>` 统一 typed storage，不保留裸 byte append/cast |
| move | SyntaxTree 只在 Builder→ParseResult 和 ParseResult 返回边界移动，不提供浅拷贝 |
| overload/default | Tree/View 查询语义明确，不增加装饰性 overload/default |
| operator | 节点和索引没有自然算术或全序，不定义 operator |
| bound methods | 所有建树、验证、查询和资源行为绑定对应对象 |
| friend | SyntaxBuilder 定向访问 SyntaxTree 私有存储，避免公开 detach/mutable storage |
| inheritance/virtual/RTTI | 语法树只有一种封闭表示，不存在运行期替换或类型识别需求 |

## 基线与验证门禁

提交前 anchor 在隔离的 caw x86-64 Linux 工作区对旧 Syntax 连续编译三次，得到
0.063613/0.063769/0.064049 秒，中位数 0.063769 秒。三次汇编均为 93,435 字节，SHA-256 均为
`7f03bf932d60d95faa073745259f13054e297ad6682fcb525182a10b6585095d`。

最终必须通过：

- 独立编译消费端并链接真实 `syntax.lo` 的 Builder、Tree、View、move、mark/restore 和结构契约；
- 原有 Lexer frontend 契约、CLI、固定协议和完整行为测试；
- 相关文件两逻辑项条件扫描、audit 与 formatter；
- `verify --fresh` 的 transition→next→fixed 全 artifact 逐字节一致；
- 三次 Syntax 模块编译的大小、SHA-256 和中位耗时记录。

## 验证结果

最终在隔离的 caw x86-64 Linux 工作区完成全部门禁：

- audit 为 72 modules、1 driver、62 library objects；formatter 为 0 reflow/0 token drift；
- 对 Syntax、全部 Parser、必要 Context/Codegen/Tools 边界和两个 frontend 契约共 18 个文件执行条件扫描，
  所有 `if`/`for`/`while` 均不超过两个逻辑项；
- Syntax 消费端独立编译并链接真实 `syntax.lo`，完整覆盖 Builder add/set/flags/child/root、mark/restore、
  Tree/View 查询、越界失败、move 与 moved-from 析构；原有 Lexer 独立契约同时保持通过；
- 完整测试扩展为 449/449，0 failed、0 skipped，用时 4.81 秒，峰值 27,040 KiB；
- `verify --fresh` 用时 57.42 秒，峰值 34,200 KiB，stage-next/stage-fixed 的全部 assembly、object 和
  最终 executable 逐字节一致。

同一旧 anchor 对最终 Syntax 源码连续编译三次为 0.194502/0.191968/0.190444 秒，中位数 0.191968 秒；
三次均生成 400,578 字节汇编，SHA-256 为
`d17042b2daf0618ee37cbf6a32ed2d7e8f723f4bb8ce7ca1b784a336ca5f794d`。相较 0.063769 秒、
93,435 字节的裸 buffer 基线，编译中位耗时约增加 201.0%，模块汇编约增加 328.7%；绝对编译耗时仍低于
0.21 秒，增长来自 Tree/View/Builder/Verifier 与 typed-vector 生命周期的无优化代码生成。本批记录该结构
成本，不在缺少真实前端运行基准时引入提前优化。

stage-fixed `syntax.lo` 为 146,877 字节，SHA-256 为
`7e84c13e9b503aa0db4e5a9edbae175754e0a23c9a99d17fa0b4d26306ec0fc7`。最终 `luna` 为 4,986,707 字节，
SHA-256 为 `77f0e575626bad58f91caa59fc2f6261d7b68cffcca6192ed9bcd11124a3d979`。
