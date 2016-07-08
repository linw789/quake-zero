#pragma once

#include <stdlib.h>

//=================================
// String related operations
//=================================


//=================================
// Memory Management
//=================================

// a wrapper of pointer to provide type safety
struct CacheUser
{
    void *data;
};

enum ALLocType 
{
    ZONE,
    LOWHUNK,
    TEMPHUNK,
    CACHE,
    TEMPSTACK
};

//=================================
// Dynamic variable tracking
//=================================


struct Cvar
{
    char *name;
    float val;
    // set true to save the variable before quitting
    I32 archive;
};


//=================================
// File system
//=================================

#define MAX_PACK_FILE_PATH 64
#define MAX_FILES_IN_PACK 2048
#define PAK0_FILE_NUM 339

/*
 PAK file in memory
 pack_header | file_data0 | file_data1 | ... | packfile_header0 | packfile_header1 | ...
*/
struct PackFile
{
    char name[MAX_PACK_FILE_PATH];
    int filePosition;
    int fileLength;
};

struct PackHeader
{
    char filepath[MAX_OS_PATH_LENGTH];
    int handle;
    int numfiles;
    PackFile *files; // array of pack files
};

struct SearchPath
{
    char filename[MAX_OS_PATH_LENGTH];
    PackHeader *pack;
    SearchPath *next;
};

