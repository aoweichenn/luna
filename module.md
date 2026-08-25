# Luna 当前模块系统审查

## 文档范围

本文审查当前 `main` 上的纯 Luna 自举实现，而不是 `m0` 分支中的 C23
重建种子。

审查基线为 `9de5a3e`（`refactor: separate module interfaces and
implementations`）。后续实现变化应同步更新本文中的源码数量和优先级判断。

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

- 一个模块至多有一个接口单元和一个实现单元；
- 普通 import 只绑定模块限定符，不自动平铺名字；
- selective import 才显式引入未限定名字；
- import 是直接、非传递的；
- module graph 必须可达且无环；
- 接口声明与实现定义必须精确匹配；
- `::` 只负责模块限定，`.` 负责字段和枚举成员访问。

当前真正需要处理的是：

1. qualifier 冲突只在使用限定名时才报告；
2. 名字查找和 import 验证反复扫描全局 symbol 表；
3. `Import` 同时承担依赖边和名字绑定；
4. module graph 仍依赖 AST node/token 身份；
5. Python 构建脚本维护了一份正则版 module scanner；
6. 一个模块只能有一个实现文件；
7. 缺少 opaque/incomplete aggregate；
8. 驱动的 64 source-unit 上限比语义层的 1024 module 上限更早生效。

`.lmi` 指纹拆分不是当前第一优先级。只有重新引入二进制模块接口、增量缓存或
已编译包分发时，才应重新设计 public API identity、内部接口 identity 和单符号
ABI identity。

## 一、当前实现模型

### 构建边界

`tools/selfhost.py` 的 `LIBRARIES` 是模块名与接口/实现路径的唯一注册表。依赖和
链接闭包来自源码 import：

```text
LIBRARIES registry
        |
Python import scan
        |
root implementation + root interface + dependency interfaces
        |
lunac
        |
assembly -> luna-as -> object -> luna-link
```

每个 library 单独编译。依赖模块只提供 `.lh`，其定义由另外生成的模块对象在
链接时提供。当前没有已编译接口文件或 metadata-only dependency。

### 语义模型

当前核心记录是：

```text
Module {
    interface_unit
    implementation_unit
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
```

它不包含模块指纹，也不包含函数签名。由于当前语言明确禁止重载，模块名和函数名
足以保证同一构建内的唯一性；但链接器不会发现调用方与实现方来自不兼容的旧接口。
当前 self-host 流程通过从同一源码图全量重建所有对象来避免这种混用。

## 二、立即存在的正确性问题

### qualifier 冲突诊断过晚

下面两个 import 都绑定 `text::`：

```luna
import foo.text;
import bar.text;
```

当前实现只在解析 `text::name` 时扫描 imports 并发现歧义。如果程序没有使用
`text::`，冲突不会被报告。

import 声明本身已经建立名字绑定，因此应在 import 收集完成后立即检查 qualifier。
诊断可以继续使用现有 `ambiguous_module_qualifier`，同时把前一个 import 作为
related span。应增加一个“冲突 qualifier 从未使用”的负例。

### selective import 边角

当前还有三个小问题：

- `import foo::{read, read};` 没有检测同一列表内的重复名字；
- `import foo::{};` 最终报告 `unknown_selective_import`，错误分类不准确；
- parser 接受 `import foo as f::{x};` 的组合，再由 semantic 报
  `invalid_selective_import`。

第三项不是正确性错误：semantic 诊断比 parser 的通用 expected-token 诊断更明确。
如果保留这种分层，只需把语法文档写清楚。前两项应补精确验证和负例。

## 三、名字查找和 import 验证

当前主要查找路径都是线性的：

```text
find_module_by_name       O(M)
find_local_symbol         O(S)
find_module_symbol        O(S)
find_qualifier_target     O(I)
find_imported_symbol      O(I * S)
```

其中 `M` 是 module 数，`I` 是当前模块 import 数，`S` 是 compilation 中的全局
symbol 数。每次字符串比较还要重新访问两个 unit 的 source bytes。

`validate_imported_names()` 更重：

```text
每对 import
    x 全部左侧 symbols
    x 全部右侧 symbols

每个本地 symbol
    x 每个 import
    x 全部 symbols
```

