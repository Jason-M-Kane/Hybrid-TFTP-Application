/****************************************************************************/
/* writeFctns.cpp - Contains TFTP client write fuctions.                    */
/****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <windows.h>
#include <winsock.h>
#include "cmn_util.h"
#include "sockfctns.h"
#include "opcode_definitions.h"
#include "writeFctns.h"
#include "downloadStatus.h"



int G_TEMP_BUFFER_BACKLOG = 20;             /* # of backlog buffers per thread */
int G_WRITE_BUFFER_SIZE = (65464*512);      /* Size of the client's write buffer in memory */
                                            /* Must be less than 2GB or overlap of incomming packets may occur */
int G_WRITE_SIZE = (4*1024*1024);           /* Minimal amount of Data to write to disk     */


/***********************/
/* Function Prototypes */
/***********************/
int initStorageElements(writeInfo* writeInformation, queueItem* queuedItemToDownload);
DWORD WINAPI writeToDisk(LPVOID lpParam);
int addToWriteBuffer(unsigned short blockNumber, char* data, int size,
                     writeInfo* ptrWInfo);
void releaseStorageElements(writeInfo* writeInformation);




/*****************************************************************************/
/* initStorageElements - Function initializes disk write variables.          */
/* Returns:  0 on successful initialization, -1 on error.                    */
/*****************************************************************************/
int initStorageElements(writeInfo* writeInformation, 
                        queueItem* queuedItemToDownload)
{
    char errorString[200];
    int x;
    int rval = 0;

    /* Init to NULL */
    writeInformation->tempStorageArray = NULL;
    writeInformation->storageInfoArray = NULL;
    writeInformation->wbuf = NULL;
    writeInformation->h_TempStorage = NULL;
    writeInformation->h_WriteVars = NULL;
    writeInformation->h_WriteFail = NULL;
    writeInformation->h_WriteDone = NULL;
    writeInformation->h_TFTPDone = NULL;
    writeInformation->threadexitVal = NULL;
    writeInformation->h_ThreadHandle = NULL;

    writeInformation->numThreads = queuedItemToDownload->numConnectionsToUse;

    /* Allocate memory for the temporary storage buffer */
    writeInformation->tempStorageArray = (char*)malloc(TEMP_BUFFER_INDIV_SIZE*
        G_TEMP_BUFFER_BACKLOG*writeInformation->numThreads);

    /* Allocate memory for temp storage information keeping array */
    writeInformation->storageInfoArray = (tmpStorageType*)malloc((sizeof(tmpStorageType))*
        G_TEMP_BUFFER_BACKLOG*writeInformation->numThreads);

    /* Allocate memory for the write buffer */
    writeInformation->wbuf = (char*)malloc(G_WRITE_BUFFER_SIZE);

    /* Allocate memory for the thread handles */
    writeInformation->h_ThreadHandle = (HANDLE*)malloc((sizeof(HANDLE))* writeInformation->numThreads);

    /* Allocate memory for the exit thread values */
    writeInformation->threadexitVal = (DWORD*)malloc((sizeof(DWORD))* writeInformation->numThreads);

    /* Check that memory allocation succeeded */
    if(writeInformation->tempStorageArray == NULL)
    {
        sprintf(errorString,"Could not allocate Hybrid TFTP Storage Buffer");
        rval =  -1;
    }
    if(writeInformation->storageInfoArray == NULL)
    {
        sprintf(errorString,"Could not allocate Hybrid TFTP Storage Info Array");
        rval =  -1;
    }
    if(writeInformation->wbuf == NULL)
    {
        sprintf(errorString,"Could not allocate Hybrid TFTP Write Buffer");
        rval =  -1;
    }
    if(writeInformation->h_ThreadHandle == NULL)
    {
        sprintf(errorString,"Could not allocate thread handle array");
        rval =  -1;
    }
    if(writeInformation->threadexitVal == NULL)
    {
        sprintf(errorString,"Could not allocate Exit Value array");
        rval =  -1;
    }
    if(rval == -1)
    {
        releaseStorageElements(writeInformation);
        reportError("Allocation error in initStorageElements",0,0);
        return rval;
    }

    /* Init thread exit values */
    for(x = 0; x < writeInformation->numThreads; x++)
    {
        writeInformation->threadexitVal[x] = 99;
    }

    /* Init Temp Storage Variables */
    writeInformation->totalNumStorageElements = G_TEMP_BUFFER_BACKLOG*queuedItemToDownload->numConnectionsToUse;
    writeInformation->sizePerStorageElement = TEMP_BUFFER_INDIV_SIZE;
    if(queuedItemToDownload->status == RESUMING)
    {
        writeInformation->lastCopiedBlocknum = queuedItemToDownload->firstBlock-1;
        writeInformation->resume = TRUE;
    }
    else
    {
        writeInformation->lastCopiedBlocknum = 0; /* First block to save is block 1 */
        writeInformation->resume = FALSE;
    }
    for(x = 0; x < writeInformation->totalNumStorageElements; x++)
    {
        writeInformation->storageInfoArray[x].valid = 0;
    }

    /* Init Write buffer variables */
    strcpy(writeInformation->fnameDst,queuedItemToDownload->dLItem.localfullpathAndName);
    writeInformation->freeSpacePtr = writeInformation->writePtr = writeInformation->wBufferHead = writeInformation->wbuf;
    writeInformation->wBufferTail = writeInformation->wbuf + G_WRITE_BUFFER_SIZE - 1;
    writeInformation->currentWbufSize = 0;


    /****************************************/
    /* Initialize Shared Handles/Semaphores */
    /****************************************/
    writeInformation->h_TempStorage = CreateSemaphore(NULL,1,1,NULL);
    writeInformation->h_WriteVars = CreateSemaphore(NULL,1,1,NULL);
    writeInformation->h_WriteFail = CreateSemaphore(NULL,0,1,NULL);
    writeInformation->h_WriteDone = CreateSemaphore(NULL,0,1,NULL);
    writeInformation->h_TFTPDone = CreateSemaphore(NULL,0,1,NULL);
    
    if( (writeInformation->h_TempStorage == NULL) ||
        (writeInformation->h_WriteVars == NULL) ||
        (writeInformation->h_WriteFail == NULL) ||
        (writeInformation->h_WriteDone == NULL) ||
        (writeInformation->h_TFTPDone == NULL)
        )
    {
        rval = -1;
        releaseStorageElements(writeInformation);
        reportError("Semaphore Allocation error in initStorageElements",0,0);
        return rval;
    }

    for(x = 0; x< writeInformation->numThreads; x++)
    {   
        writeInformation->h_ThreadHandle[x] = NULL;
    }

    return rval;
}




