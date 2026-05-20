///////////////////////////////////////////////////////////////////////
////  Optimized by Gemini: 8-Way Clock + LIP + Next-Line Prefetch    //
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

/* ==================================================================== */
/* Data Cache (8-Way 16KB)                        */
/* ==================================================================== */
#define DCACHE_SIZE                     16384
#define DCACHE_DATA_PER_LINE            64
#define DCACHE_DATA_PER_LINE_ADDR_BITS  GET_POWER_OF_2(DCACHE_DATA_PER_LINE)
#define DCACHE_WAY                      8    // 提升为 8-Way
#define DCACHE_SET                      (DCACHE_SIZE / DCACHE_DATA_PER_LINE / DCACHE_WAY)
#define DCACHE_SET_ADDR_BITS            GET_POWER_OF_2(DCACHE_SET)

struct DCACHE_LineStruct {
    UINT8  Valid;
    UINT8  Dirty;
    UINT64 Tag;
    UINT8  Use;
    UINT8  Data[DCACHE_DATA_PER_LINE];
} DCache[DCACHE_SET][DCACHE_WAY];

UINT8 DCacheClockPtr[DCACHE_SET];

void InitDataCache() {
    printf("[%s] +-----------------------------------+\n", __func__);
    printf("[%s] |      8-Way Data Cache Init        |\n", __func__);
    printf("[%s] |      Clock + LIP + Prefetch       |\n", __func__);
    printf("[%s] +-----------------------------------+\n", __func__);
    for (UINT32 set = 0; set < DCACHE_SET; set++) {
        DCacheClockPtr[set] = 0;
        for (UINT32 way = 0; way < DCACHE_WAY; way++) {
            DCache[set][way].Valid = 0;
            DCache[set][way].Dirty = 0;
            DCache[set][way].Use = 0;
        }
    }
}

void UpdateClock(UINT32 set, UINT32 hitway) {
    DCache[set][hitway].Use = 1; // 真正的Hit才赋予二次生命
}

UINT32 FindVictimWay(UINT32 set) {
    // 1. Invalid first
    for (UINT32 way = 0; way < DCACHE_WAY; way++) {
        if (DCache[set][way].Valid == 0) return way;
    }
    // 2. Clock replace
    while (1) {
        UINT32 current = DCacheClockPtr[set];
        if (DCache[set][current].Use == 0) {
            DCacheClockPtr[set] = (current + 1) % DCACHE_WAY;
            return current;
        } else {
            DCache[set][current].Use = 0;
            DCacheClockPtr[set] = (current + 1) % DCACHE_WAY;
        }
    }
}

void LoadDataCacheLineFromMemory(UINT64 Address, UINT32 Set, UINT32 Way) {
    UINT64 AlignAddress = Address & ~(DCACHE_DATA_PER_LINE - 1);
    UINT64* pp = (UINT64*)DCache[Set][Way].Data;
    for (UINT32 i = 0; i < DCACHE_DATA_PER_LINE / 8; i++) {
        pp[i] = ReadMemory(AlignAddress + 8LL * i);
    }
}

void StoreDataCacheLineToMemory(UINT64 Address, UINT32 Set, UINT32 Way) {
    UINT64 AlignAddress = Address & ~(DCACHE_DATA_PER_LINE - 1);
    UINT64* pp = (UINT64*)DCache[Set][Way].Data;
    for (UINT32 i = 0; i < DCACHE_DATA_PER_LINE / 8; i++) {
        WriteMemory(AlignAddress + 8LL * i, pp[i]);
    }
}