`import_binds_name()` 还会扫描 selective name 列表，因此严格最坏情况在原有
`O(I^2 * S^2 + I * S^2)` 外还包含 selective-list 长度因子。

### 先做低风险快速跳过

当前 `library/`、`compiler/` 和 `drivers/` 中共有 413 条 import，其中 371 条是
alias import，没有一条 selective import。普通和 alias import 都不建立 flat
binding，却仍会进入大量最终不可能命中的 symbol 循环。

在完整索引重构前，应先让 `validate_imported_names()` 只处理
`selective_node != no_id` 的 import。这个改动不改变语言行为，却能消除当前生产
源码中的绝大部分 import-name 验证成本。

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

当前 Luna 使用统一模块名字空间，并明确禁止函数重载，因此不需要预埋
`NamespaceKind` 或 `FunctionSet`。enum member 属于其 enum 的独立成员空间，不进入
模块顶层索引。

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
- 将来加入 compiler module scanner、多实现单元或二进制接口时难以复用图层。

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

## 六、一个模块只有一个实现文件

当前 `Module` 固定保存一个 `implementation_unit`，第二个同名 implementation
直接报告 `duplicate_implementation`。`tools/selfhost.py` 的注册表同样只保存一个
implementation path。

支持一个接口、多个实现单元是合理的扩展：

```luna
module luna.std.fs;
```

可以出现在多个 `.la` 中，而公共接口仍只有一个 `.lh`。但这不是只把一个字段改成
数组：

- 类型、常量、const fn、普通函数收集都要遍历 implementation unit 列表；
- implementation import 必须按 unit 隔离；
- 同模块私有名字仍应共享模块名字空间；
- 同一个接口函数在所有实现文件中合计必须恰好定义一次；
- 构建注册表、source-unit 上限和确定性排序都要同步修改。

因此，DependencyEdge/ImportBinding 分离和 UnitImportScope 是多实现单元的前置
工作。

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
struct/union/enum/function：还要考虑 constants、type aliases、const fn、function
pointers、variadic、layout attributes、flexible members、anonymous members、bitfields
和所有会改变调用方语义的属性。

顶层声明顺序和参数名不属于契约；结构字段顺序、enum member 顺序和值、参数类型
顺序以及布局属性属于契约。没有隐式 re-export 时，依赖身份也不应无条件污染一个
模块全部既有符号的 ABI identity。

## 十一、建议实施顺序

### 第一阶段：当前正确性和低风险性能

1. 在 import 收集完成时诊断 qualifier 冲突；
2. `validate_imported_names()` 跳过全部 non-selective imports；
3. 修复重复和空 selective list，并增加负例；
4. 明确 64 source units 与 1024 modules 的限制关系；
5. 用现有 self-host 构建测量名字查找和 import 验证耗时。

### 第二阶段：规范化模块层

1. 引入 NameId/ModuleNameId；
2. 建立 ModuleSymbolIndex 和 UnitImportScope；
3. 拆分 DependencyEdge 与 ImportBinding；
4. 让 module graph 不再保存 AST child/token identity；
5. 分两次提升完成 compiler scanner 与构建脚本切换。

### 第三阶段：语言扩展

1. 决定是否支持一个接口、多个实现单元；
2. 设计 opaque/incomplete aggregate；
3. 单独决定 selective import 是保留、修改还是删除。

### 将来存在二进制分发需求时

重新设计已编译接口格式、公共/内部 identity 和 per-symbol ABI identity。当前源码
自举没有增量缓存或 `.lmi` 消费路径，不应先为不存在的失效链实现复杂哈希协议。

## 最终评价

当前 Luna 模块系统的用户侧规则是稳健的，主要债务集中在内部查找和作用域表示：

> ModuleGraph 只管理依赖，UnitImportScope 只管理名字绑定，ModuleSymbolIndex 只管理
> 声明查找，AST 只提供声明语义和诊断来源。

保持 `::` 为模块限定符、保持 `.` 为字段和成员访问是正确方向。当前语言明确禁止
函数重载，因此也不应为了假设中的 C++ 式方法重载提前复杂化 Binding。先解决真实
的 qualifier 诊断、无效扫描、双重 scanner 和 source-unit 上限，再扩展多实现单元
或二进制模块接口，风险最低。
