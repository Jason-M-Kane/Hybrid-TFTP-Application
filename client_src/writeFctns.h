/****************************************************************************/
/* writeFctns.h - Prototypes for TFTP Client Write Functionality.           */
/****************************************************************************/
#ifndef WRITE_FUNCTIONS_H
#define WRITE_FUNCTIONS_H


/************/
/* Includes */
/************/
#include <windows.h>
#include "fileSystemType.h"


/***********/
/* Defines */
/***********/
/* Temp buffer size = (TEMP_BUFFER_INDIV_SIZE*TEMP_BUFFER_BACKLOG) * Num simultaneous connections */
#define TEMP_BUFFER_BACKLOG     20      /* # of backlog buffers per thread */
#define TEMP_BUFFER_INDIV_SIZE  65464   /* Size of Each temporary individual buffer */

#define WRITE_BUFFER_SIZE  (65464*512)    /* Size of the client's write buffer in memory */
                                          /* Must be less than 2GB or overlap of incomming packets may occur */
#define WRITE_SIZE         (64*1024)      /* Minimal amount of Data to write to disk     */


/* Tmp Storage Linked List */
typedef struct
{
    int size;
    int valid;
    unsigned short blockNum;
}tmpStorageType;

/* Write Datastructure */
typedef struct
{
    int numThreads;

    /* Shared Temporary Storage Variables */
    char* tempStorageArray;
    tmpStorageType* storageInfoArray;
    int sizePerStorageElement;
    int totalNumStorageElements;
    int numStorageElementsUsed;
    int resume;
    unsigned short lastCopiedBlocknum; //init to 0

    /* Shared Write Buffer Variables */
    char fnameDst[MAX_PATH+1];
    char* wbuf;
    char* wBufferHead;
    char* wBufferTail;
    char* freeSpacePtr;
    char* writePtr;
    unsigned int currentWbufSize; /* Shared between temp storage process and write process */

    /* Shared Semaphores */
    HANDLE h_TempStorage;   /* Handle to temp storage variables */
    HANDLE h_WriteVars;     /* Handle to shared write variables */
    HANDLE h_WriteFail;     /* Handle to write fail semaphore */
    HANDLE h_WriteDone;     /* When given indicates writing is complete. */
    HANDLE h_TFTPDone;      /* Handle to TFTP Done semaphore */
    HANDLE* h_ThreadHandle; /* Array of Handles to each hybrid TFTP Thread */
    DWORD* threadexitVal;   /* Array of exit thread values */
}writeInfo;



/**************/
/* Prototypes */
/**************/
int initStorageElements(writeInfo* writeInformation, queueItem* queuedItemToDownload);
DWORD WINAPI writeToDisk(LPVOID lpParam);
int addToWriteBuffer(unsigned short blockNumber, char* data, int size,
                     writeInfo* ptrWInfo);
void releaseStorageElements(writeInfo* writeInformation);



#endif
