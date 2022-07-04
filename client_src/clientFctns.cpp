/****************************************************************************/
/* clientFctns.c - Contains TFTP client request/data transmission code.     */
/****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <windows.h>
#include <winsock.h>
#include "shlobj.h"  
#include "cmn_util.h"
#include "sockfctns.h"
#include "opcode_definitions.h"
#include "writeFctns.h"
#include "resource.h"
#include "fileSystemType.h"
#include "downloadStatus.h"
#include "dlQueueManagement.h"
#include "createMD5.h"


/***********/
/* Defines */
/***********/
#define DEFAULT_SEC_TIMEOUT     1   //Timeout = this + 100ms


/* Download Info Structure */
typedef struct
{
    SOCKET socket;
    struct sockaddr_in serverAddr;
	HANDLE* dlInfoSem;
    queueItem* queueInfo;
}dlInfo;


/*  Hybrid TFTP Thread Parameter Passing */
typedef struct
{
    int p2pMode;
    struct sockaddr_in p2pServerInfo;
    dlInfo dLinfo;
    writeInfo* ptrWriteInfo;
}htftpParamPassType;


/* External Globals */
extern char G_outputDir[MAX_PATH+1];
extern int G_TotalUsedConnections;  //# of used connections.  Should only decrement.
extern HWND G_hWnd;                 //Handle to dialog box
extern int G_P2PMode;               // Enables P2P mode when true




/***********************/
/* Function Prototypes */
/***********************/
DWORD WINAPI beginHybridDownload(LPVOID lpParam);
int createTFTPReqMsg(char* TFTPReqMsg, int bufSize, char* fileAndPath);
int createResumeTFTPReqMsg(char* TFTPReqMsg, int bufSize, char* fileAndPath,
                           unsigned __int64 bytesDownloaded);
int initiateTFTPReq(char* tftpBuf, int tftpBufSize,
                    SOCKET sock, struct sockaddr_in remote, unsigned short* port,
                    char* fileAndPath, writeInfo* ptrWinfo,
                    int status, unsigned short firstBlock,
                    unsigned __int64 fileOffset, int p2pMode, 
                    struct sockaddr_in* p2pRemote);
DWORD WINAPI startHybridTFTPThread(LPVOID lpParam);
int transferTFTPData(char* tftpBuf, int tftpBufSize,
                     SOCKET sock, struct sockaddr_in remote,
                     writeInfo* ptrWinfo, dlInfo* ptrDlInfo,
                     int p2pMode, struct sockaddr_in p2pRemote);
int parse_oack(struct sockaddr_in remote,struct sockaddr_in expected,char* buf, 
              int size, SOCKET sock);
int parse_data(struct sockaddr_in remote,struct sockaddr_in expected,char* buf, 
              int size, SOCKET sock, int chk_blocknum, writeInfo* ptrWinfo);
int validate_oack(char* oackMsg, int size);
void closeSockets(SOCKET* sockArray, int socketArraySize);