UINT8 AccessDataCache(UINT64 Address, UINT8 Operation, UINT8 DataSize, UINT64 StoreValue, UINT64* LoadResult) {
    UINT32 Set = (Address >> DCACHE_DATA_PER_LINE_ADDR_BITS) % DCACHE_SET;
    UINT8 BlockOffset = Address % DCACHE_DATA_PER_LINE;
    UINT64 Tag = (Address >> DCACHE_DATA_PER_LINE_ADDR_BITS) >> DCACHE_SET_ADDR_BITS;
    UINT64 ReadValue = 0;
    UINT8 MissFlag = 'M';
    *LoadResult = 0;

    // Check Hit
    UINT32 HitWay = 0xFFFFFFFF;
    for (UINT32 way = 0; way < DCACHE_WAY; way++) {
        if (DCache[Set][way].Valid && DCache[Set][way].Tag == Tag) {
            HitWay = way; MissFlag = 'H'; break;
        }
    }

    if (HitWay != 0xFFFFFFFF) {
        UpdateClock(Set, HitWay);
        if (Operation == 'L') {
            switch (DataSize) {
                case 1: ReadValue = DCache[Set][HitWay].Data[BlockOffset]; break;
                case 2: BlockOffset &= 0xFE; for (int i = 1; i >= 0; i--) { ReadValue <<= 8; ReadValue |= DCache[Set][HitWay].Data[BlockOffset + i]; } break;
                case 4: BlockOffset &= 0xFC; for (int i = 3; i >= 0; i--) { ReadValue <<= 8; ReadValue |= DCache[Set][HitWay].Data[BlockOffset + i]; } break;
                case 8: BlockOffset &= 0xF8; for (int i = 7; i >= 0; i--) { ReadValue <<= 8; ReadValue |= DCache[Set][HitWay].Data[BlockOffset + i]; } break;
            }
            *LoadResult = ReadValue;
        } else {
            switch (DataSize) {
                case 1: DCache[Set][HitWay].Data[BlockOffset] = StoreValue & 0xFF; break;
                case 2: BlockOffset &= 0xFE; for (int i = 0; i < 2; i++) { DCache[Set][HitWay].Data[BlockOffset + i] = StoreValue & 0xFF; StoreValue >>= 8; } break;
                case 4: BlockOffset &= 0xFC; for (int i = 0; i < 4; i++) { DCache[Set][HitWay].Data[BlockOffset + i] = StoreValue & 0xFF; StoreValue >>= 8; } break;
                case 8: BlockOffset &= 0xF8; for (int i = 0; i < 8; i++) { DCache[Set][HitWay].Data[BlockOffset + i] = StoreValue & 0xFF; StoreValue >>= 8; } break;
            }
            DCache[Set][HitWay].Dirty = 1;
        }
        return MissFlag;
    }

    // MISS 逻辑
    UINT32 VictimWay = FindVictimWay(Set);
    if (DCache[Set][VictimWay].Valid && DCache[Set][VictimWay].Dirty) {
        UINT64 OldAddress = ((DCache[Set][VictimWay].Tag << DCACHE_SET_ADDR_BITS) << DCACHE_DATA_PER_LINE_ADDR_BITS) | ((UINT64)Set << DCACHE_DATA_PER_LINE_ADDR_BITS);
        StoreDataCacheLineToMemory(OldAddress, Set, VictimWay);
    }
    
    LoadDataCacheLineFromMemory(Address, Set, VictimWay);
    DCache[Set][VictimWay].Valid = 1;
    DCache[Set][VictimWay].Dirty = 0;
    DCache[Set][VictimWay].Tag = Tag;
    DCache[Set][VictimWay].Use = 0; // LIP策略：新装入块Use=0

    if (Operation == 'S' || Operation == 'M') {
        switch (DataSize) {
            case 1: DCache[Set][VictimWay].Data[BlockOffset] = StoreValue & 0xFF; break;
            case 2: BlockOffset &= 0xFE; for (int i = 0; i < 2; i++) { DCache[Set][VictimWay].Data[BlockOffset + i] = StoreValue & 0xFF; StoreValue >>= 8; } break;
            case 4: BlockOffset &= 0xFC; for (int i = 0; i < 4; i++) { DCache[Set][VictimWay].Data[BlockOffset + i] = StoreValue & 0xFF; StoreValue >>= 8; } break;
            case 8: BlockOffset &= 0xF8; for (int i = 0; i < 8; i++) { DCache[Set][VictimWay].Data[BlockOffset + i] = StoreValue & 0xFF; StoreValue >>= 8; } break;
        }
        DCache[Set][VictimWay].Dirty = 1;
    }

    // 硬件级预取 (Prefetch Next Line)
    UINT64 P_Addr = Address + DCACHE_DATA_PER_LINE;
    UINT32 P_Set = (P_Addr >> DCACHE_DATA_PER_LINE_ADDR_BITS) % DCACHE_SET;
    UINT64 P_Tag = (P_Addr >> DCACHE_DATA_PER_LINE_ADDR_BITS) >> DCACHE_SET_ADDR_BITS;
    
    UINT8 P_Hit = 0;
    for (UINT32 w = 0; w < DCACHE_WAY; w++) {
        if (DCache[P_Set][w].Valid && DCache[P_Set][w].Tag == P_Tag) {
            P_Hit = 1; break;
        }
    }
    
    if (!P_Hit) {
        UINT32 P_Way = FindVictimWay(P_Set);
        if (DCache[P_Set][P_Way].Valid && DCache[P_Set][P_Way].Dirty) {
            UINT64 OldPAddr = ((DCache[P_Set][P_Way].Tag << DCACHE_SET_ADDR_BITS) << DCACHE_DATA_PER_LINE_ADDR_BITS) | ((UINT64)P_Set << DCACHE_DATA_PER_LINE_ADDR_BITS);
            StoreDataCacheLineToMemory(OldPAddr, P_Set, P_Way);
        }
        LoadDataCacheLineFromMemory(P_Addr, P_Set, P_Way);
        DCache[P_Set][P_Way].Valid = 1;
        DCache[P_Set][P_Way].Dirty = 0;
        DCache[P_Set][P_Way].Tag = P_Tag;
        DCache[P_Set][P_Way].Use = 0; // 预取行保持Use=0，防止污染
    }

    return MissFlag;
}

