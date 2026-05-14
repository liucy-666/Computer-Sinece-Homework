///////////////////////////////////////////////////////////////////////
////  Copyright 2022 by mars.                                        //
///////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#define DEBUG	0
#define GET_POWER_OF_2(X)	(X == 0x00		? 0 : \
							X == 0x01		? 0 : \
							X == 0x02		? 1 : \
							X == 0x04		? 2 : \
							X == 0x08		? 3 : \
							X == 0x10		? 4 : \
							X == 0x20		? 5 : \
							X == 0x40		? 6 : \
							X == 0x80		? 7 : \
							X == 0x100		? 8 : \
							X == 0x200		? 9 : \
							X == 0x400		? 10 : \
							X == 0x800		? 11 : \
							X == 0x1000		? 12 : \
							X == 0x2000		? 13 : \
							X == 0x4000		? 14 : \
							X == 0x8000		? 15 : \
							X == 0x10000	? 16 : \
							X == 0x20000	? 17 : \
							X == 0x40000	? 18 : \
							X == 0x80000	? 19 : \
							X == 0x100000	? 20 : \
							X == 0x200000	? 21 : \
							X == 0x400000	? 22 : \
							X == 0x800000	? 23 : \
							X == 0x1000000	? 24 : \
							X == 0x2000000	? 25 : \
							X == 0x4000000	? 26 : \
							X == 0x8000000	? 27 : \
							X == 0x10000000	? 28 : \
							X == 0x20000000	? 29 : \
							X == 0x40000000	? 30 : \
							X == 0x80000000	? 31 : \
							X == 0x100000000	? 32 : 0)
/*
	直接映射Data Cache，16KB大小
	每行存放64个字节，共256行
*/
/*
    4-Way Set Associative Data Cache
*/
#define DCACHE_SIZE                     16384
#define DCACHE_DATA_PER_LINE            64
#define DCACHE_DATA_PER_LINE_ADDR_BITS  GET_POWER_OF_2(DCACHE_DATA_PER_LINE)
#define DCACHE_WAY                      4
#define DCACHE_SET \
    (DCACHE_SIZE / DCACHE_DATA_PER_LINE / DCACHE_WAY)
#define DCACHE_SET_ADDR_BITS \
    GET_POWER_OF_2(DCACHE_SET)

// === 修改开始：时钟算法数据结构 ===
struct DCACHE_LineStruct
{
    UINT8  Valid;
    UINT8  Dirty;
    UINT64 Tag;
    UINT8  Use;    // 时钟算法使用位：1=已访问，0=未访问
    UINT8  Data[DCACHE_DATA_PER_LINE];
} DCache[DCACHE_SET][DCACHE_WAY];

// 每个Set独立的时钟指针（范围0~DCACHE_WAY-1）
UINT8 DCacheClockPtr[DCACHE_SET];
// === 修改结束 ===

/*
    Init
*/
void InitDataCache()
{
    UINT32 set;
    UINT32 way;
    printf("[%s] +-----------------------------------+\n", __func__);
    printf("[%s] |      4-Way Data Cache Init        |\n", __func__);
    // === 修改开始：添加时钟算法提示 ===
    printf("[%s] |      使用基础时钟替换算法          |\n", __func__);
    // === 修改结束 ===
    printf("[%s] +-----------------------------------+\n", __func__);
    for (set = 0; set < DCACHE_SET; set++)
    {
        // === 修改开始：初始化时钟指针和使用位 ===
        DCacheClockPtr[set] = 0;  // 时钟指针初始化为0
        // === 修改结束 ===
        for (way = 0; way < DCACHE_WAY; way++)
        {
            DCache[set][way].Valid = 0;
            DCache[set][way].Dirty = 0;
            // === 修改开始：初始化使用位 ===
            DCache[set][way].Use = 0;  // 使用位初始化为0
            // === 修改结束 ===
        }
    }
}

