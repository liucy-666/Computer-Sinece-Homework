# 动态内存分配器实验报告

> **学号**: 202402720024
> **实验**: EXP 10 - 动态内存分配器 (malloc/free/realloc)
> **最终评分**: **86/100** (效率 46/60 + 性能 40/40)

---

## 一、实验原理

### 1.1 什么是动态内存分配器？

动态内存分配器（Dynamic Memory Allocator）是操作系统和运行时库的核心组件，它管理着进程的**堆（Heap）**空间。堆是一个线性的字节数组，分配器负责：

- **malloc(size)**: 从堆的空闲空间中分配 `size` 字节，返回指向该空间（payload）的指针
- **free(ptr)**: 释放 `ptr` 指向的已分配空间，使其可被后续分配重用
- **realloc(ptr, size)**: 调整已分配块的大小，可能涉及移动数据

### 1.2 核心概念

#### 边界标记法 (Boundary Tags)

每个内存块都有 header 和 footer，记录块的大小和分配状态。Header 和 footer 互为镜像，使得从任意块出发都可以找到相邻块。

```
已分配块:                          空闲块:
+------------------+              +------------------+
| header (size|1)  |              | header (size|0)  |
+------------------+              +------------------+
| payload          |              | next ptr (8B)    |
| (user data)      |              +------------------+
+------------------+              | prev ptr (8B)    |
(无footer，节省空间)               +------------------+
                                  | ... (unused)     |
                                  +------------------+
                                  | footer (size|0)  |
                                  +------------------+
```

**关键优化**: 已分配块不存储 footer（节省 8 字节/块）。通过 Header 的 bit 1（`prev_alloc` 标志）来判断前一个块是否空闲：
- `prev_alloc = 1`: 前一个块已分配，无 footer，不能向后合并
- `prev_alloc = 0`: 前一个块空闲，有 footer（位于 `bp - 16`），可以向后合并

#### 隔离空闲链表 (Segregated Free Lists)

将空闲块按大小分为 12 个类别，每个类别维护一个显式双向链表：

| 类别 | 大小范围   | 类别 | 大小范围      |
|------|-----------|------|--------------|
| 0    | 32-64     | 6    | 2049-4096    |
| 1    | 65-128    | 7    | 4097-8192    |
| 2    | 129-256   | 8    | 8193-16384   |
| 3    | 257-512   | 9    | 16385-32768  |
| 4    | 513-1024  | 10   | 32769-65536  |
| 5    | 1025-2048 | 11   | > 65536      |

**优势**: 分配时只需在对应类别及更高级别搜索，大幅减少搜索时间。

### 1.3 算法流程

```
mm_malloc(size):
  1. 计算所需块大小 asize = ALIGN(size + 16)
  2. 在隔离链表中 best-fit 查找合适块
  3. 若找到 → place(bp, asize), 可能分割
  4. 若未找到 → extend_heap() → place(bp, asize)
  
mm_free(ptr):
  1. 标记块为空闲 (header + footer)
  2. 更新相邻块的 prev_alloc 标志
  3. coalesce() 与相邻空闲块合并
  4. insert_free() 插入空闲链表 (LIFO)

mm_realloc(ptr, size):
  1. 新大小 ≤ 旧大小 → 原地缩减（可能分割）
  2. 右侧空闲 → 向前合并扩展
  3. 左侧空闲 → 向后合并扩展（memmove 搬移数据）
  4. 两侧空闲 → 双向合并扩展
  5. 以上都不行 → malloc + memcpy + free
```

---

## 二、代码修改说明

本实验只修改了 `mm.c` 文件，完全重写了原有的简单实现。

### 2.1 原实现的问题

原实现 (`mm-naive.c`) 是一个极简实现：
- `mm_malloc`: 只调 `mem_sbrk()` 增加 brk 指针，**从不回收空间**
- `mm_free`: **空函数**，什么都不做
- `mm_realloc`: 总是分配新块 + 拷贝 + 释放旧块

这种实现会导致堆空间迅速耗尽，无法通过大多数测试。

### 2.2 新实现的关键代码

#### (1) 块结构和宏定义