/* ==================================================================== */
/* Inst Cache (8-Way 16KB)                        */
/* ==================================================================== */
#define ICACHE_SIZE                     16384
#define ICACHE_DATA_PER_LINE            64
#define ICACHE_DATA_PER_LINE_ADDR_BITS  GET_POWER_OF_2(ICACHE_DATA_PER_LINE)
#define ICACHE_WAY                      8    // 指令Cache同样提升为 8-Way
#define ICACHE_SET                      (ICACHE_SIZE / ICACHE_DATA_PER_LINE / ICACHE_WAY)
#define ICACHE_SET_ADDR_BITS            GET_POWER_OF_2(ICACHE_SET)

struct ICACHE_LineStruct {
    UINT8  Valid;
    UINT64 Tag;
    UINT8  Use;
    UINT8  Data[ICACHE_DATA_PER_LINE];
} ICache[ICACHE_SET][ICACHE_WAY];

UINT8 ICacheClockPtr[ICACHE_SET];

void InitInstCache(void) {
    for (UINT32 set = 0; set < ICACHE_SET; set++) {
        ICacheClockPtr[set] = 0;
        for (UINT32 way = 0; way < ICACHE_WAY; way++) {
            ICache[set][way].Valid = 0;
            ICache[set][way].Use = 0;
        }
    }
}

UINT32 FindInstVictimWay(UINT32 set) {
    for (UINT32 way = 0; way < ICACHE_WAY; way++) {
        if (ICache[set][way].Valid == 0) return way;
    }
    while (1) {
        UINT32 current = ICacheClockPtr[set];
        if (ICache[set][current].Use == 0) {
            ICacheClockPtr[set] = (current + 1) % ICACHE_WAY;
            return current;
        } else {
            ICache[set][current].Use = 0;
            ICacheClockPtr[set] = (current + 1) % ICACHE_WAY;
        }
    }
}

