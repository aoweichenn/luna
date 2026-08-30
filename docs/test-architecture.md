# Luna 测试模块化架构

## 目标

历史 harness 把`tests/expectations.txt`的每一行都执行成一次独立 compile/assemble/link/run。
这为负例提供了精确首诊断隔离，但让大量普通正例重复启动工具链、解析公共接口并生成
短生命周期对象。

新架构把测试分为三层：

1. 可模块化正例组成少量 suite module graph，每个 suite 只编译、汇编和链接一次；
2. 编译期负例与信号/入口/源码路径敏感正例保持独立；
3. frontend、CLI、relocation、callable identity 和 FFI 等专用套件保持各自的模块协议。

## `TestSuite`

`tests.modules.framework.TestSuite`是运行期断言状态类。断言名遵循 C++ 测试框架惯例使用
大写：

- `EXPECT(condition, case_id)`记录首个失败并继续；
- `EXPECT_EQ(actual, expected, case_id)`比较整数结果并继续；
- `EXPECT_EXIT(actual, expected, case_id)`按 Linux 低 8 位进程退出状态比较；
- `ASSERT(condition, case_id)`记录失败并返回条件值，调用方可立即结束当前测试；
- `ASSERT_EQ(actual, expected, case_id)`提供对应的相等断言；
- 失败后继续执行同一 suite 的后续 case；
- `result()`返回 0 或首个失败编号。

不用宏模拟断言。TestSuite 具有真实状态和单一不变量，使用 class、private 字段和方法比
全局失败标志或一串立即 return 更清晰。

## 正例 suite 生成

harness 对满足以下条件的 expectation 自动生成少量大 suite 文件：

- 编译成功且预期退出码为非负整数；
- case 名唯一且没有显式`UNITS`顺序契约；
- 入口精确为`fn main() -> i32`，没有命令行参数；
- 不依赖`@file`、`@embed`、`@export_name`、`asm fn`、extern 或直接进程终止；
- 不包含额外 main 调用；
- 当前不含 import；已有模块边界的 case 保持原样，避免为了合批额外生成成百上千个接口单元。

生成过程移除原 case 的模块声明，把入口重命名为唯一的`run_case_NNNN`，并对该 case 的
顶层函数、类型和常量做 token-aware 前缀化。字符串与注释不参与改写，重载集合保持同一
前缀。处理后的多个 case 正文、suite driver 与`EXPECT_EXIT`调用写入同一个`.la`文件，
不会为每个 case 生成`.lh/.la`对。

每个 suite 的 case 数按 caw 同机基准约束，只向编译器提交 suite、TestSuite 实现和 TestSuite 接口
三个 source unit。这个上限控制单模块的声明规模，而不是逼近 64-unit 驱动上限。

## 必须隔离的测试

- `FAIL` expectation：一次失败编译只能验证一个首诊断；
- 预期信号或直接 process exit：会终止整个 suite；
- 显式`UNITS`：用于排列、重复、不可达和模块图契约；
- 源路径敏感、入口 ABI、extern/export-name/asm 测试；
- 带 import 的现有模块边界测试；
- 重复使用同一 case 文件但采用不同单元顺序的 expectation。

TypeTable 契约使用显式 `UNITS` 一次编译真实的六个 `luna.compiler.types` 实现单元及其依赖。它覆盖
builtins、canonicalization、callable 参数、布局、move/moved-from 和独立破坏性验证，不把每项规则拆成
单独工具链启动。

IR/semantic ownership 契约由 relocation-data 专用协议覆盖：move-only Input 在创建 InputView 前完成所有权转移，
私有 SemanticSession 再把 TypeTable、move-only `ir::Module`和 typed DiagnosticBuffer 转移进透明结果记录，不再
要求手工 release。用例继续把 Module 转移给`ir::Builder`构造全局引用，再以一次性 transfer 取回完成模块，并
确认原 Builder 已关闭。随后它破坏全局引用目标与零占位字节，确认独立验证拒绝损坏状态，恢复后重新接受，并覆盖
函数地址到对象重定位和最终链接的完整路径。

同一大用例还构造一个同时包含非法 alignment 与未知名字的源码：前者在类型布局前产生普通诊断，后者只能在最终
statement lowering 中产生。契约要求 SemanticResult 的 runtime error 仍为 none，且两个诊断都存在，从而证明
Session 在普通诊断后继续、只在 runtime failure 后停止后续 pass。