```c
#define WSIZE       8               /* 字大小（字节） */
#define DSIZE       16              /* 双字大小 */
#define CHUNKSIZE   (1 << 12)       /* 堆扩展单位：4096字节 */
#define MIN_BLOCK   32              /* 最小空闲块：header+next+prev+footer */

/* Header 中的标志位 */
#define PACK(size, alloc)  ((size) | (alloc))
#define GET_SIZE(p)        (GET(p) & ~0x7)      /* 低3位为标志 */
#define GET_ALLOC(p)       (GET(p) & 0x1)       /* bit 0: 当前块 */
#define GET_PREV_ALLOC(p)  (GET(p) & 0x2)       /* bit 1: 前一个块 */

/* 块指针计算 */
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* 空闲链表指针 */
#define SET_NEXT(bp, val)  (*(char **)(bp) = (val))
#define SET_PREV(bp, val)  (*(char **)((char *)(bp) + WSIZE) = (val))
```

#### (2) 堆初始化 (`mm_init`)

```c
int mm_init(void) {
    // 初始化12个隔离链表为空
    for (i = 0; i < LIST_COUNT; i++)
        free_lists[i] = NULL;

    // 创建初始堆：[PAD 8B][PRO_HDR][PRO_FTR][EPI_HDR] = 32B
    heap_listp = mem_sbrk(4 * WSIZE);
    PUT(heap_listp, 0);                                 // 对齐填充
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1) | 0x2);     // Prologue Header
    PUT(heap_listp + DSIZE, PACK(DSIZE, 1));            // Prologue Footer
    PUT(heap_listp + WSIZE + DSIZE, PACK(0, 1) | 0x2); // Epilogue Header
    heap_listp += DSIZE;

    // 扩展初始空闲空间（4096B）
    extend_heap(CHUNKSIZE / WSIZE);
    return 0;
}
```

**设计要点**:
- Prologue 和 Epilogue 是永久的"哨兵"块，始终标记为已分配
- 它们消除了边界检查：合并时无需判断是否到达堆的起点/终点
- Prologue 的 `prev_alloc=1` 防止第一个块向后合并越界

#### (3) 立即合并 (`coalesce`)

```c
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {           // Case 1: 前后都分配
        // 只更新下一个块的prev_alloc标志
        return bp;
    } else if (prev_alloc && !next_alloc) {   // Case 2: 向前合并
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_free(NEXT_BLKP(bp));           // 从链表移除被合并的块
        PUT(HDRP(bp), PACK(size, 0) | prev_flag);
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {   // Case 3: 向后合并
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_free(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);                   // bp 移动到合并后块的开头
        PUT(HDRP(bp), PACK(size, 0) | prev_flag);
        PUT(FTRP(bp), PACK(size, 0));
    } else {                                   // Case 4: 双向合并
        size += GET_SIZE(HDRP(PREV_BLKP(bp)))
              + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_free(PREV_BLKP(bp));
        remove_free(NEXT_BLKP(bp));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0) | prev_flag);
        PUT(FTRP(bp), PACK(size, 0));
    }
    return bp;
}
```

#### (4) Best-Fit 搜索 + 块分割

```c
static void *find_fit(size_t asize) {
    // 从目标类别开始，向上搜索最佳适配块
    for (i = get_list_index(asize); i < LIST_COUNT; i++) {
        for (bp = free_lists[i]; bp != NULL; bp = GET_NEXT(bp)) {
            if (bp_size >= asize && bp_size < best_size) {
                best = bp;
                best_size = bp_size;
                if (bp_size == asize) return best; // 精确匹配则立即返回
            }
        }
        if (best != NULL) return best;  // 当前类别找到最佳，不再向上搜索
    }
    return NULL;
}

static void place(void *bp, size_t asize) {
    remove_free(bp);
    if (total_size - asize >= MIN_BLOCK) {
        // 分割：前部分分配，后部分保持空闲
        PUT(HDRP(bp), PACK(asize, 1) | prev_flag);
        void *remain_bp = NEXT_BLKP(bp);
        PUT(HDRP(remain_bp), PACK(remainder, 0) | 0x2); // prev_alloc=1
        PUT(FTRP(remain_bp), PACK(remainder, 0));
        insert_free(remain_bp);
    } else {
        // 不分割：整个块分配
        PUT(HDRP(bp), PACK(total_size, 1) | prev_flag);
    }
}
```

#### (5) Realloc 的多级优化

```c
void *mm_realloc(void *ptr, size_t size) {
    // Case 1: 缩小 → 原地缩减，可能分割
    // Case 2a: 右侧空闲且足够 → 向前合并扩展（指针不变！）
    // Case 2b: 左侧空闲且足够 → 向后合并扩展（需 memmove 搬数据）
    // Case 2c: 两侧都空闲 → 双向合并扩展
    // Case 3: 无法原地扩展 → mm_malloc + memcpy + mm_free
}
```

