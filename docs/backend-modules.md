# x86-64 后端模块收缩

## 目标

历史后端把实现文件路径直接映射成 20 个`luna.bootstrap.backend.x86_64.*`模块。ABI、
frame、instruction、reader、writer、operand 和 encoding 都没有父 facade 之外的独立消费
者，却分别承担接口、object 和 import 边界，放大了注册表、固定点链接闭包和模块指纹面。

本批次只收缩依赖边界，不把实现合成大文件，也不改变算法或二进制格式。后端现在只有六个
模块：

| 模块 | 实现文件职责 |
| --- | --- |
| `luna.compiler.x86.text` | checked assembly text |
| `luna.compiler.x86.codegen` | facade、ABI、frame、support、value、call、instruction |
| `luna.compiler.x86.object` | LUNAOBJ1 被动对象模型与序列化 |
| `luna.compiler.x86.elf` | facade、format、reader、writer |
| `luna.compiler.x86.assembler` | facade、operands、symbols、encoding、source |
| `luna.compiler.x86.linker` | static ELF64 link |

模块数从 20 降为 6；`LIBRARIES`仍列出每个 implementation path，但只为六个接口生成六个
library object。

## 文件与接口

每个模块只有一个`.lh`，并保留多个职责清晰的`.la`：

```text
compiler/src/backend/x86_64/
  codegen/{facade,abi,frame,support,value,call,instruction}.la
  assembler/{facade,operands,symbols,encoding,source}.la
  elf/{facade,format,reader,writer}.la
  object/object.la
  linker/linker.la
  text/text.la
```

`x86_64/`只包含模块目录；每个模块目录只包含实现文件，不混合下一层目录。接口行数为：

- codegen 175；
- assembler 119；
- object 95；
- ELF 41；
- text 20；
- linker 13。

接口只保存导出契约和跨 implementation unit 共享的记录。算法、解析、编码和 writer body
继续留在原职责文件中。`SymbolName`、`SymbolBinding`和`SymbolTable`因当前跨模块泛型
单态化需要公开类型身份，但它们仍是 assembler 的支持契约，不是独立模块。

## 依赖方向

```text
frontend / middleend
        │
        ▼
     codegen ─────► text

assembler ───────► object
    │                 ▲
    └─► std::map      │
                      │
elf ──────────────────┤
linker ────────────────┘
```

drivers 只消费六个 facade 中与命令相关的入口。后端模块之间没有父 facade 反向导入，也没有
implementation-file 名称进入 module namespace。

## 最新语言特性审查

- 保留 SymbolTable 的 class、访问控制、构造/析构、泛型 map、组合和自然`operator <`；
- ABI、frame、operand、ELF record、Object 和 Fixup 继续是透明 struct；
- 本批次没有资源转移边界，因此不新增 move surface；
- 没有运行期替换层次，因此 inheritance、virtual dispatch 和 RTTI 不适用；
- 没有需要捕获对象的策略回调，因此 bound method 不适用；
- 不使用 friend 扩大访问面；同模块 implementation unit 已提供所需私有共享边界。

后续 OOP backend 批次会分别把 CodeGenerator、Assembler、ElfReader、ElfWriter 和 Linker
升级为状态类。本批次刻意不把 namespace 迁移与行为重写混在同一个正确性变化中。

## 验证

迁移必须保持：

- audit 中模块注册、源码声明和 import graph 一致；
- stage-next 与 stage-fixed 的 assembly、object 和`luna`逐字节相同；
- 443 项完整测试、FFI 和负诊断全部通过；
- `sem_funcs.s`的 indexed assembler 性能不回退；
- LUNAOBJ1、ELF64 和 System V ABI 输出语义不变。

caw x86-64 WSL2 最终结果：audit 报告 74 modules、64 library objects；旧 anchor 构建
transition 用时 760.53 秒，indexed transition 连续构建 next/fixed 用时 172.76 秒；所有
next/fixed artifact 逐字节一致。完整测试 443/443 通过且无跳过，用时 20.49 秒。
`sem_funcs.s`汇编保持 0.75 秒，并与旧 anchor 产出的 LUNAOBJ1 object 字节一致。