void LoadInstCacheLineFromMemory(UINT64 Address, UINT32 Set, UINT32 Way) {
    UINT64 AlignAddress = Address & ~(ICACHE_DATA_PER_LINE - 1);
    UINT64* pp = (UINT64*)ICache[Set][Way].Data;
    for (UINT32 i = 0; i < ICACHE_DATA_PER_LINE / 8; i++) {
        pp[i] = ReadMemory(AlignAddress + i * 8LL);
    }
}

UINT8 AccessInstCache(UINT64 Address, UINT8 Operation, UINT8 InstSize, UINT64* InstResult) {
    UINT32 Set = (Address >> ICACHE_DATA_PER_LINE_ADDR_BITS) % ICACHE_SET;
    UINT8 BlockOffset = Address % ICACHE_DATA_PER_LINE;
    UINT64 Tag = (Address >> ICACHE_DATA_PER_LINE_ADDR_BITS) >> ICACHE_SET_ADDR_BITS;
    UINT64 ReadValue = 0;
    UINT8 MissFlag = 'M';

    UINT32 HitWay = 0xFFFFFFFF;
    for (UINT32 way = 0; way < ICACHE_WAY; way++) {
        if (ICache[Set][way].Valid && ICache[Set][way].Tag == Tag) {
            HitWay = way; MissFlag = 'H'; break;
        }
    }

    if (HitWay != 0xFFFFFFFF) {
        ICache[Set][HitWay].Use = 1;
    } else {
        MissFlag = 'M';
        UINT32 VictimWay = FindInstVictimWay(Set);
        LoadInstCacheLineFromMemory(Address, Set, VictimWay);
        ICache[Set][VictimWay].Valid = 1;
        ICache[Set][VictimWay].Tag = Tag;
        ICache[Set][VictimWay].Use = 0; // LIP
        HitWay = VictimWay;

        // 指令Cache极其适合预取
        UINT64 P_Addr = Address + ICACHE_DATA_PER_LINE;
        UINT32 P_Set = (P_Addr >> ICACHE_DATA_PER_LINE_ADDR_BITS) % ICACHE_SET;
        UINT64 P_Tag = (P_Addr >> ICACHE_DATA_PER_LINE_ADDR_BITS) >> ICACHE_SET_ADDR_BITS;
        UINT8 P_Hit = 0;
        for (UINT32 w = 0; w < ICACHE_WAY; w++) {
            if (ICache[P_Set][w].Valid && ICache[P_Set][w].Tag == P_Tag) {
                P_Hit = 1; break;
            }
        }
        if (!P_Hit) {
            UINT32 P_Way = FindInstVictimWay(P_Set);
            LoadInstCacheLineFromMemory(P_Addr, P_Set, P_Way);
            ICache[P_Set][P_Way].Valid = 1;
            ICache[P_Set][P_Way].Tag = P_Tag;
            ICache[P_Set][P_Way].Use = 0;
        }
    }

    switch (InstSize) {
        case 1: ReadValue = ICache[Set][HitWay].Data[BlockOffset]; break;
        case 2: BlockOffset &= 0xFE; ReadValue = ICache[Set][HitWay].Data[BlockOffset + 1]; ReadValue <<= 8; ReadValue |= ICache[Set][HitWay].Data[BlockOffset + 0]; break;
        case 4: BlockOffset &= 0xFC; for (int i = 3; i >= 0; i--) { ReadValue <<= 8; ReadValue |= ICache[Set][HitWay].Data[BlockOffset + i]; } break;
        case 8: BlockOffset &= 0xF8; for (int i = 7; i >= 0; i--) { ReadValue <<= 8; ReadValue |= ICache[Set][HitWay].Data[BlockOffset + i]; } break;
    }
    *InstResult = ReadValue;
    return MissFlag;
}