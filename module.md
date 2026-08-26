# Luna 当前模块系统审查

## 文档范围

本文审查当前 `main` 上的纯 Luna 自举实现，而不是 `m0` 分支中的 C23
重建种子。

本文随当前实现维护；后续实现变化应同步更新其中的源码数量和优先级判断。

当前分支不生成或消费 `.lmi`：`tools/selfhost.py` 为每次编译传入根模块源码和
依赖接口源码，`lunac` 只提供 `--library` 与 `--executable` 两种编译模式。
旧的 `.lmi` 格式、完整 payload 指纹和带指纹的链接符号仍记录在
`docs/module-metadata.md`，但它们只描述 `m0` 历史实现。

因此，本文把问题分成三类：

1. 当前实现中已经存在的正确性、复杂度和扩展性问题；
2. 已归档的 `.lmi` 设计问题；
3. 将来重新引入二进制模块接口时才需要解决的问题。

## 结论

当前模块系统的表面规则总体正确：

- 一个模块至多有一个接口单元，并可由多个实现单元共同实现；
- 普通 import 只绑定模块限定符，不自动平铺名字；
- selective import 才显式引入未限定名字；
- import 是直接、非传递的；
- module graph 必须可达且无环；
- 接口函数与实现函数的类型签名必须匹配；
- `::` 只负责模块限定，`.` 负责字段和枚举成员访问。

当前仍需处理的主要是后续扩展问题：

1. Python 构建脚本维护了一份正则版 module scanner；
2. 名字查找仍反复扫描全局 symbol 表；
3. `Import` 同时承担依赖边和名字绑定；
4. module graph 仍依赖 AST node/token 身份；
5. 缺少 opaque/incomplete aggregate；
6. 驱动的 64 source-unit 上限比语义层的 1024 module 上限更早生效。

`.lmi` 指纹拆分不是当前第一优先级。只有重新引入二进制模块接口、增量缓存或
已编译包分发时，才应重新设计 public API identity、内部接口 identity 和单符号
ABI identity。

## 一、当前实现模型

### 构建边界

`tools/selfhost.py` 的 `LIBRARIES` 是生产 library/compiler module graph 的模块名与
接口/实现路径注册表。drivers 由 `DRIVERS` 注册；测试和 FFI module 使用各自的测试
路径约定。生产依赖和链接闭包来自源码 import：

```text
LIBRARIES registry
        |
Python import scan
        |
root implementations + root interface + dependency interfaces
        |
lunac
        |
assembly -> luna-as -> object -> luna-link
```

每个生产 library 单独编译。依赖模块只提供 `.lh`，其定义由另外生成的模块对象在
链接时提供；测试编译可以在一次调用中显式提供多个被测模块的 `.lh/.la`。当前没有
已编译接口文件或 metadata-only dependency。

### Compilation-set contract

`lunac` 不根据 module name 搜索路径或加载文件。调用方必须提供根模块和完整的依赖
接口闭包；module declaration 决定模块身份，构建驱动负责文件路径映射。

- executable compilation 的 root 是唯一包含 `main` 的 implementation module；
- library compilation 的 root 是输入单元 0 所属模块；
- 每个 direct import 必须在所提供的 source units 中解析成功；
- 所有提供的模块必须从 root 可达，并且整个所提供模块图无环；
- 提供了 implementation units 的每个模块都必须满足自己的 matching interface；
- interface-only dependency 的函数定义由另外编译和链接的模块对象提供。

source unit 的 module/import 前缀语法是：

```text
ModuleUnit        ::= [ "export" ] "module" ModuleName ";"
                      ImportDeclaration* TopLevelDeclaration*
ModuleName        ::= Identifier ( "." Identifier )*
ImportDeclaration ::= "import" ModuleName [ ImportBinding ] ";"
ImportBinding     ::= "as" Identifier
                    | "::" "{" Identifier ( "," Identifier )* [ "," ] "}"
```

import 必须紧跟 module declaration 并形成连续前缀。selective list 非空、名字不重复，
允许一个 trailing comma；alias 和 selective binding 互斥。

interface import 及其 alias/selective binding 会被所有 implementation units 继承；
任一 implementation unit 写下的 import 会进入共享的 module-private implementation
scope，但对 interface 不可见。同一 target module 在一个模块的 `.lh + 全部 .la` 中
合计最多 import 一次。

### 语义模型

当前核心记录是：

```text
Module {
    interface_unit
    implementation_count
    name_unit/name_node
    first_import/import_count
}

Import {
    owner_module
    source_unit/declaration_node
    target_module
    interface_import
    alias_token
    selective_node
}
```

