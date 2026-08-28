# 统一 Luna Tools 模块设计

## 目标与协议边界

当前发行物已经只有一个 `luna` 可执行文件，但源码仍保留 `luna.tools.cli`、
`luna.tools.compile`、`luna.tools.assemble` 与 `luna.tools.link` 四个模块。后三个接口各只有
一项 `run(argc, argv)`，没有独立消费者或依赖边界。本批次把它们原子合并为唯一
`luna.tools` 模块，同时保留所有命令文本、退出码、固定协议文件名和 compile/assemble/link
可观察阶段边界。

本批次不增加 `luna build`、包发现、动态链接或隐藏的 in-process pipeline。三条命令仍通过
assembly、LUNAOBJ1/ELF 与 executable 文件解耦。

## 领域对象

模块只使用五个有真实责任的类：

- `CommandLine`：持有 argc/argv、当前游标和 `--` 状态，统一安全 lookahead 与参数消费；
- `ToolDriver`：根命令分类、help/version 和封闭 `switch` 分派；
- `CompileCommand`：编译模式、输入/输出路径、固定协议和 compilation session 清理；
- `AssembleCommand`：输入/输出路径、对象格式选择和 assembly session 清理；
- `LinkCommand`：有界输入路径集合、输出路径、固定协议和 ObjectSet/link session 清理。

命令类的构造函数建立空参数状态，析构统一释放已经构造的 Path。成功、用法错误和阶段错误仍返回
原有 i32 状态，不引入异常或新的 result 外壳。诊断格式化、字节比较和 first-error 等真正无状态的
小算法可以保留为模块自由函数。

根分派是闭合集合，使用 enum + `switch`，不建立虚拟 Command 基类或 Strategy 层次。三条命令
共享词法级 CommandLine，但不共享含糊的 manager/context 状态。

## 模块和文件

唯一接口是 `drivers/include/luna/tools.lh`，只公开 `run(argc, argv)`。所有实现文件属于同一模块：

| 文件 | 职责 |
| --- | --- |
| `cli.la` | CommandLine、命令表、共享文本/错误算法 |
| `compile.la` | CompileCommand、固定编译协议、诊断和 backend 输出 |
| `assemble.la` | AssembleCommand、格式选择和原子对象输出 |
| `link.la` | LinkCommand、Object/ELF 输入和 executable 输出 |
| `facade.la` | ToolDriver 生命周期与根命令分派 |
| `entry.la` | 独立 driver module 的 `main`，只调用 `luna.tools::run` |

前三个 3 行命令接口和旧 `cli.lh` 全部删除。`stage_*` 是历史 bootstrap 文件名，改为领域名；固定协议
仍是命令方法的一种调用模式，不再污染文件和函数命名。

## CLI 与固定协议

必须逐字节保留：

- root help/version、缺失/未知命令状态 125；
- compile/assemble/link 的 help/version 文本；
- `--`、`-o`、compile mode 与 assemble `--emit` 解析；
- 输入路径不得与输出路径相同；
- compile/assemble/link 无额外参数时分别选择已有固定协议；
- 固定协议成功仍返回 42，普通 CLI 成功转换为 0；
- 所有现有阶段错误码和诊断前缀。

固定文件名、大小上限和版本内容继续由各命令自己的具名 enum 管理，不上移到根 facade。

## 当前 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | 五个命令/游标类保护解析、路径和阶段清理不变量 |
| composition/RAII | 命令组合 CommandLine；析构释放已构造 Path，阶段资源按命令集中收尾 |
| generics | 当前命令只使用固定 ABI 上限数组；没有真实可复用动态元素集合，不新增泛型实例 |
| move | 命令不逃逸 facade；Path 仍是迁移期句柄，不伪造无实现的 move 协议 |
| overload/default | 三条命令语义不同；用独立类而非装饰性 overload/default |
| operator | 命令和参数没有自然值运算 |
| bound methods | 所有解析与执行步骤绑定各自命令；没有需要保存的 callback |
| friend | 类通过公开窄方法协作，不需要扩大私有访问面 |
| inheritance/virtual/RTTI | 命令集合封闭，enum + switch 比运行期层次准确且无额外成本 |

## 基线与验证

caw 上旧 anchor 的模块汇编基线：

| 模块 | 时间 | 字节 | SHA-256 |
| --- | ---: | ---: | --- |
| tool_cli | 0.022s | 42,398 | `b0c6708c421959b7021fd08e4984c06929fe5c026de8d0c78cba36b89a1fd70b` |
| tool_compile | 0.503s | 486,104 | `276f5782776e7631b9c60cdfc44570e590ac58e66085abc34d1b24824d7c921b` |
| tool_assemble | 0.156s | 158,019 | `8d0aac8078e84a75596414e9885e3d98c537feb908834ec25929706c8ff8a5e7` |
| tool_link | 0.182s | 207,072 | `2608d37dafcb72b015ae73db043ae55683d44ad2ebad447f773d0c95027a6b81` |
| root driver | 0.013s | 29,102 | `c0dd6381f872193434fe31ab7243a20415e203111ea49ffeaa191fc06857b5c9` |

最终必须通过根/子命令 CLI 精确文本、固定协议、输出字节、audit、formatter、完整测试和
`verify --fresh`。模块合并会改变内部汇编哈希，不允许改变工具产物和协议结果。

## 验证结果

最终在隔离的 caw x86-64 Linux 工作区验证通过：

- audit 从 75 modules/65 library objects 收缩为 72/62，formatter 为 0 reflow/0 token drift；
- 10 组 root/command help/version/status 与旧 anchor 逐字节一致；
- 新增固定协议回归完整执行 compile→assemble→link，三个命令均返回 42，最终程序退出 42；
- 固定协议 assembly/object/executable 分别为 951/597/4,190 字节，SHA-256 为
  `aa5023c18d89eb0ea04a0764d4cc7602afbc784774d4845141fcd2c6f338d3a6`、
  `d7c2ff72e8065eecc7a9ae56c43404dd576841723b8c47cdd18d637bb5e79b55` 和
  `09a52ee70e72522c7b8d2c39df588d8291683470c9befe9502abcede66260cae`；
- 完整测试扩展为 447/447，通过用时 4.39 秒；
- `verify --fresh` 用时 53.03 秒，stage-next/stage-fixed 全部 artifact 逐字节一致；
- 最终 unified tools assembly 为 909,810 字节，`luna` 为 4,884,298 字节，SHA-256 分别为
  `bc530e7422738a6d9eeb6a383d6a9ca318d2fb21874d82863244414bb9dd6c4f` 和
  `ca1b1c80e8c03b7d0a56b880ce5ef18f69a3fb35fa41e18b3445b28cb1c9d786`。
