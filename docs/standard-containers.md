# Luna 标准容器设计

## 目标与范围

本批次为后续编译器 OOP 重构补齐 C++ 形状的常用容器：

- `luna.std.list`：双向链表；
- `luna.std.deque`：双端队列；
- `luna.std.queue`：以 deque 为底层的队列适配器；
- `luna.internal.tree`：map/set 可复用的有序红黑树实现；
- `luna.std.map`：唯一键有序映射；
- `luna.internal.pool`：list/tree 共享的稳定地址 slot pool 实现。

公开类型和方法遵循 C++ 标准库的小写命名。list 保持节点引用稳定和 O(1) 首尾操作；
deque 保持 O(1) 随机访问与摊销 O(1) 首尾操作；queue 只暴露 FIFO 契约；map 使用唯一键
和平衡树，不用排序数组冒充。C++ 的 list、deque、map 和 queue 契约分别以标准草案
`[list]`、`[deque]`、`[map]`和`[queue]`为设计依据。

## 当前生命周期边界

M5 已支持构造、析构、copy/move 和 RAII，但尚未公开在未初始化动态地址上执行原地构造
与显式销毁的`construct_at`/`destroy_at`原语。普通赋值不能替代非平凡对象生命周期的
开始，释放原始内存也不能替代析构。

因此本批次所有拥有元素的容器都在构造阶段执行：

```luna
assert(@is_trivially_relocatable(Value));
```

map/tree 同时约束 Key 和 Value。该限制保证节点写入、环形缓冲搬迁、erase 和整池释放都
只有可证明的字节语义。非平凡元素支持必须先独立落地`construct_at`、`destroy_at`、
逐元素失败回滚和对应 anchor，再解除该限制；本批次不保留静默字节复制资源对象的后门。

## 模块与依赖

```text
luna.std.memory
  └─ luna.std.buffer
       ├─ luna.std.vector
       ├─ luna.std.deque
       │    └─ luna.std.queue
       └─ luna.internal.pool
            ├─ luna.std.list
            └─ luna.internal.tree
                 └─ luna.std.map
```

公开容器与 typed pool/tree wrapper 是跨模块单态化的 interface-only 泛型实现。pool 将块
管理、位图和空闲表算法下沉到一个非泛型`slot_storage`实现单元，避免每种 Node 重复生成
同一算法。公开容器位于`library/include/luna/std/`；C++ 没有公开 tree 或 slot pool 容器，
因此两项实现依赖位于`library/include/luna/internal/`。`pool`是 list 与 tree 的真实共享
依赖，`tree`是 map 与未来 set 的真实有序索引边界；二者不是为了缩短文件产生的空壳模块。

## `slot_pool<Value>`

slot pool 每次从`luna.std.memory`申请一块包含固定数量 slot 的映射。非泛型
`slot_storage`用两个`byte_buffer`集中保存块记录和带 block/bit 身份的空闲 slot；泛型
`slot_pool<Value>`只负责`sizeof(Value)`、平凡性断言和 typed pointer 转换。它不构造或
销毁 Value；上层容器负责写入和解除节点关系。

不变量：

- 每块起点满足目标页对齐，`sizeof(Value)`保证块内所有 slot 保持 Value 对齐；
- 每个 slot 只处于 live 或 free 一种状态；
- `live_count + free_count == block_count * slots_per_block`；
- deallocate 拒绝池外、未对齐和重复释放的指针；
- move 后源 pool 回到唯一空状态；
- pool 析构释放本身创建的全部映射。

补块和 metadata reserve 在发布新块前完成。失败时不改变已有块、空闲表和 live 计数。

## `deque<Value>`

deque 使用 move-only RAII 环形缓冲：`byte_buffer`拥有连续映射，`head`与`element_count`
描述逻辑序列。扩容时按逻辑顺序复制到新缓冲并把 head 归零。

提供：`empty`、`size`、`capacity`、`front`、`back`、`get`、`push_front`、
`push_back`、`pop_front`、`pop_back`和`clear`。当前 Luna 没有类下标操作符和迭代器，
因此随机访问使用`get`。

复杂度：随机访问 O(1)，首尾 push 摊销 O(1)，首尾 pop O(1)，扩容 O(n)。连续环形实现
会在扩容时使引用失效，这是与 C++ 分段 deque 的明确差异；在 Luna 具备统一迭代器和
非平凡原地生命周期前，不增加分段 map 的实现复杂度。

## `list<Value>`

list 是双向节点链，节点由`slot_pool<list_node<Value>>`提供稳定地址。容器保存 first、
last 和元素计数，首尾插入/删除为 O(1)，clear 为 O(n)。除被删除元素外，插入与删除不
改变其他元素地址。