语义流水线先收集全部 module，再收集 import 和校验图，然后依次收集类型、常量、
函数并降低函数体。名字仍以 `(unit, token)` 表示，比较时重新读取 source bytes。

### 链接符号身份

普通 Luna 函数的链接符号是：

```text
_L + hex(canonical module name) + _ + hex(function name)
   + __ + hex(versioned canonical callable signature)
```

它不包含模块指纹，但包含完整、版本化的函数签名。签名覆盖参数、结果、变参状态、
调用约定以及预留的 owner/receiver 类别，因此同名重载拥有不同链接身份，调用方与
实现方的 ABI 签名不一致也会在链接时表现为未解析符号。当前 self-host 流程仍从同一
源码图全量重建所有对象，避免混用旧对象。默认参数只属于源码接口和调用方物化，
不进入重载 key、链接签名或函数指针类型。

## 二、已冻结的即时规则

### qualifier 冲突在 import 阶段诊断

下面两个 import 都绑定 `text::`：

```luna
import foo.text;
import bar.text;
```

import 声明本身已经建立名字绑定。当前实现会在 import 收集完成后立即比较同模块
qualifier；即使程序没有使用 `text::`，也会在后一个 import 处报告
`ambiguous_module_qualifier`，并把前一个 import 作为 related span。

### selective import 边角

当前规则是：

- `import foo::{read, write,};` 合法，trailing comma 是正式语法；
- `import foo::{read, read};` 报 `duplicate_selective_import_name`；
- `import foo::{};` 报 `empty_selective_import`；
- `import foo as f::{x};` 由 semantic 报 `invalid_selective_import`。

最后一项保留 semantic 诊断，因为它比 parser 的通用 expected-token 诊断更明确。

## 三、名字查找和 import 验证

当前主要查找路径都是线性的：

```text
find_module_by_name       O(M)
find_local_symbol         O(S)
find_module_symbol        O(S)
find_qualifier_target     O(I)
find_imported_symbol      O(I * S)
find_module_binding       O(B)
```

其中 `M` 是 module 数，`I` 是当前模块 import 数，`S` 是 compilation 中的全局
symbol 数，`B` 是 callable binding 数。每次字符串比较还要重新访问两个 unit 的
source bytes。

`validate_imported_names()` 只处理建立 flat binding 的 selective imports；设其数量为
`J`，验证路径仍然较重：

```text
每对 import
    x 全部左侧 symbols
    x 全部右侧 symbols

每个本地 symbol
    x 每个 import
    x 全部 symbols
```

`import_binds_name()` 还会扫描 selective name 列表，因此严格最坏情况近似为
`O(J^2 * S^2 * L + J * S^2 * L)`，其中 `L` 是 selective-list 长度。

### 已有低风险快速跳过

当前 `library/`、`compiler/` 和 `drivers/` 中共有 434 条 import，其中 392 条是
alias import，没有一条 selective import。实现已经按 `selective_node != no_id`
跳过普通和 alias imports，因此当前生产源码不会进入 flat-name 的二次全表验证。

### 后续索引模型

测量确认仍有瓶颈后，再引入三层索引：

```text
NameTable
    identifier bytes -> NameId

ModuleSymbolIndex
    NameId -> SymbolId

UnitImportScope
    qualifier NameId -> ModuleId
    selected NameId  -> target ModuleId
```

当前 Luna 使用统一模块名字空间。类型、常量与 callable binding 仍不能占用同一个
顶层名字；函数重载则由一个 `Binding` 指向规范排序的候选切片。后续索引必须保留
这种集合语义，但不需要预埋通用 `NamespaceKind`。enum member 属于其 enum 的独立
成员空间，不进入模块顶层索引。

ImportScope 可以先把 selective name 映射到 target module；目标声明是否存在，
仍在相应声明收集完成后校验。这样不必为构建 Binding 提前打乱现有类型、常量和
函数 pass 的顺序。

## 四、依赖边与名字绑定应分离

当前一条 `Import` 同时表示：

1. 模块 A 依赖模块 B；
2. 这个 import 属于 interface 还是 implementation；
3. 当前 scope 使用什么 qualifier；
4. 是否平铺 selective names；
5. 诊断对应哪个 AST node。

因此 duplicate import 只按 `target_module` 判断。对于当前“一接口、一实现”规则，
这与语言禁止重复 import 的约束一致；但它会阻碍多个实现单元，因为两个实现文件
可能各自需要导入同一个依赖，而它们的名字绑定应当彼此独立。

