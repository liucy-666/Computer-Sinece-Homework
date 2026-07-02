/*
 * mm.c - 高性能动态内存分配器
 *
 * 【算法设计思想】
 * 本实现采用"隔离空闲链表 (Segregated Free Lists) + 边界标记 (Boundary Tags)"
 * 的设计方案，参考CS:APP教材第9.9章。
 *
 * 1. 数据结构：
 *    - 每个块的header存储：块大小（总是8的倍数，低3位为0）+ 状态标志。
 *      bit 0：当前块是否已分配 (1 = 已分配, 0 = 空闲)
 *      bit 1：前一个块是否已分配 (1 = 已分配, 0 = 空闲)
 *    - 已分配块：只有header（8字节），没有footer，节省空间。
 *    - 空闲块：有header（8字节）+ footer（8字节），footer是header的副本。
 *      空闲块的payload区域前16字节用于存储next和prev指针（双向链表）。
 *    - 通过bit 1（prev_alloc）可以安全地判断前一个块是否有footer：
 *      若prev_alloc=0（前块空闲），则前块有footer在bp-16处；
 *      若prev_alloc=1（前块已分配），则前块无footer，不能读取bp-16。
 *
 * 2. 隔离空闲链表：
 *    - 12个大小类别，按2的幂次划分：
 *      Class  0: [32,    64]
 *      Class  1: (64,   128]
 *      Class  2: (128,  256]
 *      Class  3: (256,  512]
 *      Class  4: (512, 1024]
 *      Class  5: (1024, 2048]
 *      Class  6: (2048, 4096]
 *      Class  7: (4096, 8192]
 *      Class  8: (8192, 16384]
 *      Class  9: (16384, 32768]
 *      Class 10: (32768, 65536]
 *      Class 11: > 65536
 *    - 每个类别内使用双向链表管理空闲块。
 *
 * 3. 分配策略 (mm_malloc)：
 *    - 首适配(first-fit)：在对应类别及更高级别的链表中查找第一个足够大的块。
 *    - 若找到的块可分割（剩余 >= 32B），则分割并保留剩余部分在空闲链表中。
 *    - 若所有类别均无合适块，扩展堆空间（最小4096B）。
 *
 * 4. 释放策略 (mm_free)：
 *    - 设置header和footer标记为空闲，保留prev_alloc位。
 *    - 立即合并(Immediate Coalescing)：检查并合并相邻空闲块。
 *    - LIFO插入：合并后插入对应链表头部（O(1)）。
 *
 * 5. 重分配策略 (mm_realloc)：
 *    - 优先原地缩减或扩展（避免数据拷贝）。
 *    - 原地扩展：合并右侧连续空闲块。
 *    - 原地缩减：分割多余空间。
 *    - 最后才使用 malloc + memcpy + free 方案。
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * 学生信息
 ********************************************************/
team_t team = {
	"Individual",
	"Student",
	"student@xmu.edu.cn",
	"",
	""
};

/*********************************************************
 * 基本常量
 ********************************************************/
#define WSIZE       8               /* 字大小（字节） */
#define DSIZE       16              /* 双字大小 */
#define CHUNKSIZE   (1 << 12)       /* 堆扩展单位：4096字节 */
#define MIN_BLOCK   32              /* 最小空闲块：header+next+prev+footer */

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define LIST_COUNT  12              /* 隔离链表类别数 */

/*********************************************************
 * 基本读写宏
 ********************************************************/
#define PACK(size, alloc)  ((size) | (alloc))
#define GET(p)             (*(size_t *)(p))
#define PUT(p, val)        (*(size_t *)(p) = (size_t)(val))

#define GET_SIZE(p)        (GET(p) & ~0x7)
#define GET_ALLOC(p)       (GET(p) & 0x1)
#define GET_PREV_ALLOC(p)  (GET(p) & 0x2)

/*********************************************************
 * 块指针计算宏
 ********************************************************/
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/*********************************************************
 * 空闲链表操作宏
 ********************************************************/
#define SET_NEXT(bp, val)  (*(char **)(bp) = (val))
#define SET_PREV(bp, val)  (*(char **)((char *)(bp) + WSIZE) = (val))
#define GET_NEXT(bp)       (*(char **)(bp))
#define GET_PREV(bp)       (*(char **)((char *)(bp) + WSIZE))