/*****************************************************************************/
/* releaseStorageElements - Frees used up storage elements.                  */
/* Returns:  Void.                                                           */
/*****************************************************************************/
void releaseStorageElements(writeInfo* writeInformation)
{
    /* Free Allocated Memory */
    if(writeInformation->tempStorageArray != NULL)
        free(writeInformation->tempStorageArray);
    if(writeInformation->storageInfoArray != NULL)
        free(writeInformation->storageInfoArray);
    if(writeInformation->wbuf != NULL)
        free(writeInformation->wbuf);
    if(writeInformation->h_ThreadHandle != NULL)
    {
        int x;
        for(x=0; x < writeInformation->numThreads; x++)
        {
            if(writeInformation->h_ThreadHandle[x]!=NULL)
                CloseHandle(writeInformation->h_ThreadHandle[x]);
        }
        free(writeInformation->h_ThreadHandle);
    }
    if(writeInformation->threadexitVal != NULL)
        free(writeInformation->threadexitVal);
        

    /* Free Handles */
    CloseHandle(writeInformation->h_TempStorage);
    CloseHandle(writeInformation->h_WriteVars);
    CloseHandle(writeInformation->h_WriteFail);
    CloseHandle(writeInformation->h_WriteDone);
    CloseHandle(writeInformation->h_TFTPDone);

    /* Re-Init to NULL */
    writeInformation->tempStorageArray = NULL;
    writeInformation->storageInfoArray = NULL;
    writeInformation->wbuf = NULL;
    writeInformation->h_TempStorage = NULL;
    writeInformation->h_WriteVars = NULL;
    writeInformation->h_WriteFail = NULL;
    writeInformation->h_WriteDone = NULL;
    writeInformation->h_TFTPDone = NULL;
    writeInformation->h_ThreadHandle = NULL;
    writeInformation->threadexitVal = NULL;

    return;
}