/*****************************************************************************/
/* beginHybridDownload - Handles a Hybrid TFTP Download Session.             */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
DWORD WINAPI beginHybridDownload(LPVOID lpParam)
{
    char errorString[300];
    int downloadSuccess = FALSE;
    htftpParamPassType* threadParameter = NULL;
    writeInfo writeInformation;
    queueItem* queuedItemToDownload = NULL;
    char* tftpBuffer = NULL;
    unsigned short port;                // Remote Server Port
    SOCKET* sfd;                        // Client UDP Socket Array
    struct sockaddr_in remoteSockAddr;	// Socket Structure
    long prev;
    int tftpBufSize, counter = 0;
    HANDLE hThread,H_Sem_dlInformation;
    DWORD dwThreadId;
	int x, numHybridTftpThreads,len,found;
	char* localFname;

    /* Gui static update related variables */
    unsigned __int64 bytesDownloaded, lastBytesLeft, currentSample,blockNum64;
    double rate, timeDouble;
    DWORD newElapsedTime, totalElapsedTime, firstTick, prevElapsedTime;

    numHybridTftpThreads = 0;

    /* Parse void input */
    queuedItemToDownload = (queueItem*)lpParam;
    lastBytesLeft = queuedItemToDownload->bytesRemain;
    sfd = &queuedItemToDownload->sockArray[0];

	/**********************************************************/
	/* Determine local filename and path if they do not exist */
	/**********************************************************/
    if(queuedItemToDownload->status != RESUMING)
    {
	    localFname = (char*)malloc(MAX_PATH+1);
	    if(localFname == NULL)
	    {
            reportError("Filename Allocation error in beginHybridDownload",0,0);
		    closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
            G_TotalUsedConnections -= queuedItemToDownload->numConnectionsToUse;
            queuedItemToDownload->status = DL_FAILED;
            updateLVItem(queuedItemToDownload);
            ExitThread(-1);
	    }
	    memset(localFname,0,MAX_PATH+1);

	    /* Create the local path and filename */
	    len = (int)strlen(queuedItemToDownload->dLItem.fullpathAndName);
	    x=0;
	    found = FALSE;
	    while(x < len)
	    {
		    if(queuedItemToDownload->dLItem.fullpathAndName[x] == '\\')
		    {
			    found = TRUE;
			    break;
		    }
		    x++;
	    }
	    x++;
	    if(!found)
		    x=0;

	    if( (strlen(&queuedItemToDownload->dLItem.fullpathAndName[x]) + strlen(G_outputDir) + 1) > MAX_PATH)
	    {
		    reportError("Error max path exceeded for filename.",0,0);
		    free(localFname);
		    closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
            G_TotalUsedConnections -= queuedItemToDownload->numConnectionsToUse;
            queuedItemToDownload->status = DL_FAILED;
            updateLVItem(queuedItemToDownload);
            ExitThread(-1);
	    }

	    strcpy(localFname,G_outputDir);
	    if(G_outputDir[strlen(G_outputDir)-1] != '\\')
		    strcat(localFname,"\\");
	    strcat(localFname,&queuedItemToDownload->dLItem.fullpathAndName[x]);
	    strcpy(queuedItemToDownload->dLItem.localfullpathAndName,localFname);

	    /* Create local path if it does not already exist */
	    createLocalPath(localFname);
	    free(localFname);
        queuedItemToDownload->firstBlock = 1;
    }
    else
    {
        /* We are resuming a partially downloaded file, determine */
        /* the first block to be downloaded.                      */
        blockNum64 = (queuedItemToDownload->fileOffset / 65464);
        blockNum64 = blockNum64 + 1;
        while(blockNum64 > 65535)
        {
            blockNum64 -= 65536;
        }
        queuedItemToDownload->firstBlock = (unsigned short)blockNum64;
    }

	/***********************************/
	/* Local Path and Filename Created */
	/***********************************/
   

    /* Malloc threadParameter structure */
    threadParameter = (htftpParamPassType*)malloc(queuedItemToDownload->numConnectionsToUse * sizeof(htftpParamPassType) );
    if(threadParameter == NULL)
    {
        sprintf(errorString,"Could not allocate Hybrid TFTP threadParameter structure");
        reportError(errorString,0,0);
        closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
        G_TotalUsedConnections -= queuedItemToDownload->numConnectionsToUse;
        if(queuedItemToDownload->status != RESUMING)
            queuedItemToDownload->status = DL_FAILED;
        else
            queuedItemToDownload->status = FAIL_PARTIAL_DL;
        updateLVItem(queuedItemToDownload);
        ExitThread(-1);
    }

    /***************************************/
    /* Set Up Remote Packet Structure      */
    /* Default TFTP Server Port = 69       */
    /***************************************/
    memset(&remoteSockAddr,0,sizeof(struct sockaddr_in));
    remoteSockAddr.sin_family = AF_INET;
    remoteSockAddr.sin_port   = htons(69 /*remote port of server - did i do this right?              STORE THIS IP ADDR and PORT FOR P2P? */);
    remoteSockAddr.sin_addr.s_addr = inet_addr(queuedItemToDownload->ipaddress);
    
    /* Set up for P2P mode if applicable */
    if(G_P2PMode == TRUE)
    {
        threadParameter[0].p2pMode = TRUE;
        memcpy(&threadParameter[0].p2pServerInfo,&remoteSockAddr,sizeof(struct sockaddr_in));
    }
    else
    {
        threadParameter[0].p2pMode = FALSE;
    }

    /* Allocate memory for send/recv buffer */
    tftpBufSize = 4+65464;
    tftpBuffer = (char*)malloc(tftpBufSize*queuedItemToDownload->numConnectionsToUse);
    if(tftpBuffer == NULL)
    {
        free(threadParameter);
        sprintf(errorString,"Could not allocate Hybrid TFTP Send/Recv Buffer");
        reportError(errorString,0,0);
        closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
        G_TotalUsedConnections -= queuedItemToDownload->numConnectionsToUse;
        if(queuedItemToDownload->status != RESUMING)
            queuedItemToDownload->status = DL_FAILED;
        else
            queuedItemToDownload->status = FAIL_PARTIAL_DL;
        updateLVItem(queuedItemToDownload);
        ExitThread(-1);
    }

    /******************************************************************/
    /* Allocate memory for Temp Storage and the Circular Write Buffer */
    /******************************************************************/
    if( initStorageElements(&writeInformation, queuedItemToDownload) < 0)
    {
        free(tftpBuffer); free(threadParameter);
        closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
        reportError("Initializing Storage Elements Failed",0,0);
        G_TotalUsedConnections -= queuedItemToDownload->numConnectionsToUse;
        if(queuedItemToDownload->status != RESUMING)
            queuedItemToDownload->status = DL_FAILED;
        else
            queuedItemToDownload->status = FAIL_PARTIAL_DL;
        updateLVItem(queuedItemToDownload);
        ExitThread(-1);
    }

    /*******************************/
    /* Send TFTP Request to Server */
    /*******************************/
    firstTick = prevElapsedTime = GetTickCount();
    if(initiateTFTPReq(tftpBuffer, tftpBufSize, *sfd, remoteSockAddr,&port,
        queuedItemToDownload->dLItem.fullpathAndName, &writeInformation, 
        queuedItemToDownload->status, queuedItemToDownload->firstBlock,
        queuedItemToDownload->fileOffset, threadParameter[0].p2pMode,
        &(threadParameter[0].p2pServerInfo)) < 0)
    {
        free(tftpBuffer); free(threadParameter);
        closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
        releaseStorageElements(&writeInformation);
        reportError("HTFTP Request Error - HTFTP Request Failed",0,0);
        G_TotalUsedConnections -= queuedItemToDownload->numConnectionsToUse;
        if(queuedItemToDownload->status != RESUMING)
            queuedItemToDownload->status = DL_FAILED;
        else
            queuedItemToDownload->status = FAIL_PARTIAL_DL;
        updateLVItem(queuedItemToDownload);
        ExitThread(-1);
    }
    free(tftpBuffer);
    remoteSockAddr.sin_port = port;

    /* Got the 1st data packet */
    if( queuedItemToDownload->bytesRemain < 65464)
        queuedItemToDownload->bytesRemain = 0;
    else
        queuedItemToDownload->bytesRemain -= 65464;  


    /*****************************/
    /* Start up the write thread */
    /*****************************/
    hThread = CreateThread( 
		NULL,              // default security attributes
		0,                 // use default stack size  
		writeToDisk,       // thread function 
		(void*)&writeInformation,       // argument to thread function 
		0,                 // use default creation flags 
		&dwThreadId);	   // returns the thread identifier 
	if (hThread == NULL)
    {
        free(threadParameter);
        reportError("HTFTP Client Error - HTFTP Write Thread Failed to Spawn.",0,0);
        closesocket(*sfd);
        closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
        releaseStorageElements(&writeInformation);
        G_TotalUsedConnections -= queuedItemToDownload->numConnectionsToUse;
        if(queuedItemToDownload->status != RESUMING)
            queuedItemToDownload->status = DL_FAILED;
        else
            queuedItemToDownload->status = FAIL_PARTIAL_DL;
        updateLVItem(queuedItemToDownload);
        ExitThread(-1);
    }

    /* Elevate the Write Thread's Priority */
    //SetThreadPriority(hThread,THREAD_PRIORITY_HIGHEST);


    /**************************************/
    /* Transfer the Data over Hybrid TFTP */
    /**************************************/
	H_Sem_dlInformation = CreateSemaphore(NULL,1,1,NULL);
    for(x = 0; x < queuedItemToDownload->numConnectionsToUse; x++)
    {
        if(x != 0)
        {
            threadParameter[x].p2pMode = threadParameter[0].p2pMode;
            memcpy(&threadParameter[x].p2pServerInfo,&threadParameter[0].p2pServerInfo,sizeof(struct sockaddr_in));
        }
        threadParameter[x].ptrWriteInfo = &writeInformation;;
		threadParameter[x].dLinfo.dlInfoSem = &H_Sem_dlInformation;
		threadParameter[x].dLinfo.queueInfo = queuedItemToDownload;
        threadParameter[x].dLinfo.socket = queuedItemToDownload->sockArray[x];
        memcpy(&threadParameter[x].dLinfo.serverAddr,&remoteSockAddr, sizeof(struct sockaddr_in));

        hThread = CreateThread( 
		    NULL,              // default security attributes
		    0,                 // use default stack size  
		    startHybridTFTPThread,   // thread function 
		    (void*)&threadParameter[x], // argument to thread function 
		    0,                 // use default creation flags 
		    &dwThreadId);	   // returns the thread identifier 
	    if (hThread == NULL)
        {
            reportError("HTFTP Client Error - HTFTP Thread Failed to Spawn.",0,0);
            closesocket(queuedItemToDownload->sockArray[x]);
            G_TotalUsedConnections -= 1;
        }
        else
        {
            writeInformation.h_ThreadHandle[numHybridTftpThreads] = hThread;
            numHybridTftpThreads++;
            writeInformation.threadexitVal[x] = 99;
        }
    }


    /**********************************************************************/
    /* Wait for all threads to terminate successfully (or unsuccessfully) */
    /* Check every half-second.                                           */
    /**********************************************************************/
    while(1)
    {
        int rtn, count = 0;
        DWORD result;
        for(x = 0; x < numHybridTftpThreads; x++)
        {
            if(writeInformation.threadexitVal[x] == 99)
            {
                rtn = GetExitCodeThread(writeInformation.h_ThreadHandle[x], &result);
                if( (result != 259) && (rtn != 0) ) /* 259 means still active, 0 means error */
                {
                    writeInformation.threadexitVal[x] = result;
                    count++;
                }
            }
            else
                count++;
        }

        /* Check to see if all threads have returned */
        if(count >= numHybridTftpThreads)
        {
            break;
        }

        Sleep(500);

        /*********************************************************/
        /* Calculate avg, inst speeds and % complete, update gui */
        /*********************************************************/
        newElapsedTime = GetTickCount();
        bytesDownloaded = queuedItemToDownload->dLItem.fileSize - queuedItemToDownload->bytesRemain;

        if(queuedItemToDownload->bytesRemain > 0)
        {
            /* Instantaneous Speed */
            timeDouble = (((double)(newElapsedTime - prevElapsedTime)) / 1000.0);
            currentSample = lastBytesLeft - queuedItemToDownload->bytesRemain;
            rate = ((double)(currentSample));
            lastBytesLeft = queuedItemToDownload->bytesRemain;
            rate /= 1024.0;
            rate /= 1024.0;
            rate /= (double)timeDouble;
            queuedItemToDownload->instSpeed = (float)rate;
            prevElapsedTime = newElapsedTime;
            
            /* Average Speed */
            totalElapsedTime = GetTickCount() - firstTick;
            timeDouble = (((double)totalElapsedTime) / 1000.0);
            rate = ((double)bytesDownloaded);
            rate /= 1024.0;
            rate /= 1024.0;
            rate /= (double)timeDouble;
            queuedItemToDownload->avgSpeed = (float)rate;
        }

        /* Percent Complete */
        queuedItemToDownload->percentComplete = (int)
            (((double)bytesDownloaded / (double)queuedItemToDownload->dLItem.fileSize)*100.0);
        
        /* Update the Item in the GUI */
        updateLVItem(queuedItemToDownload);
    }


    /*********************************************************/
    /* Calculate avg, inst speeds and % complete, update gui */
    /*********************************************************/
    newElapsedTime = GetTickCount();
    bytesDownloaded = queuedItemToDownload->dLItem.fileSize - queuedItemToDownload->bytesRemain;
	
    /* Instantaneous Speed */
	if(newElapsedTime != prevElapsedTime)
	{
        timeDouble = (((double)(newElapsedTime - prevElapsedTime)) / 1000.0);
        currentSample = lastBytesLeft - queuedItemToDownload->bytesRemain;
        rate = ((double)(currentSample));
        lastBytesLeft = queuedItemToDownload->bytesRemain;
        rate /= 1024.0;
        rate /= 1024.0;
        rate /= (double)timeDouble;
        queuedItemToDownload->instSpeed = (float)rate;
        prevElapsedTime = newElapsedTime;
	}

	/* Average Speed */
    totalElapsedTime = GetTickCount() - firstTick;
    timeDouble = (((double)totalElapsedTime) / 1000.0);
    rate = ((double)bytesDownloaded);
    rate /= 1024.0;
    rate /= 1024.0;
    rate /= (double)timeDouble;
    queuedItemToDownload->avgSpeed = (float)rate;


    /* Percent Complete */
    queuedItemToDownload->percentComplete = (int)
        (((double)bytesDownloaded / (double)queuedItemToDownload->dLItem.fileSize)*100.0);
    
    /* Update the Item in the GUI */
    updateLVItem(queuedItemToDownload);


    /***********************************************************/
    /* Check to make sure at least 1 thread ended successfully */
    /* Otherwise the download was unsuccessful.                */
    /***********************************************************/
    for(x = 0; x < numHybridTftpThreads; x++)
     {
         if(writeInformation.threadexitVal[x] == 0)
         {
             downloadSuccess = TRUE;
             break;
         }
     }
     G_TotalUsedConnections -= numHybridTftpThreads;
     updateLVItem(queuedItemToDownload);


    /*******************************************/
    /* Signal Hybrid TFTP Transfer is Complete */
    /* Wait for the writes to finish           */
    /*******************************************/
    queuedItemToDownload->status = WAIT_WRITES;
    ReleaseSemaphore(writeInformation.h_TFTPDone,1,&prev);
    while(1)
    {
        if( WaitForSingleObject(writeInformation.h_WriteFail,0) == WAIT_OBJECT_0)
        {
            downloadSuccess = FALSE;
            reportError("HTFTP Write Data Error - A write error occurred after transferring HTFTP data.",0,0);
            reportError("HTFTP Transfer FAILED.",0,0);
            break;
        }
        if( WaitForSingleObject(writeInformation.h_WriteDone,0) == WAIT_OBJECT_0)
        {
            reportError("The Write thread Completed Successfully.",0,0);
            break;
        }
        Sleep(500);
        updateLVItem(queuedItemToDownload);
    }

    /***********/
    /* Cleanup */
    /***********/
    closeSockets(queuedItemToDownload->sockArray,queuedItemToDownload->numConnectionsToUse);
    releaseStorageElements(&writeInformation);
	CloseHandle(H_Sem_dlInformation);

    /******************************************/
    /* Check the md5 hash to ensure integrity */
    /******************************************/
    if(queuedItemToDownload->dLItem.hashType == MD5_HASH)
    {
        /* Partial download, Do not bother with hash */
        if(queuedItemToDownload->bytesRemain > 0)
        {
            queuedItemToDownload->status = FAIL_PARTIAL_DL;
            downloadSuccess = FALSE;
            updateLVItem(queuedItemToDownload);
            Sleep(2000);  //Give time to let the user see the failure
        }
        else
        {
		    queuedItemToDownload->status = CHECKING_MD5_HASH;
            updateLVItem(queuedItemToDownload);
            if( createMD5hash(queuedItemToDownload->dLItem.localfullpathAndName, (char*)&errorString[0]) < 0)
            {
                reportError("HTFTP XFER ERROR - MD5 Hash Algorithm Failure.",0,0);
                queuedItemToDownload->status = HASH_FAILED;
                updateLVItem(queuedItemToDownload);
            }
		    else
		    {
	            if(strcmp(errorString,&queuedItemToDownload->dLItem.hashValue[2]) != 0)
	            {
	                reportError("HTFTP XFER ERROR - MD5 Hash Match Failure.",0,0);
	                queuedItemToDownload->status = HASH_FAILED;
	                updateLVItem(queuedItemToDownload);
				    downloadSuccess = FALSE;
				    Sleep(2000);  //Give time to let the user see the failure
	            }
	            else
	            {
	                /* If a dl thread failed for whatever reason, but the hash passed, we got all the file */
	                downloadSuccess = TRUE;
	            }
            }
		}
    }

    /* Exit the thread */
    if(!downloadSuccess)
    {
        free(threadParameter);
        reportError("HTFTP XFER ERROR - The Data Transfer Terminated Prematurely.",0,0);
        if(queuedItemToDownload->status != RESUMING)        //Comment this out and find out why this kills the server
            queuedItemToDownload->status = DL_FAILED;
        else
            queuedItemToDownload->status = FAIL_PARTIAL_DL;
        updateLVItem(queuedItemToDownload);
        ExitThread(-1);
    }
    else
    {
        free(threadParameter);
        queuedItemToDownload->status = DL_COMPLETE;
        updateLVItem(queuedItemToDownload);
        ExitThread(0);
    }
}