/*********************************************************
 * 全局变量
 ********************************************************/
static char *heap_listp = NULL;
static void *free_lists[LIST_COUNT];

/*********************************************************
 * 函数原型
 ********************************************************/
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void insert_free(void *bp);
static void remove_free(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/*********************************************************
 * 工具函数
 ********************************************************/

/* 根据块大小返回对应链表索引 */
static int get_list_index(size_t size)
{
	if (size <= 64)   return 0;
	if (size <= 128)  return 1;
	if (size <= 256)  return 2;
	if (size <= 512)  return 3;
	if (size <= 1024) return 4;
	if (size <= 2048) return 5;
	if (size <= 4096) return 6;
	if (size <= 8192) return 7;
	if (size <= 16384) return 8;
	if (size <= 32768) return 9;
	if (size <= 65536) return 10;
	return 11;
}

/*
 * mm_init - 初始化内存分配系统
 */
int mm_init(void)
{
	int i;

	for (i = 0; i < LIST_COUNT; i++)
		free_lists[i] = NULL;

	/* 分配初始堆：pad + prologue header + prologue footer + epilogue */
	if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
		return -1;

	/*
	 * 堆布局：
	 * [0-7]:   对齐填充 (0)
	 * [8-15]:  Prologue Header (size=16, alloc=1, prev_alloc=1)
	 * [16-23]: Prologue Footer (size=16, alloc=1) — 与payload重叠，仅标记
	 * [24-31]: Epilogue Header (size=0, alloc=1, prev_alloc=1)
	 */
	PUT(heap_listp, 0);
	PUT(heap_listp + WSIZE,      PACK(DSIZE, 1) | 0x2);  /* pro hdr: alloc=1, prev=1 */
	PUT(heap_listp + DSIZE,      PACK(DSIZE, 1));         /* pro ftr */
	PUT(heap_listp + WSIZE + DSIZE, PACK(0, 1) | 0x2);   /* epi hdr: alloc=1, prev=1 */

	heap_listp += DSIZE;  /* 指向prologue的payload */

	/* 扩展初始空闲空间 */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return -1;

	return 0;
}

/*
 * extend_heap - 扩展堆空间
 * 返回新空闲块的bp（已合并），失败返回NULL
 */
static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;
	size_t prev_flag;

	size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	/*
	 * bp = 旧brk值 = 新空间起始地址
	 * 旧epilogue header在HDRP(bp)处（bp-8），即将被覆盖。
	 * 从旧epilogue中读取prev_alloc，正确反映前一个块的状态。
	 */
	prev_flag = GET(HDRP(bp)) & 0x2;  /* 保存旧epilogue的prev_alloc */

	/* 新空闲块：覆盖旧epilogue */
	PUT(HDRP(bp), PACK(size, 0) | prev_flag);          /* header: 保留prev_alloc */
	PUT(FTRP(bp), PACK(size, 0));                       /* footer */
	SET_NEXT(bp, NULL);                                  /* 初始化链表指针 */
	SET_PREV(bp, NULL);
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));               /* 新epilogue */
	/* 新epilogue的prev_alloc=0: 前面的空闲块是空闲的 */

	bp = coalesce(bp);
	insert_free(bp);                                     /* 插入空闲链表 */
	return bp;
}

/*
 * insert_free - LIFO插入空闲链表（O(1)时间复杂度）
 */
static void insert_free(void *bp)
{
	int idx = get_list_index(GET_SIZE(HDRP(bp)));
	void *head = free_lists[idx];

	SET_NEXT(bp, head);
	SET_PREV(bp, NULL);

	if (head != NULL)
		SET_PREV(head, bp);

	free_lists[idx] = bp;
}

/*
 * remove_free - 从空闲链表中移除块
 */
static void remove_free(void *bp)
{
	int idx = get_list_index(GET_SIZE(HDRP(bp)));
	void *next = GET_NEXT(bp);
	void *prev = GET_PREV(bp);

	if (prev != NULL)
		SET_NEXT(prev, next);
	else
		free_lists[idx] = next;

	if (next != NULL)
		SET_PREV(next, prev);
}

/*
 * coalesce - 立即合并相邻空闲块
 * 返回合并后块的bp
 */