/*****************************************************************************/
/* addToWriteBuffer - Function adds data to the circular write buffer.       */
/* Returns:  0 on successful addition to temp buffer or successful copy.     */
/*           -1 on failure or if the window has been exceeded.               */
/*           -2 if packet occurs prior to window or duplicate                */
/*****************************************************************************/
int addToWriteBuffer(unsigned short blockNumber, char* data, int size,
                     writeInfo* ptrWInfo)
{
    int x,y,numToShift,flag=0,localWbufSize = 0;
    int wrapBounds = FALSE;
    int duplicate = FALSE;
    int copyDataToWriteBuf = FALSE;
    int finalCheck, tempIndex;
    unsigned int remainingSpace, leftover, copySize, offset,numElementsCopied;
    unsigned short highBound, lowBound, tmpLastCopied, tmpOffset;
    long prev;
    tmpLastCopied = 9999;

    /* Wait to grab the temp storage semaphore */
    WaitForSingleObject(ptrWInfo->h_TempStorage,INFINITE);


    /***************************************************************************/
    /* Check to see if the item to be inserted is outside of our packet window */
    /* If so, just drop it.                                                    */
    /***************************************************************************/
    lowBound = ptrWInfo->lastCopiedBlocknum + 1;
    highBound = lowBound + ptrWInfo->totalNumStorageElements-1;
    if(highBound < lowBound)
        wrapBounds = TRUE;

    if(wrapBounds)
    {
        if( (blockNumber < lowBound) && (blockNumber > (lowBound-ptrWInfo->totalNumStorageElements*2)) )
        {
            /* Duplicate packet */
            ReleaseSemaphore(ptrWInfo->h_TempStorage,1,&prev);
            return -2;
        }
        else if( (blockNumber < lowBound) && (blockNumber > highBound) )
        {
            /* Out of bounds */
            ReleaseSemaphore(ptrWInfo->h_TempStorage,1,&prev);
            return -1;
        }

    }
    else if(blockNumber > highBound)
    {
        /* Out of bounds */
        ReleaseSemaphore(ptrWInfo->h_TempStorage,1,&prev);
        return -1;
    }
    else if(blockNumber < lowBound)
    {
        /* Assume duplicate packet */
        ReleaseSemaphore(ptrWInfo->h_TempStorage,1,&prev);
        return -2;
    }
    
    /***************************************************************************/
    /* The packet is within our window.  Insert it if it does not yet already  */
    /* exist.                                                                  */
    /***************************************************************************/
    tempIndex = ((unsigned short)blockNumber - (unsigned short)(ptrWInfo->lastCopiedBlocknum+1));
    if(wrapBounds)
    {
        if(blockNumber < ptrWInfo->lastCopiedBlocknum)
        {
            tmpOffset = (unsigned short)65535 - (unsigned short)ptrWInfo->lastCopiedBlocknum;
            tempIndex = (unsigned short)blockNumber + tmpOffset;
        }
    }

    if(ptrWInfo->storageInfoArray[tempIndex].valid == FALSE) /* ACCESS VIOLATION HERE WHEN DOING 4GB FILE */
    {
        /* Copy Data to temp storage */
        offset = tempIndex * ptrWInfo->sizePerStorageElement;
        memcpy(&ptrWInfo->tempStorageArray[offset],data,size);

        /* Update temp storage information */
        ptrWInfo->storageInfoArray[tempIndex].blockNum = blockNumber;
        ptrWInfo->storageInfoArray[tempIndex].valid = TRUE;
        ptrWInfo->storageInfoArray[tempIndex].size = size;
    }
    else
    {
        /* Its a duplicate packet! */
        ReleaseSemaphore(ptrWInfo->h_TempStorage,1,&prev);
        return -2;
    }


    /*************************************************************/
    /* Determine if data can be copied to the write buffer       */
    /* Need several sequential blocks of data to be available.   */
    /* This will minimize I/O transactions in memory.            */
    /*************************************************************/
    finalCheck = (G_TEMP_BUFFER_BACKLOG/2);
    if(finalCheck > ptrWInfo->totalNumStorageElements)
        finalCheck = ptrWInfo->totalNumStorageElements-1;
    if(finalCheck == 0)
        finalCheck = 1;
    copyDataToWriteBuf = FALSE;
    copySize = 0;
    for(x=0; x < finalCheck; x++)
    {
        if(!ptrWInfo->storageInfoArray[x].valid)
            break;
        else if(x == (finalCheck-1))
        {
            copyDataToWriteBuf = TRUE;
            copySize += ptrWInfo->storageInfoArray[x].size;
            tmpLastCopied = ptrWInfo->storageInfoArray[x].blockNum;
            ptrWInfo->storageInfoArray[x].size;
            break;
        }
        copySize += ptrWInfo->storageInfoArray[x].size;
    }

    /* Determine if this is potentially the final copy to the write buffer */
    /* If so, allow the data to be copied to the write buffer.             */
    if( (copyDataToWriteBuf == FALSE) && (ptrWInfo->storageInfoArray[x-1].size < 65464)
        && (copySize > 0) )
    {
        tmpLastCopied = ptrWInfo->storageInfoArray[x-1].blockNum;
        copyDataToWriteBuf = TRUE;
    }


    if(copyDataToWriteBuf)
    {
        /**********************************************************/
        /* Grab the current amount of data in the write buffer    */
        /* Wait for free buffer space to be available, or give up */
        /* after waiting for roughly 50ms.                        */
        /**********************************************************/
        WaitForSingleObject(ptrWInfo->h_WriteVars,INFINITE);
        localWbufSize = ptrWInfo->currentWbufSize;
        ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);

        /* Delay until the write buffer is available */
        while((copySize + localWbufSize) > (unsigned int)G_WRITE_BUFFER_SIZE)
        {
            /* Cant add the data, buffer full */

            /* Yield Execution */
			Sleep(0);
            if(flag == 0)
			{
				flag = 1;
#if DEBUG_TFTP
	            reportError("WARNING: Data buffer full",0,0);
#endif
			}

            WaitForSingleObject(ptrWInfo->h_WriteVars,INFINITE);
            localWbufSize = ptrWInfo->currentWbufSize;
            ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);

            /* Check to see if write thread is dead, if so, give up */
            if( WaitForSingleObject(ptrWInfo->h_WriteFail,0) == WAIT_OBJECT_0)
            {
                ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
                return -1;
            }
        }


        /***************************************************************/
        /* Free space is available in the Write Buffer if you get here */
        /***************************************************************/
       
        /* Copy the data to the write buffer.  Account for */
        /* a potential rolling over of the buffer.         */
        remainingSpace = (unsigned int)(ptrWInfo->wBufferTail - ptrWInfo->freeSpacePtr) + 1;
        if(remainingSpace >= (unsigned int)copySize)
        {
            memcpy(ptrWInfo->freeSpacePtr,ptrWInfo->tempStorageArray,copySize);
            ptrWInfo->freeSpacePtr += copySize;
            if(ptrWInfo->freeSpacePtr > ptrWInfo->wBufferTail)
            {
                ptrWInfo->freeSpacePtr = ptrWInfo->wBufferHead;
            }
        }
        else
        {
            memcpy(ptrWInfo->freeSpacePtr,ptrWInfo->tempStorageArray,remainingSpace);
            ptrWInfo->freeSpacePtr = ptrWInfo->wBufferHead;
            leftover = copySize-remainingSpace;
            memcpy(ptrWInfo->freeSpacePtr,&ptrWInfo->tempStorageArray[remainingSpace],leftover);
            ptrWInfo->freeSpacePtr += leftover;
        }

        /* Update the current amount of data in the write buffer */
        WaitForSingleObject(ptrWInfo->h_WriteVars,INFINITE);
        ptrWInfo->currentWbufSize += copySize;
        ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);

        /***********************************************************************/
        /* Temp Data was copied to the write buffer, shift remaining data down */
        /* Invalidate the old data at the end.                                 */
        /***********************************************************************/
        memmove(ptrWInfo->tempStorageArray,&ptrWInfo->tempStorageArray[copySize],
            ((ptrWInfo->sizePerStorageElement*ptrWInfo->totalNumStorageElements)-copySize));
        numElementsCopied = copySize / 65464;
        if((numElementsCopied * 65464) < copySize)
            numElementsCopied++;

        /* Shift down storage info */
        numToShift = ptrWInfo->totalNumStorageElements - numElementsCopied;
        y = 0;
        for(x = numElementsCopied; x < ptrWInfo->totalNumStorageElements; x++,y++)
        {
            ptrWInfo->storageInfoArray[y].valid = ptrWInfo->storageInfoArray[x].valid;
            ptrWInfo->storageInfoArray[y].size = ptrWInfo->storageInfoArray[x].size;
            ptrWInfo->storageInfoArray[y].blockNum = ptrWInfo->storageInfoArray[x].blockNum;
        }

        /* Erase old info */
        for(; y < ptrWInfo->totalNumStorageElements; y++)
        {
            ptrWInfo->storageInfoArray[y].valid = FALSE;
        }


        ptrWInfo->lastCopiedBlocknum = tmpLastCopied;
    }

    /* Release the Temp Storage Handle */
    ReleaseSemaphore(ptrWInfo->h_TempStorage,1,&prev);

    return 0;
}




