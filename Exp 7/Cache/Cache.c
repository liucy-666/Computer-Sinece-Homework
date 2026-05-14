#include <stdio.h>
#include <stdlib.h>
#include "common.h"

#define GET_POWER_OF_2(X)	(X == 0x20 ? 5 : X == 0x40 ? 6 : X == 0x08 ? 3 : 0) // 简化示意，实际仍可用你定义的宏

#define DCACHE_SIZE						16384
#define DCACHE_DATA_PER_LINE			64									
#define DCACHE_DATA_PER_LINE_ADDR_BITS	6    
#define DCACHE_WAY                      8    
#define DCACHE_SET						32   
#define DCACHE_SET_ADDR_BITS			5    

struct DCACHE_LineStruct {
	UINT8	Valid;
	UINT64	Tag;
	UINT32  LRU_counter; 
	UINT8	Data[DCACHE_DATA_PER_LINE];
} DCache[DCACHE_SET][DCACHE_WAY];

/* --- 辅助函数 --- */
void UpdateLRU(UINT32 setIndex, UINT32 wayIndex) {
	UINT32 current_val = DCache[setIndex][wayIndex].LRU_counter;
	for (UINT32 j = 0; j < DCACHE_WAY; j++) {
		if (DCache[setIndex][j].Valid && DCache[setIndex][j].LRU_counter < current_val) {
			DCache[setIndex][j].LRU_counter++;
		}
	}
	DCache[setIndex][wayIndex].LRU_counter = 0;
}

UINT32 FindVictimWay(UINT32 setIndex) {
	for (UINT32 j = 0; j < DCACHE_WAY; j++) if (DCache[setIndex][j].Valid == 0) return j;
	UINT32 victim = 0;
	for (UINT32 j = 1; j < DCACHE_WAY; j++) {
		if (DCache[setIndex][j].LRU_counter > DCache[setIndex][victim].LRU_counter) victim = j;
	}
	return victim;
}

void InitDataCache() {
	for (int i = 0; i < DCACHE_SET; i++) {
		for (int j = 0; j < DCACHE_WAY; j++) {
			DCache[i][j].Valid = 0;
			DCache[i][j].LRU_counter = j; 
		}
	}
}

/* --- 数据加载与写入 --- */
void LoadDataCacheLineFromMemory(UINT64 Address, UINT32 setIndex, UINT32 wayIndex) {
	UINT64 AlignAddress = Address & ~0x3F; // 64字节对齐
	UINT64* pp = (UINT64*)DCache[setIndex][wayIndex].Data;
	for (int i = 0; i < 8; i++) pp[i] = ReadMemory(AlignAddress + 8LL * i);
}

// 注意：Write-Through 策略下，写操作直接调用 WriteMemory，不再需要 StoreDataCacheLineToMemory

UINT8 AccessDataCache(UINT64 Address, UINT8 Operation, UINT8 DataSize, UINT64 StoreValue, UINT64* LoadResult) {
	UINT32 setIndex = (Address >> 6) & 0x1F;
	UINT64 AddressTag = (Address >> 11);
	UINT8 BlockOffset = Address & 0x3F;
	UINT32 hitWay = 0xFFFFFFFF;
	UINT8 MissFlag = 'M';

	for (UINT32 j = 0; j < DCACHE_WAY; j++) {
		if (DCache[setIndex][j].Valid && DCache[setIndex][j].Tag == AddressTag) {
			hitWay = j; MissFlag = 'H'; break;
		}
	}

	if (MissFlag == 'M') {
		hitWay = FindVictimWay(setIndex);
		LoadDataCacheLineFromMemory(Address, setIndex, hitWay);
		DCache[setIndex][hitWay].Valid = 1;
		DCache[setIndex][hitWay].Tag = AddressTag;
	}

	UpdateLRU(setIndex, hitWay);

	if (Operation == 'L') {
		UINT64 res = 0;
		for (int i = 0; i < DataSize; i++) res |= ((UINT64)DCache[setIndex][hitWay].Data[BlockOffset + i] << (8 * i));
		*LoadResult = res;
	} else {
		// Write-Through：更新 Cache 同时更新 Memory
		for (int i = 0; i < DataSize; i++) {
			DCache[setIndex][hitWay].Data[BlockOffset + i] = (StoreValue >> (8 * i)) & 0xFF;
		}
		// 关键修复：确保数据立即同步到存储器
		UINT64 AlignAddr8 = Address & ~0x7; 
		UINT64 memData = ReadMemory(AlignAddr8);
		UINT8* bytePtr = (UINT8*)&memData;
		for(int i = 0; i < DataSize; i++) bytePtr[(Address + i) & 0x7] = (StoreValue >> (8 * i)) & 0xFF;
		WriteMemory(AlignAddr8, memData);
	}
	return MissFlag;
}

/* --- 指令 Cache 实现 --- */
#define ICACHE_SET 256
struct { UINT8 Valid; UINT64 Tag; UINT8 Data[64]; } ICache[ICACHE_SET];

void InitInstCache(void) {
	for (int i = 0; i < ICACHE_SET; i++) ICache[i].Valid = 0;
}

UINT8 AccessInstCache(UINT64 Address, UINT8 Operation, UINT8 InstSize, UINT64* InstResult) {
	(void)Operation;
	UINT32 idx = (Address >> 6) & 0xFF;
	UINT64 tag = (Address >> 14);
	UINT8 offset = Address & 0x3F;
	UINT8 res = 'H';

	if (!(ICache[idx].Valid && ICache[idx].Tag == tag)) {
		res = 'M';
		ICache[idx].Valid = 1; ICache[idx].Tag = tag;
		for (int i = 0; i < 8; i++) ((UINT64*)ICache[idx].Data)[i] = ReadMemory((Address & ~0x3F) + 8LL * i);
	}

	UINT64 val = 0;
	for (int i = 0; i < InstSize; i++) val |= ((UINT64)ICache[idx].Data[offset + i] << (8 * i));
	*InstResult = val;
	return res;
}