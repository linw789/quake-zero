#include "..\code\q_platform.h"
#include "..\code\q_common.cpp"
#include "..\code\q_math.h"

#include <stdio.h>
#include <stdarg.h>

SYS_ERROR(CmdError)
{
    char error[1024];
    va_list vl;
    va_start(vl, format);
    vsprintf_s(error, 1024, format, vl);
    va_end(vl);

    printf("Error: ");
    printf(error);
}

I32 g_errorCount = 0;
#define ERROR(condition) if (!(condition)) { \
    ++g_errorCount; \
    printf("!!! Error at line %d\n", __LINE__); \
}

void test_StringLength()
{
    char *testStr = "";
    I32 length = StringLength(testStr);
    ERROR(length == 0);

    // error: testStr = '\0'; assigning 0 to Str
    testStr = "\0";
    length = StringLength(testStr);
    ERROR(length == 0);

    testStr = "quake remake\n \r";
    length = StringLength(testStr);
    ERROR(length == 15);
}

void test_StringCompare()
{
    char *str1 = "test string 1";
    char *str2 = "test string 2";
    
    I32 result = StringCompare(str1, str2);
    ERROR(result == -1);

    result = StringNCompare(str1, str2, 12);
    ERROR(result == 0);

    result = StringNCompare(str1, str2, 0);
    ERROR(result == 0);

    str1 = "";

    result = StringCompare(str1, str2);
    ERROR(result == -1);

    str2 = "";

    result = StringCompare(str1, str2);
    ERROR(result == 0);

    str1 = "I am, indeed, a king,";

    result = StringCompare(str1, "I am, indeed, a king,");
    ERROR(result == 0);
}

void test_StringCopy()
{
    char *src = "normal string";
    char dest[128];

    I32 result = StringCopy(dest, 128, src, 0);
    ERROR(result == 13);
    ERROR(StringCompare(dest, src) == 0);

    result = StringCopy(dest, 128, src, 6);
    ERROR(result == 6);
    ERROR(StringCompare(dest, "normal") == 0);

    result = StringCopy(dest, 3, src, 128);
    ERROR(result == 2);
    ERROR(StringCompare(dest, "no") == 0);

    result = StringCopy(dest, 128, "", 128);
    ERROR(result == 0);
    ERROR(StringCompare(dest, "") == 0);
}

void test_IntToString()
{
    char str[128];
    IntToString(1234, str, 128);
    ERROR(StringCompare(str, "1234") == 0);

    IntToString(123456, str, 3);
    ERROR(StringCompare(str, "56") == 0);

    IntToString(-12223, str, 128);
    ERROR(StringCompare(str, "-12223") == 0);

    IntToString(0, str, 128);
    ERROR(StringCompare(str, "0") == 0);
}

void test_StringToInt()
{
    I32 result = StringToInt("9999");
    ERROR(result == 9999);

    result = StringToInt("-12345");
    ERROR(result == -12345);

    result = StringToInt("+9900");
    ERROR(result == 9900);

    result = StringToInt("d12345");
    ERROR(result == 0);

    result = StringToInt("324dx34");
    ERROR(result == 324);

    result = StringToInt("00320");
    ERROR(result == 320);
}

void test_MemSet()
{
    // U8 dest[128] = {7}; will only initialize the dest[0] to 7, the rest 
    // will still be 0
    U8 dest[128] = {0};
    for (I32 i = 0; i < 128; ++i)
    {
        dest[i] = 7;
    }
    dest[0] = 5;
    dest[1] = 11;
    dest[2] = 13;
    dest[3] = 17;

    ERROR(((size_t)dest & 3) == 0);

    MemSet(dest, 2, 1);
    ERROR(dest[0] == 2);

    dest[0] = 5;
    MemSet(dest + 1, 2, 4);
    for (I32 i = 1; i < 5; ++i) 
    {
        dest[i] = 2;
    }

    MemSet(dest, 2, 128);
    for (I32 i = 0; i < 128; ++i)
    {
        ERROR(dest[i] == 2);
    }
}

void test_MemCpy()
{
    U8 dest[128] = {0};
    for (I32 i = 0; i < 128; ++i)
    {
        dest[i] = 3;
    }

    U8 src[128] = {0};
    for (I32 i = 0; i < 128; ++i)
    {
        src[i] = 5;
    }

    MemCpy(dest + 1, src, 4);
    ERROR(dest[0] == 3);
    for (I32 i = 1; i < 5; ++i)
    {
        ERROR(dest[i] == 5);
    }

    for (I32 i = 1; i < 5; ++i)
    {
        src[i] = 7;
    }
    MemCpy(dest, src + 1, 4);
    for (I32 i = 0; i < 4; ++i)
    {
        src[i] = 7;
    }

    for (I32 i = 0; i < 4; ++i)
    {
        src[i] = 11;
    }
    MemCpy(dest, src, 128);
    for (I32 i = 0; i < 4; ++i)
    {
        ERROR(dest[i] == 11);
    }
}