这是提升 realloc 利用率的关键：优先保持指针不变（Case 2a），其次用 memmove 原地扩展（Case 2b/2c），最后才走分配-拷贝-释放。

---

## 三、测试结果

运行命令: `./malloc -t traces`

测试了 26 个 trace 文件（13 个 balanced + 13 个 regular），全部通过正确性验证：

| 指标 | 数值 |
|------|------|
| 总 trace 数 | 26 |
| 全部正确 | ✅ 26/26 |
| 平均空间利用率 | 77% |
| 平均吞吐量 | 108,863 Kops/s |
| **最终评分** | **86/100** |

### 各 trace 详细结果

| Trace | 正确性 | 利用率 |
|-------|--------|--------|
| amptjp-bal | ✅ | 99% |
| cccp-bal | ✅ | 99% |
| cp-decl-bal | ✅ | 99% |
| expr-bal | ✅ | 99% |
| random-bal | ✅ | 96% |
| random2-bal | ✅ | 95% |
| short2-bal | ✅ | 89% |
| coalescing-bal | ✅ | 66% |
| short1-bal | ✅ | 66% |
| binary-bal | ✅ | 54% |
| binary2-bal | ✅ | 47% |
| realloc-bal | ✅ | 44% |
| realloc2-bal | ✅ | 45% |

性能分数 40/40 已满分（超过 AVG_LIBC_THRUPUT 基准），效率分数 46/60。

### 关于 Trace 中非法操作的处理

**问题**：并非所有 trace 文件都是"干净"的。Readme.txt 中明确提到：

> 有些trace内部包含了错误的操作，是跑不通的。比如：
> 1. 试图realloc一个不存在的指针
> 2. 试图free一个不存在的指针

**关键认识**：这些非法操作的处理**不在 `mm.c` 中，而是在测试驱动程序 `mdriver.c` 的 trace 读取阶段就已经自动修正/过滤了**。我们的分配器代码不会收到任何非法请求。

具体来说，`mdriver.c` 在读 trace 文件时做了三重过滤：

**过滤 1 — realloc 不存在的指针 → 自动转为 alloc**

```c
// mdriver.c read_trace() 函数中
case 'r':
    if (trace->blocks[index] == (char*)1) {
        // 该 index 之前出现过 alloc → 正常 realloc
        trace->ops[op_index].type = REALLOC;
    } else {
        // 变通方案：该 index 没有被 alloc 过，改成普通分配
        printf("尝试realloc一个未初始化的指针(%d)，修改为alloc\n", index);
        trace->ops[op_index].type = ALLOC;
    }
```

原理：驱动在逐行读取 trace 时，遇到 `a`（alloc）就把 `trace->blocks[index]` 设为 `(char*)1` 作为标记位。后续遇到 `r`（realloc）时检查该标记——若标记存在说明之前 alloc 过，正常执行 realloc；若不存在说明是非法操作，**直接改为 ALLOC**，从源头消除了非法 realloc。

**过滤 2 — free 不存在的指针（free -1）→ 标记为跳过**

```c
case 'f':
    if (index == -1) {
        // free(-1) 是非法操作，标记为 ERROR_BUG
        printf("free一个-1指针，跳过\n");
        trace->ops[op_index].type = ERROR_BUG;
    } else {
        trace->ops[op_index].type = FREE;
    }
```

非法 free（`index == -1`）被标记为 `ERROR_BUG` 类型。

**过滤 3 — 执行阶段的静默跳过**

```c
// eval_mm_valid() / eval_mm_util() / eval_mm_speed() 中均有
case ERROR_BUG:
    break;    // 什么都不做，直接跳过该操作
```

被标记为 `ERROR_BUG` 的操作在真正执行时被 `break` 跳过，根本不会调用我们的 `mm_free()` 或 `mm_realloc()`。

**数据流总结**：

```
 trace 文件               mdriver 读取阶段            mdriver 执行阶段         我们的 mm.c
┌──────────────┐        ┌─────────────────┐        ┌──────────────────┐     ┌──────────────┐
│ r 5 100      │───────▶│ 检查 index=5    │───────▶│                  │────▶│              │
│ (非法realloc)│        │ 之前alloc过?    │        │ ALLOC 操作        │     │ mm_malloc()  │
│              │        │ 否→改为ALLOC   │        │                  │     │              │
│              │        │                │        │                  │     │              │
│ f -1         │───────▶│ index==-1?     │───────▶│ ERROR_BUG        │────▶│ (不调用)     │
│ (非法free)   │        │ 是→标记ERROR   │        │ break 跳过       │     │              │
└──────────────┘        └─────────────────┘        └──────────────────┘     └──────────────┘
```