// === 修改开始：删除原UpdateLRU函数，替换为时钟命中更新 ===
/*
    Update Clock (替换原UpdateLRU)
*/
void UpdateClock(UINT32 set, UINT32 hitway)
{
    // 命中时仅需将对应Way的使用位设为1，无需遍历所有Way
    DCache[set][hitway].Use = 1;
}
// === 修改结束 ===

/*
    Find victim
*/
UINT32 FindVictimWay(UINT32 set)
{
    /*
        invalid first
    */
    for (UINT32 way = 0; way < DCACHE_WAY; way++)
    {
        if (DCache[set][way].Valid == 0)
        {
            return way;
        }
    }

    // === 修改开始：替换原LRU查找为时钟算法 ===
    /*
        Clock replace
    */
    while (1)
    {
        UINT32 current = DCacheClockPtr[set];
        if (DCache[set][current].Use == 0)
        {
            // 找到未使用的行，指针后移一位，返回该Way
            DCacheClockPtr[set] = (current + 1) % DCACHE_WAY;
            return current;
        }
        else
        {
            // 已使用的行，清掉使用位，指针后移
            DCache[set][current].Use = 0;
            DCacheClockPtr[set] = (current + 1) % DCACHE_WAY;
        }
    }
    // === 修改结束 ===
}

/*
    Load line from memory
*/
void LoadDataCacheLineFromMemory(
    UINT64 Address,
    UINT32 Set,
    UINT32 Way
)
{
    UINT64 AlignAddress;
    UINT64* pp;
    AlignAddress =
        Address &
        ~(DCACHE_DATA_PER_LINE - 1);
    pp =
        (UINT64*)DCache[Set][Way].Data;
    for (UINT32 i = 0;
        i < DCACHE_DATA_PER_LINE / 8;
        i++)
    {
        pp[i] =
            ReadMemory(
                AlignAddress + 8LL * i
            );
    }
}

/*
    Write back
*/
void StoreDataCacheLineToMemory(
    UINT64 Address,
    UINT32 Set,
    UINT32 Way
)
{
    UINT64 AlignAddress;
    UINT64* pp;
    AlignAddress =
        Address &
        ~(DCACHE_DATA_PER_LINE - 1);
    pp =
        (UINT64*)DCache[Set][Way].Data;
    for (UINT32 i = 0;
        i < DCACHE_DATA_PER_LINE / 8;
        i++)
    {
        WriteMemory(
            AlignAddress + 8LL * i,
            pp[i]
        );
    }
}