/*****************************************************************************/
/* createTFTPReqMsg - Creates a TFTP Request Message.                        */
/* Returns:  Size of Request Msg.                                            */
/*****************************************************************************/
int createTFTPReqMsg(char* TFTPReqMsg, int bufSize, char* fileAndPath)
{
    int index = 0;

    memset(TFTPReqMsg,0,bufSize);

    /* Build Request Message */
    /* 01 FileToGet.txt\0 octet\0 blksize\0 65464\0 timeout\0 #SEC\0 tsize\0 0\0 */
    TFTPReqMsg[0] = 0;
    TFTPReqMsg[1] = 1;
    sprintf(&TFTPReqMsg[2], "%s", fileAndPath);
    index = (int)strlen(&TFTPReqMsg[2])+3;
    sprintf(&TFTPReqMsg[index],"octet");
    index += ((int)strlen("octet") + 1);

    /* Add Optional Parameters to the Request (dont really need these) */

    /* Blocksize is fixed to 65464 */
    sprintf(&TFTPReqMsg[index],"blksize");
    index += ((int)strlen(&TFTPReqMsg[index]) + 1);
    sprintf(&TFTPReqMsg[index],
        itoa(65464,&TFTPReqMsg[index], 10));
    index += ((int)strlen(&TFTPReqMsg[index]) + 1);

    /* Ask for xfer size */
    sprintf(&TFTPReqMsg[index],"tsize");
    index += ((int)strlen(&TFTPReqMsg[index]) + 1);
    sprintf(&TFTPReqMsg[index],
    itoa((int)0,&TFTPReqMsg[index], 10));
    index += ((int)strlen(&TFTPReqMsg[index]) + 1);
    
    return index;
}




