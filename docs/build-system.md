# Self-host 构建执行器

## 目标

Luna 的每个 library object 都由一个完整源码闭包独立编译，模块之间不消费彼此的宿主
object；import graph 只决定接口闭包和最终稳定链接顺序。因此旧`build_stage`按拓扑序串行
执行 65 个 compile/assemble 对没有正确性必要，却让单阶段构建约为 85 秒，三阶段 verify
约为 256 秒。

当前构建执行器位于`tools/build.py`，`tools/selfhost.py`只负责从唯一模块注册表构造
`StagePlan`。默认使用四个 worker，同时提供有界、内容寻址且自校验的 artifact cache。

## 并行执行模型

一个 library target 在同一 worker 内顺序执行：

```text
ordered source/interface closure -> compile -> assembly -> assemble -> object
```

所有 library target 之间并行。全部 object 完成后，driver 按确定的 interface closure 编译、
汇编，再按 import graph 导出的稳定 object 顺序链接。并行完成顺序只影响进度日志，不影响
命令参数、文件名、链接顺序或产物字节。

`--jobs N`控制 library worker 数，默认 4；非正值在启动任何工具前失败。Python worker 只
调度独立进程，不在线程内执行编译器逻辑，因此不受 GIL 限制。

## 缓存信任模型

compile、assemble 和 link 分别缓存，不能用源码时间戳代替内容：

- compile key：cache format、实际`luna`二进制 SHA-256、runner 身份、编译模式，以及有序
  unit 的逻辑路径和内容 SHA-256；
- assemble key：cache format、实际工具 SHA-256、runner 身份和 assembly 内容 SHA-256；
- link key：cache format、实际工具 SHA-256、runner 身份，以及有序 object 名称和内容
  SHA-256。

runner 可执行文件存在时，其解析后路径和自身 SHA-256 也进入 key。源码工作区移动不会
失效缓存，因为注册源码使用相对仓库根的逻辑路径。

每个 cache entry 的 manifest 记录 input fingerprint、artifact SHA-256、长度和 Unix mode。
命中前重新验证长度与内容；截断、篡改、半写入或旧格式一律视为 miss。artifact 和
manifest 都通过同目录临时文件加`os.replace`发布，manifest 最后提交；崩溃不会把未完成
产物标成有效命中。

缓存按 stage scope、action 和逻辑 target 直接映射，只保留每个位置的最新 entry。构建前
删除已经不在当前 plan 中的 target，缓存空间不会随源码提交数或内容 hash 无限增长。

## Fixed-point 边界

缓存 scope 严格分为`build`、`transition`、`next`和`fixed`。更重要的是，实际执行每个
stage 的工具二进制 hash 都属于 key：

- anchor 产物不能冒充 transition 产物；
- transition 产物不能冒充 stage-next 产物；
- stage-next 产物不能冒充 stage-fixed 产物。

普通`verify`允许从完全匹配且自校验的缓存恢复三阶段，再执行 next/fixed 全产物比较，适合
未变化工作区的快速复验。`verify --fresh`忽略全部 cache read，真实执行每个 compile、
assemble 和 link，同时刷新缓存；它是提交、发布和 anchor promotion 的最终可信门禁。

`--fresh`不会删除缓存，因此不会制造一次性大目录；它只保证当前运行没有缓存命中。

## 命令

```sh
python3 tools/selfhost.py build                  # 4 workers，允许缓存
python3 tools/selfhost.py build --fresh          # 单阶段真实重建
python3 tools/selfhost.py build --jobs 1         # 串行诊断/基准
python3 tools/selfhost.py verify                 # 内容匹配时快速复验
python3 tools/selfhost.py verify --fresh         # 最终可信 fixed-point
python3 tools/selfhost.py verify --fresh --jobs 8
python3 tools/selfhost.py build --cache /path/to/cache
```

默认 cache 是 output root 下的`cache/`；`--cache`可以把它放到普通磁盘，避免受 tmpfs 容量
限制。stage output 每次仍重建目录，cache entry 恢复使用复制而非硬链接，后续操作不能反向
修改缓存。

## 验证结果

远程`caw`x86-64 WSL2、Python 3.13.9、默认四 worker：

| 场景 | 墙钟时间 | 缓存 |
| --- | ---: | --- |
| 单阶段串行 fresh | 85.21 秒 | 0/66 compile，0/66 assemble |
| 单阶段 4-worker fresh | 40.62 秒 | 0/66 compile，0/66 assemble |
| 单阶段完全命中 | 内部 0.11 秒；进程 0.74 秒 | 66/66、66/66、1/1 link |
| 三阶段 verify fresh | 124.55 秒 | 所有 action 强制 miss |
| 三阶段 verify 完全命中 | 2.02 秒 | 每阶段 66/66、66/66、1/1 |

串行/并行、冷/热、单源码失效和损坏恢复的全部产物逐字节一致。向`ascii.la`追加临时注释
只造成 1 个 compile miss；其 assembly 未变化，因此 assemble 与 link 继续命中。人为把
缓存 object 截断为 1 字节后，size/SHA 校验只重跑对应 assemble。缓存稳定为 133 个
manifest、约 49 MiB。向同一 implementation 临时加入真实函数后，只有该 module 的
compile/assemble 和最终 link 失效；恢复源码后重新生成的全套产物与基线逐字节一致。
最终`verify --fresh`全产物一致，完整测试 443/443 通过，用时 20.22 秒。