static void *coalesce(void *bp)
{
	size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));
	size_t prev_flag;

	/* Case 1: 前后都分配 */
	if (prev_alloc && next_alloc) {
		/* 设置下一个块的prev_alloc=0（当前块现在是空闲的） */
		prev_flag = GET(HDRP(NEXT_BLKP(bp))) & ~0x2;
		PUT(HDRP(NEXT_BLKP(bp)), prev_flag);
		return bp;
	}

	/* Case 2: 前分配，后空闲 — 向前合并 */
	else if (prev_alloc && !next_alloc) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		remove_free(NEXT_BLKP(bp));
		prev_flag = GET(HDRP(bp)) & 0x2;  /* 保留当前prev_alloc */
		PUT(HDRP(bp), PACK(size, 0) | prev_flag);
		PUT(FTRP(bp), PACK(size, 0));
		/* 更新合并后下一个块的prev_alloc */
		prev_flag = GET(HDRP(NEXT_BLKP(bp))) & ~0x2;
		PUT(HDRP(NEXT_BLKP(bp)), prev_flag);
	}

	/* Case 3: 前空闲，后分配 — 向后合并 */
	else if (!prev_alloc && next_alloc) {
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		remove_free(PREV_BLKP(bp));
		bp = PREV_BLKP(bp);
		prev_flag = GET(HDRP(bp)) & 0x2;  /* 保留新bp的prev_alloc */
		PUT(HDRP(bp), PACK(size, 0) | prev_flag);
		PUT(FTRP(bp), PACK(size, 0));
		prev_flag = GET(HDRP(NEXT_BLKP(bp))) & ~0x2;
		PUT(HDRP(NEXT_BLKP(bp)), prev_flag);
	}

	/* Case 4: 前后都空闲 — 双向合并 */
	else {
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
			GET_SIZE(HDRP(NEXT_BLKP(bp)));
		remove_free(PREV_BLKP(bp));
		remove_free(NEXT_BLKP(bp));
		bp = PREV_BLKP(bp);
		prev_flag = GET(HDRP(bp)) & 0x2;  /* 保留最前块的prev_alloc */
		PUT(HDRP(bp), PACK(size, 0) | prev_flag);
		PUT(FTRP(bp), PACK(size, 0));
		prev_flag = GET(HDRP(NEXT_BLKP(bp))) & ~0x2;
		PUT(HDRP(NEXT_BLKP(bp)), prev_flag);
	}

	return bp;
}

/*
 * find_fit - 隔离链表最佳适配查找
 * 在对应类别及更高级别中查找最合适（最接近请求大小）的空闲块
 */
static void *find_fit(size_t asize)
{
	int idx = get_list_index(asize);
	int i;
	void *bp;
	void *best = NULL;
	size_t best_size = ~0;  /* 初始化为最大值 */

	for (i = idx; i < LIST_COUNT; i++) {
		for (bp = free_lists[i]; bp != NULL; bp = GET_NEXT(bp)) {
			size_t bp_size = GET_SIZE(HDRP(bp));
			if (bp_size >= asize && bp_size < best_size) {
				best = bp;
				best_size = bp_size;
				/* 精确匹配，直接返回 */
				if (bp_size == asize)
					return best;
			}
		}
		/* 如果在当前类别找到合适的块，不再搜索更大的类别 */
		if (best != NULL)
			return best;
	}
	return best;
}

/*
 * place - 在空闲块中放置分配请求
 * 若剩余空间足够，进行分割
 */
static void place(void *bp, size_t asize)
{
	size_t total_size = GET_SIZE(HDRP(bp));
	size_t remainder = total_size - asize;
	size_t prev_flag;

	remove_free(bp);

	if (remainder >= MIN_BLOCK) {
		/* 分割 */
		prev_flag = GET(HDRP(bp)) & 0x2;  /* 保留prev_alloc */
		PUT(HDRP(bp), PACK(asize, 1) | prev_flag);
		/* 无需footer：下一个块的prev_alloc会标记本块已分配 */

		/* 剩余空闲块 */
		void *remain_bp = NEXT_BLKP(bp);
		PUT(HDRP(remain_bp), PACK(remainder, 0) | 0x2);   /* prev_alloc=1 */
		PUT(FTRP(remain_bp), PACK(remainder, 0));

		/* 设置剩余块下一个块的prev_alloc=0 */
		prev_flag = GET(HDRP(NEXT_BLKP(remain_bp))) & ~0x2;
		PUT(HDRP(NEXT_BLKP(remain_bp)), prev_flag);

		insert_free(remain_bp);
	} else {
		/* 不分割：整个块分配 */
		prev_flag = GET(HDRP(bp)) & 0x2;  /* 保留prev_alloc */
		PUT(HDRP(bp), PACK(total_size, 1) | prev_flag);

		/* 设置下一个块的prev_alloc=1 */
		PUT(HDRP(NEXT_BLKP(bp)),
			GET(HDRP(NEXT_BLKP(bp))) | 0x2);
	}
}