/*****************************************************************************/
/* createResumeTFTPReqMsg - Creates a Hybrid TFTP Resume Message.            */
/* Returns:  Size of Request Msg.                                            */
/*****************************************************************************/
int createResumeTFTPReqMsg(char* TFTPReqMsg, int bufSize, char* fileAndPath,
                           unsigned __int64 bytesDownloaded)
{
    int index = 0;
    unsigned short resumeId = ntohs(OPC_RESUME);

    memset(TFTPReqMsg,0,bufSize);

    /* Build Request Message */
    /* 0x90 TFTP_P2P_RESTART\0 FileToGet.txt\0 BytesAlreadyDownloaded\0 */

    memcpy(&TFTPReqMsg[0],&resumeId,2);
    sprintf(&TFTPReqMsg[2], "TFTP_P2P_RESTART");
    index = (int)strlen(&TFTPReqMsg[2])+3;
    sprintf(&TFTPReqMsg[index], "%s", fileAndPath);
    index += (int)strlen(&TFTPReqMsg[index])+1;
    sprintf(&TFTPReqMsg[index], "%I64u", bytesDownloaded);
    index += (int)strlen(&TFTPReqMsg[index])+1;
    
    return index;
}




/*****************************************************************************/
/* initiateTFTPReq - Formulates and sends a TFTP Request and associated Acks.*/
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int initiateTFTPReq(char* tftpBuf, int tftpBufSize,
                    SOCKET sock, struct sockaddr_in remote, unsigned short* port,
                    char* fileAndPath, writeInfo* ptrWinfo, 
                    int status, unsigned short firstBlock,
                    unsigned __int64 fileOffset, int p2pMode,
                    struct sockaddr_in* p2pRemote)
{
    char errorString[200];
    int reqSize = 0;
    int dataPktSize = 0;
    int rtn, timeoutcnt, bytesRcvd, type;
    fd_set tset;
    struct sockaddr_in lremoteSockAddr;
    struct timeval timeout;
    timeout.tv_sec = DEFAULT_SEC_TIMEOUT;
    timeout.tv_usec = 100000;  /* 100ms */
    timeoutcnt = 0;