/*
    Access Data Cache
*/
UINT8 AccessDataCache(
    UINT64 Address,
    UINT8 Operation,
    UINT8 DataSize,
    UINT64 StoreValue,
    UINT64* LoadResult
)
{
    UINT32 Set;
    UINT8 BlockOffset;
    UINT64 Tag;
    UINT64 ReadValue = 0;
    UINT8 MissFlag = 'M';
    *LoadResult = 0;
    /*
        Address split
    */
    Set =
        (Address >> DCACHE_DATA_PER_LINE_ADDR_BITS)
        % DCACHE_SET;
    BlockOffset =
        Address % DCACHE_DATA_PER_LINE;
    Tag =
        (Address >> DCACHE_DATA_PER_LINE_ADDR_BITS)
        >> DCACHE_SET_ADDR_BITS;
    /*
        Search hit
    */
    UINT32 HitWay = 0xFFFFFFFF;
    for (UINT32 way = 0; way < DCACHE_WAY; way++)
    {
        if (DCache[Set][way].Valid &&
            DCache[Set][way].Tag == Tag)
        {
            HitWay = way;
            MissFlag = 'H';
            break;
        }
    }
    /*
        HIT
    */
    if (HitWay != 0xFFFFFFFF)
    {
        // === 修改开始：替换UpdateLRU为UpdateClock ===
        UpdateClock(Set, HitWay);
        // === 修改结束 ===
        if (Operation == 'L')
        {
            switch (DataSize)
            {
            case 1:
                ReadValue =
                    DCache[Set][HitWay]
                    .Data[BlockOffset];
                break;
            case 2:
                BlockOffset &= 0xFE;
                for (int i = 1; i >= 0; i--)
                {
                    ReadValue <<= 8;
                    ReadValue |=
                        DCache[Set][HitWay]
                        .Data[BlockOffset + i];
                }
                break;
            case 4:
                BlockOffset &= 0xFC;
                for (int i = 3; i >= 0; i--)
                {
                    ReadValue <<= 8;
                    ReadValue |=
                        DCache[Set][HitWay]
                        .Data[BlockOffset + i];
                }
                break;
            case 8:
                BlockOffset &= 0xF8;
                for (int i = 7; i >= 0; i--)
                {
                    ReadValue <<= 8;
                    ReadValue |=
                        DCache[Set][HitWay]
                        .Data[BlockOffset + i];
                }
                break;
            }
            *LoadResult = ReadValue;
        }
        else
        {
            switch (DataSize)
            {
            case 1:
                DCache[Set][HitWay]
                    .Data[BlockOffset]
                    = StoreValue & 0xFF;
                break;
            case 2:
                BlockOffset &= 0xFE;
                for (int i = 0; i < 2; i++)
                {
                    DCache[Set][HitWay]
                        .Data[BlockOffset + i]
                        = StoreValue & 0xFF;
                    StoreValue >>= 8;
                }
                break;
            case 4:
                BlockOffset &= 0xFC;
                for (int i = 0; i < 4; i++)
                {
                    DCache[Set][HitWay]
                        .Data[BlockOffset + i]
                        = StoreValue & 0xFF;
                    StoreValue >>= 8;
                }
                break;
            case 8:
                BlockOffset &= 0xF8;
                for (int i = 0; i < 8; i++)
                {
                    DCache[Set][HitWay]
                        .Data[BlockOffset + i]
                        = StoreValue & 0xFF;
                    StoreValue >>= 8;
                }
                break;
            }
            DCache[Set][HitWay].Dirty = 1;
        }
        return MissFlag;
    }
    /*
        MISS
    */
    UINT32 VictimWay =
        FindVictimWay(Set);
    /*
        Write back if dirty
    */
    if (DCache[Set][VictimWay].Valid &&
        DCache[Set][VictimWay].Dirty)
    {
        UINT64 OldAddress;
        OldAddress =
            (
                (DCache[Set][VictimWay].Tag
                << DCACHE_SET_ADDR_BITS)
                << DCACHE_DATA_PER_LINE_ADDR_BITS
            )
            |
            (
                (UINT64)Set
                << DCACHE_DATA_PER_LINE_ADDR_BITS
            );
        StoreDataCacheLineToMemory(
            OldAddress,
            Set,
            VictimWay
        );
    }
    /*
        Load new line
    */
    LoadDataCacheLineFromMemory(
        Address,
        Set,
        VictimWay
    );
    DCache[Set][VictimWay].Valid = 1;
    DCache[Set][VictimWay].Dirty = 0;
    DCache[Set][VictimWay].Tag = Tag;
    // === 修改开始：替换UpdateLRU为UpdateClock ===
    UpdateClock(Set, VictimWay);
    // === 修改结束 ===
    /*
        Store after miss
    */
    if (Operation == 'S' ||
        Operation == 'M')
    {
        switch (DataSize)
        {
        case 1:
            DCache[Set][VictimWay]
                .Data[BlockOffset]
                = StoreValue & 0xFF;
            break;
        case 2:
            BlockOffset &= 0xFE;
            for (int i = 0; i < 2; i++)
            {
                DCache[Set][VictimWay]
                    .Data[BlockOffset + i]
                    = StoreValue & 0xFF;
                StoreValue >>= 8;
            }
            break;
        case 4:
            BlockOffset &= 0xFC;
            for (int i = 0; i < 4; i++)
            {
                DCache[Set][VictimWay]
                    .Data[BlockOffset + i]
                    = StoreValue & 0xFF;
                StoreValue >>= 8;
            }
            break;
        case 8:
            BlockOffset &= 0xF8;
            for (int i = 0; i < 8; i++)
            {
                DCache[Set][VictimWay]
                    .Data[BlockOffset + i]
                    = StoreValue & 0xFF;
                StoreValue >>= 8;
            }
            break;
        }
        DCache[Set][VictimWay].Dirty = 1;
    }
    return MissFlag;
}

