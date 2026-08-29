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