    /* Create the TFTP Request Message */
    if(status != RESUMING)
    {
        reqSize = createTFTPReqMsg(tftpBuf, tftpBufSize,fileAndPath);
    }
    else
    {
        reqSize = createResumeTFTPReqMsg(tftpBuf,tftpBufSize,fileAndPath,
            fileOffset);
    }

    /**********************************************************************/
    /* Continue to Send the Request until an OACK is received, or timeout */
    /**********************************************************************/
    while(1)
    {
        /* Send The TFTP Request Message */
        if( udp_send(sock,tftpBuf,reqSize,remote) < 0)
        {
            sprintf(errorString,"HTFTP Request Msg Send failed.");
            reportError(errorString,0,0);
        }

        FD_ZERO(&tset);
        FD_SET(sock,&tset);
        rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

        if(rtn == 0)
        {
            timeoutcnt++;
            if(timeoutcnt >= 5)
            {
                sprintf(errorString,"HTFTP Timeout Occurred while waiting for OACK.");
                reportError(errorString,0,0);
                return -1;
            }
        }
        else if(FD_ISSET(sock, &tset))
        {
            /* Receive the OACK from the server */
            /* Server will send 1st data packet if it doesnt support options negotiation */
            if( (bytesRcvd = udp_recv(sock,tftpBuf, tftpBufSize, &lremoteSockAddr)) < 0)
            {
                sprintf(errorString,"HTFTP OACK Receive Failed. Error %u\n",WSAGetLastError());
                reportError(errorString,0,0);
                return -1;
            }
            else 
            {
                /* Check the OACK/Data Packet and who sent it */
                *port = lremoteSockAddr.sin_port;
                remote.sin_port = *port;
                if((type = parse_oack(lremoteSockAddr,remote,tftpBuf,bytesRcvd,sock)) < 0)
                    continue;
                else
                    break;
            }
        }
    }

    /***************************************************************************/
    /* If we got back a data packet instead of an OACK, then the server doesnt */
    /* support options.  Disable all options and start transferring data       */
    /***************************************************************************/
    if(type == OPC_DATA)
    {
        sprintf(errorString,"HTFTP - Server responded with invalid data packet");
        reportError(errorString,0,0);
        return -1;
    }