/*****************************************************************************/
/* writeToDisk - Main write to disk thread.  Writes to disk in increments of */
/*               WRITE_SIZE from the circular buffer.                        */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
DWORD WINAPI writeToDisk(LPVOID lpParam)
{
    LARGE_INTEGER dist,newFP;
    HANDLE hOutFile = NULL;
    char tchar[200];
    char* localFname = NULL;
	writeInfo* ptrWInfo;
    char* scratchPad = NULL;
    unsigned int remainingSpace, leftover;
    long prev;
    int fail = FALSE;
    int rval;
    int localWbufSize = 0;
    unsigned int localWbufWritten;
    DWORD bytesWritten = 0;

    /* Decode LPVOID parameters */
    ptrWInfo = (writeInfo*)lpParam;

    /* Allocate memory for the scratch space */
    scratchPad = (char*)malloc(G_WRITE_SIZE);
    if(scratchPad == NULL)
    {
        ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
        reportError("ScratchPad Allocation error in writeToDisk",0,0);
        return -1;
    }

    /* Attempt to open local file for writing */
    hOutFile = INVALID_HANDLE_VALUE;
    if(ptrWInfo->resume == TRUE)
    {
        hOutFile = CreateFile(
		    ptrWInfo->fnameDst,         /* lpFileName      */
            GENERIC_WRITE,              /* dwDesiredAccess */
            0,                          /* dwShareMode */
            NULL,                       /* lpSecurityAttributes */
            OPEN_EXISTING,              /* dwCreationDisposition */
            FILE_FLAG_WRITE_THROUGH,    /* dwFlagsAndAttributes */
            NULL                        /* hTemplateFile */
            );

        if(hOutFile != NULL)
        {
            dist.QuadPart = 0;
            rval = SetFilePointerEx(
                hOutFile, /* HANDLE hFile, */
                dist,       /* LARGE_INTEGER lDistanceToMove, */
                &newFP,       /* PLARGE_INTEGER lpNewFilePointer, */
                FILE_END    /* DWORD dwMoveMethod */
            );
        }
    }
    else
    {
        hOutFile = CreateFile(
		    ptrWInfo->fnameDst,         /* lpFileName      */
            GENERIC_WRITE,              /* dwDesiredAccess */
            0,                          /* dwShareMode */
            NULL,                       /* lpSecurityAttributes */
            CREATE_ALWAYS,              /* dwCreationDisposition */
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,     /* dwFlagsAndAttributes */
            NULL                        /* hTemplateFile */
            );
    }
    if(hOutFile == INVALID_HANDLE_VALUE)
    {
        sprintf(tchar,"Error creating output file %s for writting in writeToDisk",ptrWInfo->fnameDst);
        reportError(tchar,0,0);
        ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
        CloseHandle(hOutFile);
        free(localFname);
		ExitThread(-1);
    }

    /**************************************************/
    /* Loop writting out WRITE_SIZE bytes to the file */
    /**************************************************/
    while(1)
    {
        /* Yield Execution */
        Sleep(0);

        /* Check to see if the transfer is done */
        if( WaitForSingleObject(ptrWInfo->h_TFTPDone,0) == WAIT_OBJECT_0 )
        {
            break;
        }

        /* See if there is data to be written to disk */
        WaitForSingleObject(ptrWInfo->h_WriteVars,INFINITE);
        localWbufSize = ptrWInfo->currentWbufSize;
        ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);

        localWbufWritten = 0;
        while(localWbufSize > G_WRITE_SIZE)
        {
            /* Check to see if the scratch pad will be needed */
            /* scratch pad used for wraparound case          */
            remainingSpace = (unsigned int)(ptrWInfo->wBufferTail - ptrWInfo->writePtr) + 1;
            if(remainingSpace >= (unsigned int)G_WRITE_SIZE)
            {
                /* Normal Case */
                if( WriteFile(
                    hOutFile,                  // open file handle
                    ptrWInfo->writePtr,        // start of data to write
                    G_WRITE_SIZE,                // number of bytes to write
                    &bytesWritten,             // number of bytes that were written
                    NULL) == FALSE)            // no overlapped structure
                {

                    sprintf(tchar,"Error writting to file (error %d)\n", GetLastError());
                    reportError(tchar,0,0);
                    fail = TRUE;
                    break;
                }

                /* Update write ptr and the size of the write buffer */
                localWbufSize -= bytesWritten;      /*Should be equal to WRITE_SIZE*/
                ptrWInfo->writePtr += bytesWritten; /*Should be equal to WRITE_SIZE*/
                if(ptrWInfo->writePtr > ptrWInfo->wBufferTail)
                    ptrWInfo->writePtr = ptrWInfo->wBufferHead;
            }
            else
            {
                /* Wraparound Case */
                memmove(scratchPad,ptrWInfo->writePtr,remainingSpace);
                ptrWInfo->writePtr = ptrWInfo->wBufferHead;
                leftover = G_WRITE_SIZE - remainingSpace;
                memmove(&scratchPad[remainingSpace],ptrWInfo->writePtr,leftover);
                ptrWInfo->writePtr += leftover;

                if( WriteFile(
                    hOutFile,                  // open file handle
                    scratchPad,                // start of data to write
                    G_WRITE_SIZE,                // number of bytes to write
                    &bytesWritten,             // number of bytes that were written
                    NULL) == FALSE)            // no overlapped structure
                {
                    sprintf(tchar,"Error writting to file (error %d)\n", GetLastError());
                    reportError(tchar,0,0);
                    fail = TRUE;
                    break;
                }
                localWbufSize -= bytesWritten; //added kane 12/2
            }

            localWbufWritten += bytesWritten;

            /* Update the size of the global write buffer while still looping */        
            WaitForSingleObject(ptrWInfo->h_WriteVars,INFINITE);
            ptrWInfo->currentWbufSize -= localWbufWritten;
            localWbufSize = ptrWInfo->currentWbufSize;
            ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);
            localWbufWritten = 0;
        }
        if(fail)
        {
            break;
        }   
    }
    CloseHandle(hOutFile);

    /* If failure, exit.  Otherwise write out remainder to disk. */
    if(fail)
    {
        ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
        CloseHandle(hOutFile);
        free(localFname);
		ExitThread(-1);
    }
    else
    {
        /* Attempt to open local file for writing */
        hOutFile = INVALID_HANDLE_VALUE;
        hOutFile = CreateFile(
            ptrWInfo->fnameDst,         /* lpFileName      */
            GENERIC_WRITE,              /* dwDesiredAccess */
            0,                          /* dwShareMode */
            NULL,                       /* lpSecurityAttributes */
            OPEN_EXISTING,              /* dwCreationDisposition */
            FILE_ATTRIBUTE_NORMAL,      /* dwFlagsAndAttributes */
            NULL                        /* hTemplateFile */
            );
        if(hOutFile == INVALID_HANDLE_VALUE)
        {
            reportError("File Error - Error creating output file for writting",0,0);
            ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
            CloseHandle(hOutFile);
            free(localFname);
			ExitThread(-1);
        }
       
        dist.QuadPart = 0;
        SetFilePointerEx(
            hOutFile, /* HANDLE hFile, */
            dist,       /* LARGE_INTEGER lDistanceToMove, */
            NULL,       /* PLARGE_INTEGER lpNewFilePointer, */
            FILE_END    /* DWORD dwMoveMethod */
        );

        WaitForSingleObject(ptrWInfo->h_WriteVars,INFINITE);

        /*************************************************/
        /* Write out the remaining data to disk, account */
        /* for buffer wraparound as needed.              */
        /*************************************************/
        remainingSpace = (unsigned int)(ptrWInfo->wBufferTail - ptrWInfo->writePtr) + 1;
        if(remainingSpace >= ptrWInfo->currentWbufSize)
        {
            /* Normal Case */
            if( WriteFile(
                hOutFile,                // open file handle
                ptrWInfo->writePtr,                // start of data to write
                ptrWInfo->currentWbufSize,         // number of bytes to write
                &bytesWritten,             // number of bytes that were written
                NULL) == FALSE)            // no overlapped structure
            {
                sprintf(tchar,"Error on final write to file (error %d)\n", GetLastError());
                reportError(tchar,0,0);
                ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
                ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);
                CloseHandle(hOutFile);
                free(localFname);
				ExitThread(-1);
            }
        }
        else
        {
            /* Wraparound Case */
            if( WriteFile(
                hOutFile,                  // open file handle
                ptrWInfo->writePtr,        // start of data to write
                remainingSpace,            // number of bytes to write
                &bytesWritten,             // number of bytes that were written
                NULL) == FALSE)            // no overlapped structure
            {
                sprintf(tchar,"Error on final write to file (error %d)\n", GetLastError());
                reportError(tchar,0,0);
                ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
                ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);
                CloseHandle(hOutFile);
                free(localFname);
				ExitThread(-1);
            }
            ptrWInfo->writePtr = ptrWInfo->wBufferHead;
            leftover = ptrWInfo->currentWbufSize - remainingSpace;

            if( WriteFile(
                hOutFile,                  // open file handle
                ptrWInfo->writePtr,        // start of data to write
                leftover,                  // number of bytes to write
                &bytesWritten,             // number of bytes that were written
                NULL) == FALSE)            // no overlapped structure
            {
                sprintf(tchar,"Error on final write to file (error %d)\n", GetLastError());
                reportError(tchar,0,0);
                ReleaseSemaphore(ptrWInfo->h_WriteFail,1,&prev);
                ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);
                CloseHandle(hOutFile);
                free(localFname);
				ExitThread(-1);
            }
        }

        ptrWInfo->currentWbufSize = 0;
        ptrWInfo->writePtr = NULL;
        ReleaseSemaphore(ptrWInfo->h_WriteVars,1,&prev);
    }

    /* Success */
    CloseHandle(hOutFile);
    ReleaseSemaphore(ptrWInfo->h_WriteDone,1,&prev);
	free(localFname);

    ExitThread(0);
}