// re-arrange data order in struct to reduce padding size
void test_DataSize()
{
    ERROR(sizeof(MemoryBlock) == 32);
    ERROR(sizeof(MemoryZone) == 48);
    ERROR(sizeof(HunkHeader) == 24);
    ERROR(sizeof(CacheHeader) == 64);
    ERROR(sizeof(PackFile) == MAX_PACK_FILE_PATH + 8);
    ERROR(sizeof(PackHeader) == MAX_OS_PATH_LENGTH + 16);
    ERROR(sizeof(PackFileDisk) == 64);
    ERROR(sizeof(PackHeaderDisk) == 12);
    ERROR(sizeof(SearchPath) == MAX_OS_PATH_LENGTH + 16);
}

#define POOL_SIZE 16 * 1204 * 1024
U8 pool[POOL_SIZE];

void test_MemoryAlloc()
{
    MemoryInit((void *)pool, POOL_SIZE);

    ERROR(g_hunk_base == pool);
    ERROR(g_hunk_total_size == POOL_SIZE);
    I32 size = Align16(DYNAMIC_ZONE_SIZE + sizeof(HunkHeader));
    ERROR(g_hunk_low_used == size);
    ERROR(g_hunk_high_used == 0);

    ERROR((U8 *)g_main_zone == pool + sizeof(HunkHeader));
    ERROR(g_main_zone->rover == (MemoryBlock *)(g_main_zone + 1));
    ERROR(g_main_zone->size == DYNAMIC_ZONE_SIZE);

    // ====================
    // test hunk allocation
    // ====================

    void *hunk0 = HunkLowAlloc(1231, "hunk0");
    HunkHeader *hunk0Header = (HunkHeader *)hunk0 - 1;
    I32 hunk0Size = Align16(1231 + sizeof(HunkHeader));
    ERROR(hunk0Header->size == hunk0Size);
    ERROR(hunk0Header->sentinel == HUNK_SENTINEL);
    ERROR(StringCompare(hunk0Header->name, "hunk0") == 0);
    ERROR(g_hunk_low_used == size + hunk0Size);

    void *hunk1 = HunkHighAlloc(3211, "hunk1");
    HunkHeader *hunk1Header = (HunkHeader *)hunk1 - 1;
    I32 hunk1Size = Align16(3211 + sizeof(HunkHeader));
    ERROR(hunk0Header->size == hunk0Size);
    ERROR(hunk0Header->sentinel == HUNK_SENTINEL);
    ERROR(StringCompare(hunk1Header->name, "hunk1") == 0);
    ERROR(g_hunk_high_used == hunk1Size);

    // ====================
    // test zone allocation
    // ====================

    void *zone0 = ZoneTagMalloc(234, 8);
    MemoryBlock *zone0Block = (MemoryBlock *)zone0 - 1;
    I32 zone0Size = Align8(234 + sizeof(MemoryBlock) + 4);
    ERROR(zone0Block->size == zone0Size);
    ERROR(zone0Block->tag == 8);
    ERROR(zone0Block->id == ZONE_ID);
    ERROR(zone0Block->prev == &g_main_zone->tailhead);
    MemoryBlock *freeBlock = zone0Block->next;
    ERROR(freeBlock->tag == 0);

    void *zone1 = ZoneTagMalloc(324, 1);
    void *zone2 = ZoneTagMalloc(432, 2);
    void *zone3 = ZoneTagMalloc(223, 3);
    void *zone4 = ZoneTagMalloc(333, 4);

    ZoneFree(zone1);
    MemoryBlock *zone1Block = (MemoryBlock *)zone1 - 1;
    I32 zone1Size = Align8(324 + sizeof(MemoryBlock) + 4);
    ERROR(zone1Block->tag == 0);
    ERROR(zone1Block->size == zone1Size);
    ERROR(zone1Block->id == ZONE_ID);
    ERROR(zone1Block->prev == (MemoryBlock *)zone0 - 1);
    ERROR(zone1Block->next == (MemoryBlock *)zone2 - 1);

    ZoneFree(zone3);
    MemoryBlock *zone3Block = (MemoryBlock *)zone3 - 1;
    I32 zone3Size = Align8(223 + sizeof(MemoryBlock) + 4);
    ERROR(zone3Block->tag == 0);
    ERROR(zone3Block->size == zone3Size);
    ERROR(zone3Block->id == ZONE_ID);
    ERROR(zone3Block->prev == (MemoryBlock *)zone2 - 1);
    ERROR(zone3Block->next == (MemoryBlock *)zone4 - 1);
    
    ZoneFree(zone2);
    I32 zone2Size = Align8(432 + sizeof(MemoryBlock) + 4);
    ERROR(zone1Block->tag == 0);
    ERROR(zone1Block->size == zone1Size + zone2Size + zone3Size);
    ERROR(zone1Block->prev == (MemoryBlock *)zone0 - 1);
    ERROR(zone1Block->next == (MemoryBlock *)zone4 - 1);

    void *tooBig = ZoneTagMalloc(DYNAMIC_ZONE_SIZE * 2, 22);
    ERROR(tooBig == NULL);

    ZoneClearAll(g_main_zone);
    ERROR(g_main_zone->tailhead.next == g_main_zone->rover);
    ERROR(g_main_zone->tailhead.prev == g_main_zone->rover);
    ERROR(g_main_zone->rover->next == &g_main_zone->tailhead);
    ERROR(g_main_zone->rover->next == &g_main_zone->tailhead);
}