    /**********************************************/
    /* Check to see what the server negotiated to */
    /**********************************************/
    if(validate_oack(tftpBuf,bytesRcvd) < 0)
    {
        sprintf(errorString,"HTFTP - Server responded with invalid negotation");
        reportError(errorString,0,0);
        return -1;
    }

    /**********************************************************************/
    /* Continue to Send the OACK ACK until 1st Datagram Recvd             */
    /**********************************************************************/
    dataPktSize = 4+65464;

    while(1)
    {
        /* Build OACK Ack Message */
        tftpBuf[0] = 0;
        tftpBuf[1] = 4;
        tftpBuf[2] = 0;
        tftpBuf[3] = 0;

        /* Send The OACK Ack Message */
        if( udp_send(sock,tftpBuf,4,remote) < 0)
        {
            sprintf(errorString,"HTFTP OACK Ack Send failed.");
            reportError(errorString,0,0);
            return -1;
        }

        FD_ZERO(&tset);
        FD_SET(sock,&tset);
        rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

        if(rtn == 0)
        {
            timeoutcnt++;
            if(timeoutcnt >= 5)
            {
                sprintf(errorString,"HTFTP - Timeout Occurred while waiting for first Data Packet.");
                reportError(errorString,0,0);
                return -1;
            }
        }
        else if(FD_ISSET(sock, &tset))
        {
            /* Recieve the 1st data from the server */
            if( (bytesRcvd = udp_recv(sock,tftpBuf, dataPktSize, &lremoteSockAddr)) < 0)
            {
                sprintf(errorString,"HTFTP First Data Receive failed.");
                reportError(errorString,0,0);
                return -1;
            }
            else 
            {
                *port = lremoteSockAddr.sin_port;
                if(p2pMode)
                {
                    memcpy(p2pRemote,&lremoteSockAddr,sizeof(struct sockaddr_in));
                    *port = 69;
                }

                /* Check the first data packet, and add it to the write buffer */
                if(parse_data(remote, lremoteSockAddr, tftpBuf, bytesRcvd, sock, firstBlock, ptrWinfo) < 0)
                    continue;
                else
                    break;
            }
        }

    }

    return 0;
}




/*****************************************************************************/
/* startHybridTFTPThread - Starts up the transferTFTPData function.          */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
DWORD WINAPI startHybridTFTPThread(LPVOID lpParam)
{
    char* tftpBuffer;
    int tftpBufSize, p2pMode;
    SOCKET sock;
    struct sockaddr_in remote;
    struct sockaddr_in p2pRemote;
    writeInfo* ptrWinfo;
    dlInfo* ptrDlInfo;

    /* Copy over input parameters to local variables */
    ptrDlInfo = &((htftpParamPassType*)lpParam)->dLinfo;
    ptrWinfo = ((htftpParamPassType*)lpParam)->ptrWriteInfo;
    p2pMode = ((htftpParamPassType*)lpParam)->p2pMode;
    memcpy(&p2pRemote, &((htftpParamPassType*)lpParam)->p2pServerInfo ,sizeof(struct sockaddr_in));
    
    sock = ptrDlInfo->socket;
    memcpy(&remote, &ptrDlInfo->serverAddr ,sizeof(struct sockaddr_in));

    /* Allocate memory for send/recv buffer */
    tftpBufSize = 4+65464;
    tftpBuffer = (char*)malloc(tftpBufSize);
    if(tftpBuffer == NULL)
    {
        reportError("Could not allocate Hybrid TFTP Send/Recv Buffer",0,0);
        closesocket(sock);
        ExitThread(-1);
    }

    /* Begin downloading */
    if( transferTFTPData(tftpBuffer, tftpBufSize, sock, remote,ptrWinfo, ptrDlInfo,
        p2pMode,p2pRemote) < 0)
    {
        reportError("Download thread failed.",0,0);
        closesocket(sock);
        free(tftpBuffer);
        ExitThread(-1);
    }

    free(tftpBuffer);
    closesocket(sock);

    return 0;
}