第一版提供容量、首尾访问和首尾修改。遍历接口等统一 iterator 基础设施成熟后补充；
不以裸公开节点或可伪造 cursor 破坏链表所有权边界。

## `ordered_tree<Key, Value>`与`map<Key, Value>`

ordered_tree 是唯一键红黑树。节点由 slot pool 分配，旋转只改链接，因此插入和删除不会
移动其他键值。树维护：

- 根为黑色；
- 红节点的子节点均为黑色；
- 从任一节点到空叶的黑高一致；
- parent/left/right 双向关系一致；
- 中序键满足严格弱序；
- 节点计数与可达节点数一致。

Key 的默认顺序直接使用`<`，等价关系为`!(a < b) && !(b < a)`。Luna 尚无 concepts 和
默认泛型参数，不能表达 C++ 的`Compare = less<Key>`表面；不支持`<`的 Key 会在实例化
使用比较时确定性诊断。未来 comparator 类型必须作为独立设计加入，不能用无类型
`*void`回调削弱类型安全。

tree 提供`find`、`contains`、`insert`、`insert_or_assign`、`erase`、`clear`和完整
`is_valid`。map 是窄 facade，复用同一操作并保持 C++ 命名。没有异常时，find 返回可空
指针，修改操作返回带`runtime::Error`的具体被动结果记录；不引入通用`result`类型。

## `queue<Value>`

queue 组合`deque<Value>`，只转发`empty`、`size`、`front`、`back`、`push`、`pop`和
`clear`。C++ queue 是任意满足 front/back/push_back/pop_front 的序列容器适配器；Luna
尚无默认泛型参数，因此第一版固定使用 C++ 默认的 deque，而不是暴露额外 Container
类型参数。

## 最新特性采用审查

- 泛型类：全部公开容器、节点和 typed slot pool wrapper；
- 类组合：queue→deque、map→tree、list/tree→slot pool、pool/deque→buffer；
- 访问控制：所有链接、根、块表和计数保持 private；
- 构造/析构与 RAII：建立唯一空状态并自动释放映射；
- move 特殊成员：所有拥有资源的容器显式 move-only；
- 重载：mutable/const 的 front、back、get、find；
- 操作符：当前没有比命名容器操作更自然且已支持的操作符；
- bound method：容器没有需要携带接收者的回调策略；
- friend：模块内部通过窄公开方法协作，不开放私有存储；
- 继承、虚函数、RTTI：不存在运行时替换关系，均不适用；
- 默认参数：当前接口没有能在不隐藏失败语义的默认值。

## 验证矩阵

- pool 多块增长、地址对齐、重复释放、池外指针、move 和析构；
- deque 环绕、两端增长/删除、扩容重排、mutable/const 访问和 move；
- list 空/单节点/多节点、两端操作、链接一致性、引用地址稳定和 move；
- tree LL/LR/RL/RR 插入旋转、重复键、叶/单子/双子删除、根删除、黑高与顺序验证；
- map insert、insert_or_assign、find、contains、erase、clear 和 move；
- queue FIFO、front/back、空队列错误和 move；
- 非平凡元素实例化确定性失败；
- 跨模块泛型组合、完整测试以及 stage-next/stage-fixed 全产物逐字节一致。

## 实现结果

最终快照在`caw`的 x86-64 WSL2 环境完成验证：

- `audit`确认 86 个模块、1 个统一驱动和 75 个驱动闭包对象一致；
- formatter 为 0 个待格式化文件、0 个 token 漂移；
- `stage-next`与`stage-fixed`的全部汇编、对象和`bin/luna`逐字节一致；
- 容器正例合并在一个`std_containers.la`中，三个非平凡元素诊断保持独立；
- 完整结果为 443 passed、0 failed、0 skipped；
- `slot_storage`对象`pool.lo`为 65,253 字节，泛型 wrapper 不重复生成块管理算法；
- 最终 anchor 为 4,556,630 字节，SHA-256 为
  `c2c36ff54455138416afdd2004ea2294c8cfbc68a6c375fc13e7a6987dabb500`；
- 三阶段固定点产物时间窗为 2,126.8 秒；
- expectation 测试串行耗时 83.24 秒，4 worker 并行耗时 49.44 秒，两者结果完全一致，
  当前加速约 1.68 倍。

容器实现同时暴露并修复了两个通用编译器缺口：递归泛型记录经指针访问成员前必须完成
类型布局；调用 probe/lowering 不能跨可能触发惰性泛型实例化的参数表达式持有
functions/parameters/type-fields 裸视图。修复均位于通用语义边界，没有容器名称特判。