/*
 * mm_malloc - 分配内存
 */
void *mm_malloc(size_t size)
{
	size_t asize;
	size_t extendsize;
	char *bp;

	if (size == 0)
		return NULL;

	/* 计算块大小：header + 对齐payload + footer空间 */
	if (size <= DSIZE)
		asize = 2 * DSIZE;  /* 最小32B */
	else
		asize = ALIGN(size + DSIZE);

	/* 在空闲链表中查找 */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	/* 扩展堆 */
	extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;

	place(bp, asize);
	return bp;
}

/*
 * mm_free - 释放内存
 */
void mm_free(void *ptr)
{
	size_t size;
	size_t prev_flag;

	if (ptr == NULL)
		return;

	size = GET_SIZE(HDRP(ptr));

	/* 设置header和footer为空闲，保留prev_alloc */
	prev_flag = GET(HDRP(ptr)) & 0x2;
	PUT(HDRP(ptr), PACK(size, 0) | prev_flag);
	PUT(FTRP(ptr), PACK(size, 0));

	/* 下一个块的prev_alloc=0（本块现在空闲） */
	prev_flag = GET(HDRP(NEXT_BLKP(ptr))) & ~0x2;
	PUT(HDRP(NEXT_BLKP(ptr)), prev_flag);

	/* 合并相邻空闲块 */
	ptr = coalesce(ptr);

	/* 插入空闲链表 */
	insert_free(ptr);
}

/*
 * mm_realloc - 重新调整内存块大小
 * 支持多种原地扩展策略以减少数据拷贝和碎片
 */