/*****************************************************************************/
/* transferTFTPData - Recvs TFTP Data and sends Acks.                        */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int transferTFTPData(char* tftpBuf, int tftpBufSize,
                     SOCKET sock, struct sockaddr_in remote,
                     writeInfo* ptrWinfo, dlInfo* ptrDlInfo,
                     int p2pMode, struct sockaddr_in p2pRemote)
{
    struct sockaddr_in temp;
    int parse_rtn;
    unsigned int bytesDownloaded = 0;//65464;
    unsigned short* ackBlockPtr = NULL;
    unsigned short currentBlock/*,lastBlock*/;
    int rtn, timeoutcnt, bytesRcvd, finalPacketCntr;
    fd_set tset;
    struct sockaddr_in lremoteSockAddr;
    struct timeval timeout;
    char errorString[200];
    int sendAck = TRUE;

    timeout.tv_sec = DEFAULT_SEC_TIMEOUT;
    timeout.tv_usec = 100000;  /* 100ms */
    timeoutcnt = 0;

    /* Swap the remote and p2p server structures for P2P to work */
    if(p2pMode)
    {
        memcpy(&temp,&p2pRemote,sizeof(struct sockaddr_in));
        memcpy(&p2pRemote,&remote,sizeof(struct sockaddr_in));
        memcpy(&remote,&temp,sizeof(struct sockaddr_in));
    }

    /* Ack the first block */
    currentBlock = ptrDlInfo->queueInfo->firstBlock;  // This is 1, unless doing a resume

    /***********************************************************/
    /* Perform the TFTP Data Xfer:                             */
    /* 1.) Ack a Data Packet (Should already have #1)          */
    /* 2.) Recv a Data Packet                                  */
    /* 3.) Repeat #1 and #2 until all data received.           */
    /* 4.) Send final Ack.  Setup timeout in case it may need  */
    /*     to be retransmitted to the server out of courtesy   */
    /***********************************************************/
    while(1)
    {
        /* End if the write thread is dead */
        if( WaitForSingleObject(ptrWinfo->h_WriteFail,0) == WAIT_OBJECT_0)
        {
            return -1;
        }

        /* Build the Ack Message */
        tftpBuf[0] = 0;
        tftpBuf[1] = 4;
        ackBlockPtr = (unsigned short*)&tftpBuf[2];
        *ackBlockPtr = htons(currentBlock);

        /* In case there are no bytes left to recv, just send acks */
        if( ptrDlInfo->queueInfo->bytesRemain <= 0)
            break;

        /* Send the Ack Message */
        if( udp_send(sock,tftpBuf,4,remote) < 0)
        {
            reportError("HTFTP Ack Send failed.",0,0);
            return -1;
        }

        /* Also send additional ack in p2p Mode */
        if(p2pMode)
        {
            if( udp_send(sock,tftpBuf,4,p2pRemote) < 0)
            {
                reportError("HTFTP Ack Send failed.",0,0);
                return -1;
            }    
        }


#if DEBUG_TFTP
        sprintf(tftpBuf,"Sent Ack %hd data to Addr %s, Port %hd\r\n",currentBlock,inet_ntoa(remote.sin_addr),htons(remote.sin_port));
        reportError(tftpBuf,0,0);
#endif 
        FD_ZERO(&tset);
        FD_SET(sock,&tset);
        rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

        if(rtn == 0)
        {
            timeoutcnt++;
            if(timeoutcnt >= 5)
            {
                sprintf(errorString,"Timeout Occurred while waiting for Data Packet %hd.",(currentBlock+1));
                reportError(errorString,0,0);
                return -1;
            }
        }
        else if(FD_ISSET(sock, &tset))
        {
            timeoutcnt = 0;

            /* Receieve the data from the server */
            if( (bytesRcvd = udp_recv(sock,tftpBuf, tftpBufSize, &lremoteSockAddr)) < 0)
            {
                sprintf(errorString,"HTFTP Data Receive Block %hd failed.",currentBlock);
                reportError(errorString,0,0);
                return -1;
            }
            else 
            {
                /* Check the data packet, and add it to the write buffer */
                currentBlock = htons( *((unsigned short*)&tftpBuf[2]));
                if( (parse_rtn = parse_data(remote, lremoteSockAddr, tftpBuf, bytesRcvd, sock, currentBlock,ptrWinfo)) < 0)
                {
                    /* Duplicate Packet Receive */
                    if(parse_rtn == -2)
                        continue;
                    else
                    {
                        /* Failed Packet Receive */
                        currentBlock--;
                        continue;
                    }
                }
                else
                {
                    /* Successful Packet Receive */

                    /* Adjust the size, mutual exclusion required (use another semaphore for this) */
					WaitForSingleObject(*ptrDlInfo->dlInfoSem,INFINITE);
                    ptrDlInfo->queueInfo->bytesRemain -= (bytesRcvd-4);
					ReleaseSemaphore(*ptrDlInfo->dlInfoSem,1,NULL);
					bytesDownloaded += bytesRcvd-4;

                    memcpy(&remote,&lremoteSockAddr,sizeof(struct sockaddr_in));


                    /* Check if the data transfer is complete */
                    if(bytesRcvd != tftpBufSize)
                    {
                        break;
                    }
                }
            }
        }
    }


    /*******************************************************/
    /* Send the Final Ack.  Setup a timeout and receive.   */
    /* If the server re-sends the final packet, re-ack it. */
    /* When timeout expires, its time to call it quits,    */
    /* we have everything we need anyway.                  */
    /*******************************************************/
    finalPacketCntr = 0;
    sendAck = TRUE;
    while(1)
    {
        if( WaitForSingleObject(ptrWinfo->h_WriteFail,0) == WAIT_OBJECT_0)
        {
            return -1;
        }

        /* Build the Ack Message */
        tftpBuf[0] = 0;
        tftpBuf[1] = 4;
        ackBlockPtr = (unsigned short*)&tftpBuf[2];
        *ackBlockPtr = htons(currentBlock);

        /* Send the Ack Message */
        /* Only send if a packet was just recvd */
        if(sendAck)
        {
            if( udp_send(sock,tftpBuf,4,remote) < 0)
            {
                sprintf(errorString,"HTFTP Final Ack Send failed.");
                reportError(errorString,0,0);
                break;
            }
        }
        sendAck = FALSE;

        /* Also send additional ack in p2p Mode */
        if(p2pMode)
        {
            if( udp_send(sock,tftpBuf,4,p2pRemote) < 0)
            {
                reportError("HTFTP Ack Send failed.",0,0);
                return -1;
            }    
        }

        FD_ZERO(&tset);
        FD_SET(sock,&tset);

        timeout.tv_sec = 0;
        rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

        if(rtn == 0)
        {
            timeoutcnt++;
            if(timeoutcnt >= 5)
            {
                break;
            }
        }
        else if(FD_ISSET(sock, &tset))
        {
            /* Recieve the data from the server */
            if( (bytesRcvd = udp_recv(sock,tftpBuf, tftpBufSize, &lremoteSockAddr)) < 0)
            {
                break;
            }
            else 
            {
                sendAck = TRUE;

                /* Got last packet again, Ack mustve gotten lost */
                /* will resend at the top of the while.          */
                finalPacketCntr++;
                if(finalPacketCntr == 5)
                {
                    /* Server is being dumb, just give up */
                    break;
                }
            }
        }
    }

    sprintf(errorString,"Thread Downloaded %u bytes.",bytesDownloaded);
                reportError(errorString,0,0);

    return 0;
}




