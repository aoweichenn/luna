# x86-64 Codegen 现代化设计

## 目标与边界

`luna.compiler.x86.codegen` 把已验证 typed IR 降低为 Luna Assembler 消费的受控 x86-64
汇编方言。本批次不引入优化、machine IR、寄存器分配、PIC/PIE 或新指令；它只收敛
分析与输出的所有权、类型边界和控制流，并要求未修改输入的 assembly 逐字节不变。

## 领域对象

私有 `AbiLayout` 类拥有：

- `vector<Function>`；
- `vector<Parameter>`；
- `vector<Piece>`；
- System V 分类、寄存器分配和 stack-argument 布局的 first-error 状态。

私有 `FramePlan` 类拥有 `vector<Frame>` 和 `vector<Storage>`，并以一个完成的
`AbiLayout` 作为只读输入，规划 slot、scalar home、aggregate call result、hidden return pointer 和
variadic register-save area。

私有 `CodeGenerator` 类以非拥有视图保存 semantic input、IR 和 type table，并直接组合
`AbiLayout`、`FramePlan` 与 RAII `string`。它独占 current function、outgoing call-frame depth 和
first-error 状态。生命周期为：

```text
init -> validate -> AbiLayout::build -> FramePlan::build
     -> emit module -> take_result -> deinit
```

`take_result()` 只转移最终 assembly buffer。ABI/frame 是编译阶段内部计划，driver 和测试没有
独立消费者，因此不再通过 `BackendResult` 泄漏资源存储。公开结果只包含 assembly 和 error。

## 算法与分派

ABI `Function`、`Parameter`、`Piece`，frame `Frame`/`Storage` 以及 IR/type 记录继续是被动
struct。资源容器、构建不变量和查询行为属于 class。

- 有界遍历统一使用 `for`；
- opcode、type kind、ABI class、location 和 token kind 的封闭分派使用 `switch`；
- 输出方法共享 `CodeGenerator` 粘性错误，不用长 `if (!write())` 链；
- 条件最多两个逻辑子句，布局与范围使用命名谓词和 checked arithmetic；
- System V 分类保持当前渐进复杂度，不为展示抽象增加虚分派或额外扫描。

## 实现单元

所有文件保持同一 `luna.compiler.x86.codegen` 模块：

| 文件 | 职责 |
| --- | --- |
| `abi.la` | `AbiLayout`、System V 分类、参数/返回布局与查询 |
| `frame.la` | `FramePlan`、function frame 存储规划与查询 |
| `support.la` | assembly 写入、符号命名、type/source view 和基础 load/store |
| `value.la` | 值、内存、算术和比较指令 |
| `conversion.la` | 整数、浮点之间的显式转换与范围陷阱 |
| `call.la` | caller 参数准备、直接/间接 call 和结果回收 |
| `callee.la` | callee 参数入栈、variadic 状态和 return |
| `instruction.la` | IR opcode 到具体 method family 的顶层分派 |
| `module.la` | global、entry、function、asm body 和 module footer 输出 |
| `facade.la` | `CodeGenerator` 生命周期、阶段编排与公开函数 |

`callee`、`module` 只是 implementation concern，不新增子模块。

## 最新 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | 三个资源/阶段不变量分别属于 `AbiLayout`、`FramePlan`、`CodeGenerator` |
| generics | 五个 typed vector 替代五个裸字节容器、计数镜像和 pointer cast |
| composition/RAII | `CodeGenerator` 组合分析阶段和 `string`，失败无手工 release tree |
| move | 只在 `take_result()` 边界分离 assembly；三个 operation/resource class 不对外移动 |
| bound methods | 原 `*CodegenContext` 函数族变为自然的 session methods，不使用额外 callback 层 |
| overload/default | 当前输出操作无公开可选契约，不增加装饰性 overload/default |
| operator | 分析记录没有自然值语义 operator，不定义语法糖 |
| Strategy/State | ABI/frame 是真实顺序阶段，用类状态表达；单一 x86-64 target 没有第二策略，不建虚类层次 |
| friend/inheritance/virtual/RTTI | 没有需扩大访问面或运行期替换的 Codegen 层次，不适用 |

## 验证契约

基准输入是未修改的 `sem_funcs` 模块。旧 anchor 三次编译为 9.546/9.565/9.640 秒，
输出恒为 3,069,988 字节，SHA-256 为
`b9f50e44fa6a98aaed59c883a0ac97858a6a5c18d6e35ce40b761a4a041e6b0c`。新 Codegen 必须逐字节复现，
且编译时间不得出现可见回退。

最终还必须通过 Codegen/relocation 直接探针、443 项完整测试、audit、refmt 和
`verify --fresh` 字节固定点。

本批次在 caw（x86-64 Linux、4 worker、freestanding Luna toolchain）验证通过：新 stage 三次
编译基准分别为 6.014/6.040/6.024 秒，输出大小与 SHA-256 均和旧 anchor 完全一致；中位数
由 9.565 秒降至 6.024 秒，下降约 37.0%。Codegen/relocation 6 个直接探针、443 项完整测试、
audit、refmt 以及 62.47 秒的冷 `verify --fresh` 均通过，next/fixed 的 assembly、object 和最终可执行文件
逐字节一致。
