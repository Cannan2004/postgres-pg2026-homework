# PostgreSQL内核课程作业

作业入口： https://github.com/Cannan2004/postgres-pg2026-homework/tree/pg2026-homework/course

本目录包含5次课堂作业答案、链表搜索实验和PMC大作业。完整PG源码在仓库根目录，基于REL_16_STABLE，起点提交为`f3a9e4eeae24d1d58ad3d1ec63a5fc041029f247`。

## 文件说明

- `answers/`：6份完成版Word，按题号作答。
- `linked_list/`：不使用Hash的双向循环链表、功能测试及性能实验。
- `common/`：Linux perf计数模块，与PG内核中的实现保持一致。
- `sql/pmc_init.sql`：课堂格式的PMC事件表和初始化数据。
- `sql/demo.sql`：Hash构建观测与vage文件定位实验。
- `tests/`：原始实验输出、错误恢复测试及PMC单元测试。
- `run.sh`：从编译到运行的一键复现脚本。

## 编译和复现

适用环境：x86-64 Linux，gcc、make、bison、flex、Perl及zlib开发文件。示例在Ubuntu 22.04 WSL2、Intel Core i7-11800H上验证。所有安装和数据库数据都放在实验目录，不覆盖已有PG。

```bash
git clone --branch pg2026-homework https://github.com/Cannan2004/postgres-pg2026-homework.git
cd postgres-pg2026-homework
bash course/run.sh
```

脚本默认使用项目旁的`install`和`lab`目录，Unix socket端口55432，不监听TCP；结束后停止数据库。若lab中已有数据库，脚本拒绝重新初始化。可通过`PREFIX`、`LAB`、`JOBS`指定独立路径和并行编译数。无需修改系统perf权限就能尝试运行，权限不足或事件不支持时会明确输出N/A。

单独运行链表实验：

```bash
make -C course/linked_list
course/linked_list/link_bench 10000 10000 64
make -C course/linked_list sanitize
```

参数分别为节点数、查询次数、数据字节数。基准查询包含25%的未命中，其余为分散命中。基线与优化版使用同一链表布局以单独比较搜索逻辑。分配、查询生成与预热不在测量循环内。

## 链表实现与实测

保留InitLink、AppendNode、FindNode。数据以二进制方式比较，32～512字节，返回从头起第一个匹配项。按需分配4KiB块存放追加节点与数据，避免每个节点各做两次malloc。搜索比较长度与原始8字节前缀，再比较剩余字节；没有Hash、指纹、索引或跳过遍历。AppendNode失败保持原链表不变并设置errno。该实现为单线程，不支持并发修改。

| 指标 | memcmp基线 | 前缀比较优化 |
|---|---:|---:|
| 耗时(ms) | 184.226 | 181.362 |
| 指令数 | 1625611624 | 563211621 |
| 周期数 | 801774657 | 794294756 |
| L1D读访问 | 562640394 | 250182891 |
| L1D读未命中 | 96657109 | 96667403 |
| L1D写访问 | 62577689 | 95189 |
| L1D读命中率 | 82.821% | 61.361% |
| 访存次数(L1D读+写) | 625218083 | 250278080 |

这是一次实际运行，不是跨机器保证。计数器覆盖率均为100%。优化版减少了大量指令和命中的读访问，而缓存未命中数量基本未变，所以命中率下降但耗时略降。链表的指针依赖仍然是限制。访存次数是所选L1D事件口径，不等同于DRAM读写事务；不以LLC事件代替L1事件。

## PMC大作业

初始化表保留教师提供的`pcm_name`拼写与5列结构。`config2`在这份课堂表里是排除内核的标记，不是perf原始config2。实现统一排除内核和hypervisor，测量当前后端线程，不计其子进程。默认提供通用事件；原始事件编码依赖CPU型号，不盲用其他型号编码。

```sql
\i course/sql/pmc_init.sql
SET max_parallel_workers_per_gather=0;
SET enable_mergejoin=off;
SET enable_nestloop=off;
SET halo_watch_pmc_list='instructions,cycles,PERF_COUNT_HW_CACHE_L1D_RD,PERF_COUNT_HW_CACHE_L1D_MISS_RD';
SET halo_pmc_per_tuple=off; -- 整个构建循环
-- 执行带Hash Join的SQL
SET halo_pmc_per_tuple=on;  -- 每条记录的Hash计算与插入区间
-- 再执行带Hash Join的SQL
SET halo_watch_pmc_list=''; -- 关闭
```

参数为superuser级别。没有修改共享内存，不影响普通查询的默认行为。仪器位置为`MultiExecPrivateHash`，不统计parallel-aware Hash构建；复现实验关闭并行。

低开销方案：循环外打开并启用计数器，循环内使用mmap元数据页与RDPMC取得前后快照，累加差值。遵守seqlock，处理index、offset、pmc_width、时间换算和多路复用缩放。没有每条记录一次prctl启停。无RDPMC权限、非x86平台或元数据不适用时退回read。无效或未调度事件显示N/A，避免除零。MemoryContext清理回调关闭fd并解除映射，支持查询报错后的资源回收。

整段构建实测：instructions=12225166，cycles=3765202，L1D读=3973548，L1D读未命中=15815。关闭/整段/逐条/软件回退查询结果均4950。逐条模式有4950个有效样本。

**实测边界：** 本机WSL未暴露用户态RDPMC能力，实际记录为`rdpmc=0`，`read>0`。快速指令分支已实现，但本机不能验证其真实硬件性能；单元测试覆盖缩放、无效时间、无效事件、软件read回退、index=0的mmap元数据路径，后者不冒充RDPMC硬件测试。允许RDPMC的原生Linux环境才能验证快速路径的速度。多事件快照不是原子同时读取，短区间包含测量开销；低覆盖率的缩放值只能作估计。

## 验证

- PG完整编译和安装成功；`make check`：224项全部通过。
- 链表：32～512字节、跨块分配、首尾/重复/缺失数据、链接不变量、无效输入。
- AddressSanitizer与UndefinedBehaviorSanitizer通过。
- PMC：缩放、零运行时间、无效事件、软件计数器回退及元数据分支通过。
- SQL：开启观测不改变结果，支持重复运行、未知事件报错后恢复、不可用事件N/A。

更多原始输出见`tests/`。编译选项为-O2，没有使用-O3。原始课件与群聊不上传到仓库。

## 技术参考

- PostgreSQL 16源码及文件布局文档：https://www.postgresql.org/docs/16/storage-file-layout.html
- Linux perf ABI：`include/uapi/linux/perf_event.h`，https://github.com/torvalds/linux/blob/master/include/uapi/linux/perf_event.h
- Linux perf元数据页说明：https://www.kernel.org/doc/html/latest/userspace-api/perf_ring_buffer.html

本作业尚未发送给老师，提交链接由本人审查后使用。