void tests()
{
    test_StringLength();
    test_StringCompare();
    test_StringCopy();
    // TODO lw: test_CatString();
    test_IntToString();
    test_StringToInt();

    test_MemSet();
    test_MemCpy();

    test_DataSize();

    test_MemoryAlloc();

    if (g_errorCount == 0)
    {
        printf("All tests succeeded.\n");
    }
}

void demo_ParseCommand(char *line, char *verb, char *noun)
{
    if (!line)
    {
        return ;
    }

    while (*line == ' ')
    {
        line++;
        continue;
    }

    while (*line != '\n' && *line != '\0')
    {
        if (*line == ' ')
        {
            break;
        }
        else
        {
            *verb = *line;
            line++;
            verb++;
        }
    }
    *verb = '\0';

    while (*line != '\n' && *line != '\0')
    {
        if (*line == ' ')
        {
            line++;
            continue;
        }
        else
        {
            *noun = *line;
            line++;
            noun++;
        }
    }
    *noun = '\0';

	return;
}

I32 demo_EndFreeSizeForCache()
{
    I32 size = g_hunk_total_size - g_hunk_low_used - g_hunk_high_used;
    CacheHeader *first = g_cache_head.next;
    CacheHeader *last = g_cache_head.prev;
    I32 cacheUsed = (I32)((U8 *)last - (U8 *)first) + last->size;
    size = size - cacheUsed;
    return size;
}

void demo_drawCacheList()
{
    CacheHeader *ch = g_cache_head.next;

    if (ch == &g_cache_head)
    {
        I32 endFreeSize = demo_EndFreeSizeForCache();
        printf("[end:%d]", endFreeSize);
        return ;
    }

    while (ch != &g_cache_head)
    {
        printf("[%s:%d]->", ch->name, ch->size);
        
        if (ch->next != &g_cache_head)
        {
            I32 holeSize = (I32)((U8 *)ch->next - (U8 *)ch - ch->size);
            if (holeSize > 0)
            {
                printf("[hole:%d]->", holeSize);
            }
        }
        else
        {
            I32 endFreeSize = demo_EndFreeSizeForCache();
            printf("[end:%d]", endFreeSize);
        }

        ch = ch->next;
    }

    printf("\n");
}

void demo_DrawLRUList()
{
    printf("LRU list: ");
    CacheHeader *ch = g_cache_head.lru_next;
    while (ch != &g_cache_head)
    {
        printf("%s", ch->name);
        if (ch->lru_next != &g_cache_head)
        {
            printf("->");
        }
        ch = ch->lru_next;
    }
    printf("\n\n");
}

void demo_CacheAlloc()
{
    // leave 1024 bytes for cache allocation
    g_hunk_low_used = g_hunk_total_size - g_hunk_high_used - 1024;

    char cmdLine[32];
    char arg0[16];
    char arg1[16];
    char userCountStr[16];
    CacheUser cacheUser[128] = {0};
    I32 cacheUserCount = 0;

    printf("Cache Alloc Demo Starts ... \n\n");
    printf("Available cache size: %d bytes\n", demo_EndFreeSizeForCache());
    printf("Allocation size includes the size of cache header which is 64 bytes and will be 16-byte aligned\n");
    printf("[allocation count | hole | end: memory size]\n");
    printf("hole and end are free memory blocks\n");
    printf("LRU list: most recent used -> least recent used\n");
    printf("type \"alloc\" and a number to allocate cache or \"exit\" to end the demo\n\n");

    bool running = true;
    while (running)
    {
        if (fgets(cmdLine, 32, stdin) != cmdLine)
        {
            continue ;
        }

        demo_ParseCommand(cmdLine, arg0, arg1);

        if (StringCompare(arg0, "alloc") == 0)
        {
            cacheUserCount++;

            if (cacheUserCount >= 128)
            {
                printf("Exceed maximum times of allocation!");
                break ;
            }

            I32 size = StringToInt(arg1);
            IntToString(cacheUserCount, userCountStr, 16);

            CacheAlloc(cacheUser + cacheUserCount, size, userCountStr);

            demo_drawCacheList();
            demo_DrawLRUList();
        }
        else if (StringCompare(arg0, "exit") == 0)
        {
            running = false;
        }
        else
        {
            printf("unrecognized command\n");
        }
    }

    printf("Cache Alloc Demo Ended.");
}

I32 main(I32 argc, char *argv[])
{
    g_platformAPI.SysError = CmdError;

    printf("Unit Tests Begin ... \n\n");

    tests();

    printf("\nUnit Tests End.\n\n\n");

    demo_CacheAlloc();
}