/* 指令Cache实现部分，可选实现 */
#define ICACHE_SIZE						16384
#define ICACHE_DATA_PER_LINE			64
#define ICACHE_DATA_PER_LINE_ADDR_BITS	GET_POWER_OF_2(ICACHE_DATA_PER_LINE)
#define ICACHE_SET						(ICACHE_SIZE / ICACHE_DATA_PER_LINE)
#define ICACHE_SET_ADDR_BITS			GET_POWER_OF_2(ICACHE_SET)
struct ICACHE_LineStruct
{
	UINT8	Valid;
	UINT64	Tag;
	UINT8	Data[ICACHE_DATA_PER_LINE];
} ICache[ICACHE_SET];
void InitInstCache(void)
{
	UINT32 i;
	for (i = 0; i < ICACHE_SET; i++)
	{
		ICache[i].Valid = 0;
	}
}
void LoadInstCacheLineFromMemory(UINT64 Address,UINT32 CacheLineAddress)
{
	UINT32 i;
	UINT64 AlignAddress;
	UINT64* pp;
	AlignAddress =
		Address &
		~(ICACHE_DATA_PER_LINE - 1);
	pp = (UINT64*)ICache[CacheLineAddress].Data;
	for (i = 0;
		i < ICACHE_DATA_PER_LINE / 8;
		i++)
	{
		pp[i] =
			ReadMemory(
				AlignAddress + i * 8LL
			);
	}
}
UINT8 AccessInstCache(
	UINT64 Address,
	UINT8 Operation,
	UINT8 InstSize,
	UINT64* InstResult
)
{
	UINT32 CacheLineAddress;
	UINT8 BlockOffset;
	UINT64 AddressTag;
	UINT64 ReadValue = 0;
	UINT8 MissFlag = 'M';
	CacheLineAddress =
		(Address >> ICACHE_DATA_PER_LINE_ADDR_BITS)
		% ICACHE_SET;
	BlockOffset =
		Address % ICACHE_DATA_PER_LINE;
	AddressTag =
		(Address >> ICACHE_DATA_PER_LINE_ADDR_BITS)
		>> ICACHE_SET_ADDR_BITS;
	/*
		Check Hit
	*/
	if (ICache[CacheLineAddress].Valid &&
		ICache[CacheLineAddress].Tag == AddressTag)
	{
		MissFlag = 'H';
	}
	else
	{
		MissFlag = 'M';
		LoadInstCacheLineFromMemory(
			Address,
			CacheLineAddress
		);
		ICache[CacheLineAddress].Valid = 1;
		ICache[CacheLineAddress].Tag = AddressTag;
	}
	/*
		Read instruction
	*/
	switch (InstSize)
	{
	case 1:
		ReadValue =
			ICache[CacheLineAddress]
			.Data[BlockOffset];
		break;
	case 2:
		BlockOffset &= 0xFE;
		ReadValue =
			ICache[CacheLineAddress]
			.Data[BlockOffset + 1];
		ReadValue <<= 8;
		ReadValue |=
			ICache[CacheLineAddress]
			.Data[BlockOffset + 0];
		break;
	case 4:
		BlockOffset &= 0xFC;
		for (int i = 3; i >= 0; i--)
		{
			ReadValue <<= 8;
			ReadValue |=
				ICache[CacheLineAddress]
				.Data[BlockOffset + i];
		}
		break;
	case 8:
		BlockOffset &= 0xF8;
		for (int i = 7; i >= 0; i--)
		{
			ReadValue <<= 8;
			ReadValue |=
				ICache[CacheLineAddress]
				.Data[BlockOffset + i];
		}
		break;
	}
	*InstResult = ReadValue;
	return MissFlag;
}