面向扩展的内部模型应拆成：

```text
DependencyEdge {
    owner_module
    target_module
}

ImportBinding {
    owner_unit
    dependency_id
    qualifier
    selected_names
    source_ref
}
```

图遍历、环检查和 reachability 只读取去重后的 DependencyEdge；名字解析只读取
当前 unit 的 ImportBinding/ImportScope。

表面语法不必因此改变。selective import 是否保留，应作为独立语言决策，不应由
内部结构是否混合来决定。

## 五、module graph 与 AST 的耦合

`Module` 保存模块名 AST node，`Import` 保存声明 node、alias token 和 selective
container node。限定符解析会回到目标模块 AST，找到模块名最后一个 segment 后再
比较 token bytes。

在当前直接源码编译模型中，source、tokens 和 AST 本来就会一直存活到 backend，
所以这不是生命周期错误。但它造成了三个问题：

- module graph 依赖 parser 的具体 child 布局；
- 同一名字反复从 token/source 恢复；
- 将来加入 compiler module scanner 或二进制接口时难以复用图层。

合适的规范化边界是：

```text
Source AST
    |
ModuleUnitDescriptor
    |
ModuleGraph + UnitImportScope
    |
semantic declarations
```

Descriptor 应保存规范化 ModuleNameId、unit kind、import slice 和只用于诊断的
SourceRef。声明 AST 仍可供类型检查和函数降低使用；只需让模块图不再依赖 AST
结构即可，不必试图提前释放整棵语法树。

## 六、多个实现单元

一个模块可以由多个 `.la` 共同实现，而公共接口仍至多只有一个 `.lh`：

```luna
module luna.std.fs;
```

这些文件不是 partitions，也不产生新的限定名。当前采用简单的合并语义：

- 所有 implementation units 共享一个 module-private declaration namespace；
- 私有类型、常量、const fn 和普通函数可以跨实现文件直接引用；
- 任一 implementation unit 的 import 对全部 implementation units 可见；
- 同一 target 在 interface 和全部 implementations 中合计最多 import 一次；
- 一个接口函数在全部 implementations 中合计必须恰好定义一次；
- 重复私有声明和重复函数定义按普通 module duplicate 规则诊断；
- 所有实现文件一起降低为这个模块的一个 assembly/object artifact。

`Module` 只记录 `implementation_count`；当前最多 64 个 source units，语义 pass 在
需要时扫描 unit records 取得第 N 个实现单元。这避免为了早期规模引入额外索引或
partition graph。`LIBRARIES` 可显式登记一个有序 implementation path tuple，依赖图
取所有实现文件 imports 的并集。支持该能力的工具链提升为 anchor 后，生产编译器已
用它拆分 `semantic.functions` 的 const-fn 阶段、`semantic.types` 的布局阶段和
`consteval.engine` 的解释执行阶段；这些文件不增加新的 module name 或 import edge。

这个模型借用了 C++ named modules 的多 implementation unit 能力，但没有引入 global
module fragment、header unit、partition、隐式 re-export 或 BMI 选择规则。

## 七、opaque/incomplete aggregate

当前 parser 要求 `struct` 和 `union` 声明立即带完整 `{...}`，接口无法只暴露类型
身份：

```luna
export struct File;
export fn file_open(...) -> *File;
export fn file_close(file: *File);
```

这是实际的封装缺口。支持 incomplete aggregate 后，应允许指针声明、传递、返回和
比较，禁止需要完整布局的操作，包括按值对象、按值参数/返回、数组元素、字段访问、
指针算术以及 `sizeof`/`alignof`。

实现单元中的完整定义必须补全接口中同一个 TypeId，而不是产生第二个同名类型。
当前类型表已有 complete/resolving 状态和“指针不立即要求 pointee 完整”的基础，
但 parser、重复声明匹配和布局触发规则仍需扩展。

## 八、构建脚本的第二套 module scanner

`tools/selfhost.py` 使用正则分别识别 module 和 import。真正的 Luna parser 则允许
token 间换行，并对 identifier、alias 和 selective list 执行完整语法检查。两者
可能对同一源码得出不同模块图。

长期应增加只扫描 module/import 前缀的 compiler mode，例如：

```text
lunac --scan-module file.la
```

输出规范化 module name、unit kind 和 direct imports。构建脚本只消费结果，不再
理解 Luna import 语法。

切换必须分两步完成：