void *mm_realloc(void *ptr, size_t size)
{
	void *new_ptr;
	void *prev, *next;
	size_t old_size, new_size, copy_size;
	size_t prev_size, next_size, combined_size;
	size_t prev_flag;
	int prev_free, next_free;

	if (ptr == NULL)
		return mm_malloc(size);

	if (size == 0) {
		mm_free(ptr);
		return NULL;
	}

	old_size = GET_SIZE(HDRP(ptr));
	new_size = (size <= DSIZE) ? (2 * DSIZE) : ALIGN(size + DSIZE);

	/* Case 1: 新大小 <= 旧大小 — 原地缩减 */
	if (new_size <= old_size) {
		if (old_size - new_size >= MIN_BLOCK) {
			prev_flag = GET(HDRP(ptr)) & 0x2;
			PUT(HDRP(ptr), PACK(new_size, 1) | prev_flag);

			void *remain_bp = NEXT_BLKP(ptr);
			PUT(HDRP(remain_bp), PACK(old_size - new_size, 0) | 0x2);
			PUT(FTRP(remain_bp), PACK(old_size - new_size, 0));

			prev_flag = GET(HDRP(NEXT_BLKP(remain_bp))) & ~0x2;
			PUT(HDRP(NEXT_BLKP(remain_bp)), prev_flag);

			remain_bp = coalesce(remain_bp);
			insert_free(remain_bp);
		}
		return ptr;
	}

	/* 检查相邻块状态 */
	prev_free = !GET_PREV_ALLOC(HDRP(ptr));
	next = NEXT_BLKP(ptr);
	next_free = !GET_ALLOC(HDRP(next));
	prev_size = prev_free ? GET_SIZE(HDRP(PREV_BLKP(ptr))) : 0;
	next_size = next_free ? GET_SIZE(HDRP(next)) : 0;

	/* Case 2a: 优先仅合并右侧空闲块（保持指针不变，最高效） */
	if (next_free && old_size + next_size >= new_size) {
		remove_free(next);
		combined_size = old_size + next_size;

		if (combined_size - new_size >= MIN_BLOCK) {
			prev_flag = GET(HDRP(ptr)) & 0x2;
			PUT(HDRP(ptr), PACK(new_size, 1) | prev_flag);

			void *remain_bp = NEXT_BLKP(ptr);
			PUT(HDRP(remain_bp), PACK(combined_size - new_size, 0) | 0x2);
			PUT(FTRP(remain_bp), PACK(combined_size - new_size, 0));

			prev_flag = GET(HDRP(NEXT_BLKP(remain_bp))) & ~0x2;
			PUT(HDRP(NEXT_BLKP(remain_bp)), prev_flag);

			remain_bp = coalesce(remain_bp);
			insert_free(remain_bp);
		} else {
			prev_flag = GET(HDRP(ptr)) & 0x2;
			PUT(HDRP(ptr), PACK(combined_size, 1) | prev_flag);
			PUT(HDRP(NEXT_BLKP(ptr)),
				GET(HDRP(NEXT_BLKP(ptr))) | 0x2);
		}
		return ptr;
	}

	/* Case 2b: 仅合并左侧空闲块（需要移动数据，但避免重新分配） */
	if (prev_free && prev_size + old_size >= new_size) {
		prev = PREV_BLKP(ptr);
		remove_free(prev);
		combined_size = prev_size + old_size;
		prev_flag = GET(HDRP(prev)) & 0x2;

		if (combined_size - new_size >= MIN_BLOCK) {
			PUT(HDRP(prev), PACK(new_size, 1) | prev_flag);
			memmove(prev, ptr, old_size - DSIZE);

			void *remain_bp = NEXT_BLKP(prev);
			PUT(HDRP(remain_bp), PACK(combined_size - new_size, 0) | 0x2);
			PUT(FTRP(remain_bp), PACK(combined_size - new_size, 0));

			prev_flag = GET(HDRP(NEXT_BLKP(remain_bp))) & ~0x2;
			PUT(HDRP(NEXT_BLKP(remain_bp)), prev_flag);

			remain_bp = coalesce(remain_bp);
			insert_free(remain_bp);
		} else {
			PUT(HDRP(prev), PACK(combined_size, 1) | prev_flag);
			memmove(prev, ptr, old_size - DSIZE);
			PUT(HDRP(NEXT_BLKP(prev)),
				GET(HDRP(NEXT_BLKP(prev))) | 0x2);
		}
		return prev;
	}

	/* Case 2c: 合并左右两侧空闲块（需要移动数据） */
	if (prev_free && next_free &&
		prev_size + old_size + next_size >= new_size) {
		prev = PREV_BLKP(ptr);
		remove_free(prev);
		remove_free(next);
		combined_size = prev_size + old_size + next_size;
		prev_flag = GET(HDRP(prev)) & 0x2;

		if (combined_size - new_size >= MIN_BLOCK) {
			PUT(HDRP(prev), PACK(new_size, 1) | prev_flag);
			memmove(prev, ptr, old_size - DSIZE);

			void *remain_bp = NEXT_BLKP(prev);
			PUT(HDRP(remain_bp), PACK(combined_size - new_size, 0) | 0x2);
			PUT(FTRP(remain_bp), PACK(combined_size - new_size, 0));

			prev_flag = GET(HDRP(NEXT_BLKP(remain_bp))) & ~0x2;
			PUT(HDRP(NEXT_BLKP(remain_bp)), prev_flag);

			remain_bp = coalesce(remain_bp);
			insert_free(remain_bp);
		} else {
			PUT(HDRP(prev), PACK(combined_size, 1) | prev_flag);
			memmove(prev, ptr, old_size - DSIZE);
			PUT(HDRP(NEXT_BLKP(prev)),
				GET(HDRP(NEXT_BLKP(prev))) | 0x2);
		}
		return prev;
	}

	/* Case 3: 无法原地扩展 — 分配-拷贝-释放 */
	new_ptr = mm_malloc(size);
	if (new_ptr == NULL)
		return NULL;

	copy_size = (old_size - DSIZE < size) ? (old_size - DSIZE) : size;
	memcpy(new_ptr, ptr, copy_size);
	mm_free(ptr);

	return new_ptr;
}

/*
 * mm_heapcheck - 堆检查（暂未实现）
 */
void mm_heapcheck(void)
{
}

static void print_block(int request_id, int payload)
{
	printf("\n[%s]$BLOCK %d %d\n", __func__, request_id, payload);
}