/****************************************************************************/
/*Function:  validate_oack                                                  */
/*Purpose:   Determines if the input string is a valid oack.                */
/*           Checks options, and changes local ones if necessary.           */
/*Input: a buffer fill with the supposed OACK, the buffer size.             */
/*Return: -1 if not a valid OACK, 0 if it is.                               */
/****************************************************************************/
int validate_oack(char* oackMsg, int size)
{
    short opcode;

    //Minimum possible size check: opcode and 2 null characters
    if(size < 4)
    {
        return -1;
    }

    //Check that the OPCODE is an OACK
    memcpy((char*)&opcode,oackMsg,2);
    opcode = ntohs(opcode);
    if(opcode != OPC_OACK)
    {
        return -1;
    }

    //We can ignore the contents, everything in htftp is fixed

    return 0;  //Everything is okay
}




/****************************************************************************/
/*Function:  parse_oack                                                     */
/*Purpose:   to parse an oack received from a udp connection. It will ensure*/
/*           that the oack is properly formed and is from the right server  */
/*Return:    OPC_OACK if OACK Packet, OPC_DATA if DATA Packet,              */
/*          -1 if otherwise                                                 */
/****************************************************************************/
int parse_oack(struct sockaddr_in remote,struct sockaddr_in expected,char* buf, 
              int size, SOCKET sock)
{
    short temp;
    char tchar[150];

    //Check the ip address of the acking client
    if( (memcmp(&remote.sin_addr,&expected.sin_addr,sizeof(&expected.sin_addr))!=0) )
    {
        /*Send_Error(ERR_UNKNOWN_PORT,"",remote, sock);*/
        reportError("Ignoring packet, unknown server responded.",0,0);
        return -1;
    }

    //Check the opcode, should be OPC_OACK (0x06)
    memcpy((char*)&temp,buf,2);
    temp = ntohs(temp);
    if(temp == OPC_ERROR)
    {
        reportTftpError(buf,size);
        return -1;
    }
    else if(temp == OPC_DATA)
    {
        /* If server sent data, should have block #1 */
        memcpy((char*)&temp,&buf[2],2);
        temp = ntohs(temp);
        if(temp != 0x1)
        {
            reportError("HTFTP - Got malformed data packet",0,0);
            return -1;
        }
        else
        {
            return OPC_DATA;
        }
    }
    else if(temp != OPC_OACK)
    {
        sprintf(tchar,"Expected an OACK, opcode did not match that of OACK, got %hd\n",temp);
        reportError(tchar,0,0);
        return -1;
    }

#if DEBUG_TFTP
    reportError("HTFTP - Recvd OACK from server\n",0,0);
#endif

    return OPC_OACK;
}




/****************************************************************************/
/*Function:  parse_data                                                     */
/*Purpose:   to parse data received from a udp connection.  It will ensure  */
/*           that the data is properly formed and is from the right server  */
/*Return: 0 if the data corresponds to the expected block number.           */
/*        -1 if error or could not add packet to write buffer.              */
/*        -2 if duplicate packet.                                           */
/****************************************************************************/
int parse_data(struct sockaddr_in remote,struct sockaddr_in expected,char* buf, 
              int size, SOCKET sock, int chk_blocknum, writeInfo* ptrWinfo)
{
    short temp;
    int rtnval;
    char tchar[150];

    //Check the ip address of the acking client
    if( (memcmp(&remote.sin_addr,&expected.sin_addr,sizeof(&expected.sin_addr))!=0) )
    {
        reportError("HTFTP - Ignoring packet, unknown server responded.",0,0);
        return -1;
    }

    //Check the opcode, should be OPC_DATA (0x03)
    memcpy((char*)&temp,buf,2);
    temp = ntohs(temp);
    if(temp == OPC_ERROR)
    {
        reportTftpError(buf,size);
        return -1;
    }
    else if(temp != OPC_DATA)
    {
        sprintf(tchar,"Expected data, opcode did not match that of data, got %hd\n",temp);
        reportError(tchar,0,0);
        return -1;
    }

    //Get the block number
    //Dont care about comparing block #s, they may be out of order
    memcpy((char*)&temp,&buf[2],2);
    temp = ntohs(temp);

#if DEBUG_TFTP
    sprintf(tchar,"Recvd data for block %d\n",chk_blocknum);
    reportError(tchar,0,0);
#endif

    //Add the data to the write buffer
    if((rtnval = addToWriteBuffer(temp, &buf[4], (size-4), ptrWinfo)) < 0)
    {
        /* If the data could not be added, fake an error condition here        */
        /* We will send back the previous ack, the server will resend the data */
        /* and the connection will continue.                                   */
        return rtnval;
    }

    return 0;
}




/****************************************************************************/
/*Function:  closeSockets                                                   */
/*Purpose:   Closes all open sockets in an active hybrid tftp connection    */
/*Return: Void.                                                             */
/****************************************************************************/
void closeSockets(SOCKET* sockArray, int socketArraySize)
{
    int x;
    for(x=0; x < socketArraySize; x++)
    {
        closesocket(sockArray[x]);
        sockArray[x] = INVALID_SOCKET;
    }
    free(sockArray);

    return;
}