1. 保留 Python 正则，先把 scanner mode 落到固定点并提升 anchor；
2. 新 anchor 已支持 scanner 后，再让 `selfhost.py` 调用它。

否则旧 anchor 无法执行新 CLI，会破坏“旧工具链构建新工具链”的迭代纪律。

## 九、有效规模上限

semantic context 的 `maximum_modules` 是 1024，但当前 `lunac` driver 最多只接收
64 个 source units。library 编译至少需要根 implementation、根 interface 和全部
依赖 interfaces，因此实际可达模块数远低于 1024。

在以 1024 模块为目标优化查找前，应先决定：

- 64 是 bootstrap driver 的临时边界，还是语言工具链的正式限制；
- 提升上限后，source bytes、diagnostics 和递归深度限制如何配套；
- 多实现单元是否计入同一个 module 但占多个 source-unit slots。

## 十、未来二进制模块接口

`m0` 的 `.lmi` 使用一个覆盖完整 payload 的 FNV-1a 指纹，payload 记录参数名、声明
顺序、私有接口声明和直接依赖的完整指纹。该指纹还进入所有 Luna 模块符号名。
因此参数改名、私有声明变化或无关 export 增加都可能造成依赖级联失效。

这项批评对归档的 `m0` 实现成立。如果未来恢复二进制模块接口，不应直接复活旧
格式，而应至少区分：

| Identity | 用途 |
| --- | --- |
| content hash | 文件完整性、缓存和精确复现 |
| public API hash | 判断 importer 的语义契约是否变化 |
| internal interface hash | 编译本模块实现时验证私有接口 |
| per-symbol ABI identity | 防止旧调用方对象与不兼容实现静默链接 |

public API 的规范化编码必须覆盖当前 Luna 1 的完整接口，而不只是旧格式支持的
struct/union/enum/function：还要考虑 constants、type aliases、function pointers、
variadic、layout attributes、flexible members、anonymous members、bitfields 和所有会
改变调用方语义的属性。当前 `const fn` 是 implementation-private；如果将来允许跨
模块导出，二进制接口还必须携带其函数体或等价的编译期表示。

顶层声明顺序和参数名不属于契约；结构字段顺序、enum member 顺序和值、参数类型
顺序以及布局属性属于契约。没有隐式 re-export 时，依赖身份也不应无条件污染一个
模块全部既有符号的 ABI identity。

## 十一、建议实施顺序

### 已完成：当前合同、正确性和测试

1. 明确 compiler 不加载模块、root 选择和完整 compilation-set 合同；
2. 明确 `.lh/.la` import 合并规则及完整 module/import grammar；
3. 在 import 收集完成时诊断 qualifier 冲突；
4. 修复重复和空 selective list，冻结 trailing comma；
5. `validate_imported_names()` 跳过全部 non-selective imports；
6. missing-definition 检查所有提供了 implementation 的模块；
7. 测试 harness 支持显式有序 source set，并覆盖模块错误矩阵。
8. 支持一个接口、多个共享私有作用域的 implementation units。

### 近期：保持简单

1. 明确 64 source units 与 1024 modules 的限制关系；
2. 分两次提升完成 compiler scanner 与构建脚本切换；
3. 测量现有查找耗时，在没有证据前不增加索引结构。

### 暂缓：内部重构和语言扩展

1. NameId、ModuleSymbolIndex 和 UnitImportScope；
2. DependencyEdge/ImportBinding 分离及 AST 规范化；
3. opaque/incomplete aggregate；
4. selective import 的表面修改。

### 将来存在二进制分发需求时

重新设计已编译接口格式、公共/内部 identity 和 per-symbol ABI identity。当前源码
自举没有增量缓存或 `.lmi` 消费路径，不应先为不存在的失效链实现复杂哈希协议。

## 最终评价

当前 Luna 模块系统的用户侧规则是稳健的，主要债务集中在内部查找和作用域表示：

> ModuleGraph 只管理依赖，UnitImportScope 只管理名字绑定，ModuleSymbolIndex 只管理
> 声明查找，CallableBinding 只管理同名候选集合，AST 只提供声明语义和诊断来源。

保持 `::` 为模块限定符、保持 `.` 为字段和成员访问是正确方向。当前 callable
binding 已经以最小模型支持自由函数重载：候选身份属于 `Function`，名字集合属于
`Binding`，解析结果由独立 probe 产生；不需要引入 C++ 式 ADL 或跨模块集合合并。
继续保持一个接口、共享私有作用域的多个实现文件和源码依赖闭包模型，等测试或测量
证明需要时再扩展通用名字索引，风险最低。