**结论**：26 个 trace 全部通过不是因为所有 trace 都合法，而是因为 `mdriver` 在读 trace 时就帮我们拦截并修正了非法操作。我们的 `mm.c` 代码只需要专注于正确的分配/释放/重分配逻辑，不需要额外处理这些异常情况。

---

## 四、遇到的最棘手的困难

### 困难1: `prev_alloc` 标志位导致的段错误（Segmentation Fault）

**问题描述**: 第一版实现编译通过后运行直接段错误崩溃。

**根本原因**: 这是我遇到的**最棘手的 bug**。问题出在 `prev_alloc` 标志位的处理上。我的设计采用了"已分配块不存储 footer"的优化方案，通过 header 的 bit 1 来判断前一个块是否空闲。这需要在**所有修改 header 的地方都正确保留 `prev_alloc` 位**，否则会导致：

1. **错误读取 footer**: 当 `prev_alloc=0` 时（表示前块空闲），代码会读取 `bp-16` 位置的 footer 来获取前块大小。如果 `prev_alloc` 被错误地设为 0（前块实为已分配），则会读取到用户数据（而非合法的 footer），导致计算出错误的前块大小，后续的 `PREV_BLKP(bp)` 会返回一个无效的指针地址。

2. **遗漏插入**: `extend_heap` 创建的新空闲块没有插入空闲链表。后续 `place` 函数中 `remove_free(bp)` 尝试从链表中移除一个不在链表中的块，操作了未初始化的 `next`/`prev` 指针（野指针），直接导致段错误。

**解决方法**: 仔细追踪了每个修改 header 的位置，确保：
- `mm_free` 中保留 `prev_alloc`: `prev_flag = GET(HDRP(ptr)) & 0x2`
- `place` 中保留原块的 `prev_alloc`，并在分割时为剩余块设置正确的 `prev_alloc=0x2`
- `coalesce` 中每次修改 header 都保留最早合并块的 `prev_alloc`
- `extend_heap` 中从旧 epilogue 继承 `prev_alloc`，并初始化 `next`/`prev` 指针为 NULL，在 coalesce 后调用 `insert_free`
- `realloc` 的缩减和扩展路径中同样保留 `prev_alloc`

这个 bug 的难点在于它是**全流程性**的——需要追踪每个创建、修改、合并块的代码路径，确保 `prev_alloc` 在任何时刻都准确反映相邻块的状态。

### 困难2: Realloc 的利用率优化

**问题描述**: 初始实现中 realloc trace 的利用率只有 31% 和 28%。

**分析**: 原始的 realloc 只检查右侧（next）空闲块。但 realloc 场景中，一个常见的模式是：块需要增长，但右侧被占用，左侧的空闲块（之前 realloc 留下的"洞"）被完全忽略。

**解决方法**: 实现了 realloc 的三种原地扩展策略：
1. **仅合并右侧**（保持指针不变）— 最优情况
2. **仅合并左侧**（需要 memmove）— 利用左侧的"洞"
3. **双向合并** — 两侧都有空闲块

这使得 realloc 利用率从 ~30% 提升到了 ~45%，总体评分从 84 提升到 86。

### 困难3: LIFO vs 地址排序的权衡

**问题描述**: LIFO 插入快（O(1)）但碎片多；地址排序插入好但 binary trace 性能从 0.05s 恶化到 0.6s（12倍！）。

**最终方案**: 保留 LIFO 插入以保持性能，改用 best-fit 搜索策略来减少碎片。Best-fit 在当前类别中找到最匹配的块，避免了"大块被小块请求浪费"的情况。

---

## 五、总结

本实验实现了一个完整的动态内存分配器，核心特性包括：

| 特性 | 实现方案 |
|------|---------|
| 空闲块管理 | 隔离显式空闲链表（12个大小类别） |
| 搜索策略 | Best-Fit |
| 插入策略 | LIFO（O(1)） |
| 合并策略 | 立即合并（Immediate Coalescing） |
| 边界标记 | 双向 Boundary Tags + prev_alloc 优化 |
| Realloc | 多级原地扩展（前/后/双向合并） |
| 堆扩展 | 按需扩展（最小 4096B） |

**最终得分: 86/100**，所有 26 个 trace 全部通过正确性验证，吞吐量达到满分。

---

*参考: CS:APP 教材第 9.9 章「Dynamic Memory Allocation」*
