# x86-64 静态 Linker 设计

## 目标与边界

`luna.compiler.x86.linker`将一组已验证的 `object::Object` 合并为无 libc、无动态
装载器的 x86-64 Linux `ET_EXEC`。本批次只重构当前静态链接契约：固定图像基址、最多三个
`PT_LOAD`、四个逻辑 region 以及现有五种 relocation 的输出字节不变。

动态链接不是这个类的隐藏模式。PIC/PIE、`ET_DYN`、GOT/PLT、动态段和 Luna 动态装载器
属于后续独立里程碑；当真实的第二种链接策略存在时，再抽取共享边界。

## 所有权与阶段不变量

私有 `StaticLinker` 类以非拥有视图引用输入 objects，并组合拥有：

- `vector<Placement>`：每个输入在四个合并 region 内的偏移；
- `map<GlobalName, Global>`：名字有序、唯一且可稳定查找的全局符号表；
- 三个 `byte_buffer` region 和一个最终 executable buffer；
- BSS 大小、各 region 对齐、虚拟地址、入口地址和 first-error/input-index 状态。

状态只按以下顺序推进：

```text
validate -> placements -> globals -> regions -> image layout
         -> relocations -> entry -> ELF emission -> take_result
```

构造函数建立唯一有效空状态。`run()`在首个错误处停止；粘性错误保留最初的
`runtime::Error` 和输入索引。`take_result()`在成功时零拷贝分离 executable，失败时返回有效
空 buffer；组合的 RAII 成员负责所有未完成路径的销毁。

## 符号、布局与分派

`GlobalName` 只保存指向输入 object 名字存储的非拥有 `string_view`，其自然字典序
`operator <` 专用于 `map`。输入 objects 的生命期覆盖整个 `StaticLinker`，因此 map key 不会
悬空。当前跨模块泛型单态化要求 key 类型身份和排序 operator 可见，因此仅导出
`GlobalName`；它不导出链接状态。插入结果直接表达重复全局定义，不再线性扫描裸字节记录。

section 到 region、虚拟地址以及 relocation 算法的封闭分派统一使用 `switch`。范围、
溢出、对齐和资源上限使用短条件、命名谓词与早返回。有界遍历统一使用 `for`。
布局算术通过 checked addition/alignment 建立，不允许中间表达式先溢出。

ELF writer 是 `StaticLinker` 的输出阶段，不单独创建无独立生命期的转发类。它通过同一
first-error 状态顺序写入 header、program headers 和 region，不用长 `if (!write())` 链。

## 实现单元

所有文件实现同一 `luna.compiler.x86.linker` 模块：

| 文件 | 职责 |
| --- | --- |
| `symbols.la` | `GlobalName`、全局收集、符号地址与入口解析 |
| `layout.la` | placement 初始化、region 合并、对齐和图像地址 |
| `relocation.la` | relocation 范围验证、算术和写回 |
| `writer.la` | `ET_EXEC` header、`PT_LOAD` 记录和确定性输出 |
| `facade.la` | `StaticLinker` 生命周期、阶段编排和公开 `link` |

这些是同模块 method family，不形成 `linker.symbols` 或 `linker.writer` 子模块。

## 最新 Luna 特性审查

| 特性 | 决定 |
| --- | --- |
| class/access/init/deinit | 用于拥有链接会话和 first-error 不变量的 `StaticLinker` |
| generics | `vector<Placement>` 和 `map<GlobalName, Global>` 替代固定栈数组与裸字节表 |
| composition/RAII | 直接组合 typed containers 与 `byte_buffer`，失败路径无手工清理树 |
| move | 最终 buffer 只在 `take_result()` 边界分离；operation object 不移动 |
| operator | 仅 `GlobalName` 提供 map 所需的自然字典序 `<` |
| overload/default | 当前公开链接契约无同一语义族变体，不增加装饰性接口 |
| bound method/friend | 阶段方法直接绑定会话；无策略回调或需扩大访问面的协作者 |
| inheritance/virtual/RTTI | 当前只有一种静态链接行为，封闭 enum + `switch` 比运行期层次更准确 |

## 验证契约

除 443 项完整测试和字节固定点外，集成链接探针覆盖：确定性输出、重复全局符号、
缺失/非函数入口、未解析符号、五种 relocation、越界和溢出拒绝，以及输出中无
`PT_INTERP`/`PT_DYNAMIC`。重构后的基准 `dispatch` object 和 executable 必须与旧 anchor 逐字节相同。

## 验证结果

最终在隔离的远程 `caw` x86-64 WSL2 工作区完成：

- audit 为 75 modules、65 driver-closure objects，formatter 为 0 reflow/0 token drift；
- Linker/relocation/ELF 针对性探针 6/6 通过；
- 完整测试 443/443 通过，最终用时 5.16 秒；
- `verify --fresh` 用时 94.40 秒，stage-next/stage-fixed 全部 artifact 逐字节相同；
- 旧/new `dispatch` object 均为 429 字节，executable 均为 8,200 字节，两者哈希分别为
  `c5b3ebe6c59c33489e2088d41626437eb89237f5d2b3b10a16688471108ca62d` 和
  `8f7b2e1fbda758462d5f709993c2dc53fb477db491cccecdb71a3e479816d592`；
- 对同一组 65 个 library object 及 driver object 重链三次，旧 anchor 中位数为 21.754 秒，
  新 `StaticLinker` 为 0.488 秒；六个输出均为 4,794,193 字节且 SHA-256 同为
  `f11c8b0325edd80374531f85a9ed65d31df437bb604958eea192236438a9ad14`。

全局查找从逐记录线性扫描收敛为红黑树查找，同时保留所有校验、布局和错误契约；
性能收益没有依赖缓存、减少输入或改变输出。最终 `x86_64_linker.lo` 为 382,769 字节。