同一 relocation-data 大用例直接构造`ClassTable`，一次覆盖 record、field、method 和 friend 四个 typed vector，
验证三类成员切片的连续性、hierarchy flag、virtual slot、descriptor/vtable publication、move 后目标所有权、
moved-from 空状态以及首错粘滞。ClassRecord 不再包含 base/vptr 镜像，因此该用例也固定 ClassTable 的只读
projection 边界。该契约复用既有编译/链接协议，不为每个 class-model 规则新增一次工具链启动。

该大用例也直接构造`GenericTable`：验证声明参数连续切片、实例去重、16 到 32 bucket 的 rehash、type/function
反向映射、active-binding 回滚、深验证、move/moved-from 和首错粘滞。generic model 的索引与生命周期契约因此
同样不增加独立工具链启动。

同一 relocation-data 大用例还直接构造`SymbolTable`：验证 unit/module/import/symbol 四个 typed vector、接口和
实现单元计数、连续 import slice、图遍历/可达状态、符号 flag/value 受控发布、const projection、move 后所有权、
moved-from 空状态和首错粘滞。现有模块顺序、重复/循环 import、selective import、qualified name 和歧义 qualifier
用例继续覆盖`LookupResult`的行为分支；`audit`要求旧`context.lookup`接口与 registry object 消失。

该大用例也直接构造`CallableTable`：覆盖函数/参数/绑定 typed vector、canonical signature byte buffer、初始候选顺序
的事务式一次发布、参数与普通 binding 连续切片、const projection、深层 owner 校验、move/moved-from 和首错粘滞。
重复候选顺序、重复发布、重复 generic candidate 以及非法 binding extension 都必须被拒绝且不留下部分结果。const
函数参数复用 shared parameter vector，并由`functions/const.la`显式交叉验证；generic declaration 的反向 binding
则由`functions`跨 owner 校验。现有 overload/default/generic/method/bound-method/callable-identity corpus 继续覆盖
真实语义行为；自由泛型实例可复用 generic binding 而不污染普通候选序列。

semantic domain 收缩不新增逐概念编译用例；现有 overload、class policy、generic instance、method receiver、
reference binding、value category、lifetime 和负诊断 corpus 原样覆盖`luna.compiler.sema.domain`。`audit`验证旧
callable/value 模块已经消失；本批图审查另外确认 compiler production 中 class/generic owner 只由 Context 直接
导入，relocation-data 则有意直接导入 owner 以固定其 ABI 和生命周期。该大契约继续校验迁移后记录布局。

`functions.ir` 收缩同样不增加行为用例：Sema 的批次入口和 expression probe 的泛型实例路径继续覆盖两个 IR
construction 入口，`audit`则要求旧 child interface/registry node 消失，并确认 parent `functions`对象包含该实现。

`types.visibility` 收缩继续使用现有 private-type exposure、generic public argument、anonymous field、base class 和
function signature 负例；`audit`要求 visibility child interface/registry node 消失，Sema 只保留 parent `types`依赖。

`types.lookup` 收缩继续由直接字段、匿名提升、继承字段、ambiguous field、offsetof、initializer 和 member access
用例覆盖；`audit`要求 lookup child interface/registry node 消失，五个消费族统一使用 parent `types::lookup_field`。

这些任务仍使用独立工作目录，并与 suite 一起进入有界并行 worker pool。

Lexer、Syntax 与 Parser 契约属于 frontend 专用协议：消费端只携带相应接口独立编译，再链接 stage 中单独
生成的 `lexer.lo`、`syntax.lo`、`parser.lo` 与真实依赖对象。这样同时验证公开 ABI、RAII 析构和模块链接，
避免把实现与消费端错误地揉进一个源模块编译。Lexer 大用例覆盖全部关键字、标点、字面量、trivia、span
和诊断；Syntax 大用例覆盖 Builder、Tree、View、move、mark/restore 与失败状态；Parser 大用例覆盖完整
树形、泛型回滚、精确诊断、非法输入和 Result move 生命周期，不为每条细规则启动一次工具链。

## 失败与确定性

suite 编译、汇编、链接或执行失败时，harness 只对该 suite 回退到原始单例路径，以获得
精确 case 诊断；正常绿色路径不付出逐例编译成本。所有任务结果按 expectations 原顺序
汇总，worker 完成顺序不影响日志和最终计数。

## 验证指标

- suite 化前后通过数量和失败身份完全一致；
- `--jobs 1`与默认并行结果一致；
- 记录 suite 数、独立 expectation 数、compile/assemble/link 次数和墙钟时间；
- 负例、显式图测试和 FFI 覆盖不得因模块化而减少；
- test harness 变化不改变 stage-next/stage-fixed 编译器产物。
