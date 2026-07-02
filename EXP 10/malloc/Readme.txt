动态内存分配器实验

你需要修改mm.c（你只能修改此文件，其他文件不能修改），使之能够处理内存分配（mm_malloc）、内存释放（mm_free）、内存扩张（mm_realloc）等功能。
你可以修改traces目录下的TRACE_LIST.txt，以运行不同的trace。
你需要跑尽可能多的trace，并在评分中拿到尽可能的高分。

【注意】并不是所有trace都可以跑的。有些trace内部包含了错误的操作，是跑不通的。比如：
1、试图realloc一个不存在的指针
2、试图free一个不存在的指针

Linux：
1、make
2、./malloc -t traces

Windows:
1、用VS2019打开工程myMalloc/myMalloc.sln，编译
2、生成可执行代码，myMalloc -t traces


【提交】你需要将mm.c修改为mm_201900221122.c，其中后面是你的学号。提交到educoder上。你只需要提交mm.c文件。



写在前面的话：运行先make ,再./malloc -t traces指令即可看到结果。

一. 首先，我的动态内存分配器实现了以下几个功能：
1. malloc(size): 从堆的空闲空间中分配 size字节，返回指向该空间（payload）的指针
2. free(ptr): 释放 ptr 指向的已分配空间，使其可被后续分配重用
3. realloc(ptr, size): 调整已分配块的大小，可能涉及移动数据

二.方法与原理
方法1：边界标记法
原理：每个内存块都有 header 和 footer，记录块的大小和分配状态。Header 和 footer 互为镜像，使得从任意块出发都可以找到相邻块。
优化：已分配块不存储 footer（节省 8 字节/块）。通过 Header 的 bit 1（prev_alloc 标志）来判断前一个块是否空闲：
    prev_alloc = 1: 前一个块已分配，无 footer，不能向后合并
    prev_alloc = 0: 前一个块空闲，有 footer（位于 bp - 16），可以向后合并
方法2：隔离空闲链表
    原理：将空闲块按大小分为 12 个类别，每个类别维护一个显式双向链表，分配时只需在对应类别及更高级别搜索，大幅减少搜索时间。

三.算法流程

mm_malloc(size)函数：
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

四.遇到的困难和解决方法
1.有些trace里面试图realloc一个不存在的指针或者试图free一个不存在的指针，这些trace怎么处理？
A：realloc 不存在的指针自动转为 alloc, free 不存在的指针则标记为跳过,被标记为ERROR_BUG会在执行阶段的静默跳过

2.prev_alloc 标志位导致的段错误,导致第一版实现编译通过后运行直接段错误崩溃!
A: 这是我遇到的最棘手的 bug。问题出在 prev_alloc 标志位的处理上。我的设计采用了"已分配块不存储 footer"的优化方案，
通过 header 的 bit 1 来判断前一个块是否空闲。这需要在所有修改 header 的地方都正确保留 prev_alloc 位，否则会导致错误读取 footer。

3. Realloc 的利用率优化
A：初始实现中 realloc trace 的利用率只有 31% 和 28%原始的 realloc 只检查右侧（next）空闲块。但 realloc 场景中，一个常见的模式是：块需要增长，
但右侧被占用，左侧的空闲块被完全忽略。于是我分情况对其进行了优化：
1. 仅合并右侧（保持指针不变）— 最优情况
2. 仅合并左侧（需要 memmove）— 利用左侧的空闲块
3. 双向合并 — 两侧都有空闲块
realloc 利用率从 ~30% 提升到了 ~45%，总体评分从 84 提升到 86。

4.LIFO vs 地址排序的权衡
A：IFO 插入快（O(1)）但碎片多；地址排序插入碎片少但 binary trace 性能从 0.05s 恶化到 0.6s。于是我选择保留 LIFO 插入以保持性能，
改用 best-fit 搜索策略来减少碎片。Best-fit 在当前类别中找到最匹配的块，避免了"大块被小块请求浪费"的情况