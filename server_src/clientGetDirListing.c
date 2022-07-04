/****************************************************************************/
/* clientGetDirListing.c - Contains HTFTP directory listing related client  */
/*                         request/data transmission code.                  */
/****************************************************************************/
#pragma warning(disable : 4996)
#include <stdio.h>
#include <windows.h>
#include <winsock.h>
#include "shlobj.h"  
#include "cmn_util.h"
#include "sockfctns.h"
#include "opcode_definitions.h"
//#include "resource.h"


/***********/
/* Defines */
/***********/
#define DIRECTORY_BUFFER_SIZE   (512*1024) // 512kB
#define MAX_DIRECTORY_RETRIES    5


/****************************/
/* Global Client Parameters */
/****************************/
static char errorString[300];
static int G_DIR_XFER_TIMEOUT = 500000; //500ms per packet
static __int64 G_DirListing_Tsize = 0;


/***********************/
/* Function Prototypes */
/***********************/
int getDirListing(char* serverIPAddress, char** destBuffer, int p2pMode);
static int createDirReqMsg(char* dirReqMsg, int bufSize, int p2pMode);
static int initiateDirReq(char* tftpBuf, int tftpBufSize, SOCKET sock, 
                   struct sockaddr_in remote, unsigned short* port, 
                   char** destBuffer, unsigned int* destBufferSize,
                   unsigned int* remainingSize, unsigned int* xferSize, 
                   int p2pMode);
static int transferTFTPDirData(char* tftpBuf, int tftpBufSize,
                     SOCKET sock, struct sockaddr_in remote,
                     char** destBuffer, unsigned int* destBufSize,
                     unsigned int* remainingSize,  unsigned int* xferSize);
static int validate_DIR_oack(char* oackMsg, int size, unsigned int* sendSize);
static int parse_DIR_oack(struct sockaddr_in remote,struct sockaddr_in expected,
                   char* buf, int size, SOCKET sock);
static int parse_DIR_data(struct sockaddr_in remote,struct sockaddr_in expected,
                   char* buf, int size, SOCKET sock, int chk_blocknum);
static int copyDataToDirBuffer(char** dest, unsigned int* destBufferSize, 
                        unsigned int* remainingSize, char* data, int copySize);




/*****************************************************************************/
/* getDirListing - Handles a directory listing request.                      */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int getDirListing(char* serverIPAddress, char** destBuffer, int p2pMode)
{
    char* tftpBuffer = NULL;
    unsigned short localport;           // Client Port
    unsigned short port;                // Remote Server Port
    SOCKET sfd;                         // Client UDP Socket
    struct sockaddr_in remoteSockAddr;	// Socket Structure
    unsigned int destBufferSize, remainingSize,sizeDirListingBytes;
    int tftpBufSize, counter = 0;

   
    /**************************/
    /* Bind Local Client Port */
    /**************************/
    localport = MIN_CLIENT_PORT;
    sfd = INVALID_SOCKET;
    while(sfd == INVALID_SOCKET)
    {
        sfd = udpOpenBindSocket(localport);
        if(sfd == INVALID_SOCKET)
        {
            localport++;
            if(localport == MAX_CLIENT_PORT)
                localport = MIN_CLIENT_PORT;
        }

        /* Determine when to give up */
        counter++;
        if( (counter > 50) && (sfd == INVALID_SOCKET) )
        {
            sprintf(errorString,"Could not obtain a free port");
            reportError(errorString,0,0);
//            MessageBox(G_hWnd,errorString,"Directory Request Error",MB_OK);
            return -1;
        }
    }

    /***************************************/
    /* Set Up Remote Packet Structure      */
    /* Default TFTP Server Port = 69       */
    /***************************************/
    memset(&remoteSockAddr,0,sizeof(struct sockaddr_in));
    remoteSockAddr.sin_family = AF_INET;
    remoteSockAddr.sin_port   = htons(69);
    remoteSockAddr.sin_addr.s_addr = inet_addr(serverIPAddress);

    /* Allocate memory for send/recv buffer */
    tftpBufSize = DIR_RQST_PSIZE;
    tftpBuffer = (char*)malloc(tftpBufSize);
    if(tftpBuffer == NULL)
    {
        sprintf(errorString,"Could not allocate TFTP Send/Recv Buffer for directory request");
        reportError(errorString,0,0);
//        MessageBox(G_hWnd,errorString,"Directory Request Error",MB_OK);
        closesocket(sfd);
        return -1;
    }

    /* Allocate memory for the write buffer */
    *destBuffer = (char*)malloc(DIRECTORY_BUFFER_SIZE);
    if(*destBuffer == NULL)
    {
        sprintf(errorString,"Could not allocate directory buffer");
        reportError(errorString,0,0);
 //       MessageBox(G_hWnd,errorString,"Directory Request Error",MB_OK);
        free(tftpBuffer);
        closesocket(sfd);
        return -1;
    }
    destBufferSize = remainingSize = DIRECTORY_BUFFER_SIZE;


    /*****************************************/
    /* Send TFTP Directory Request to Server */
    /*****************************************/
    if(initiateDirReq(tftpBuffer, tftpBufSize, sfd, remoteSockAddr,&port,
        destBuffer,&destBufferSize, &remainingSize,&sizeDirListingBytes, p2pMode) < 0)
    {
        free(tftpBuffer);
        free(*destBuffer);
        *destBuffer = NULL;
        sprintf(errorString,"An error occurred while initiating Directory data request.");
        reportError(errorString,0,0);
 //       MessageBox(G_hWnd,errorString,"Directory Request Error",MB_OK);
        closesocket(sfd);
        return -1;
    }
    remoteSockAddr.sin_port = port;


    /*****************************************/
    /* Transfer the Directory Data over TFTP */
    /*****************************************/
    if(transferTFTPDirData(tftpBuffer, tftpBufSize, sfd, remoteSockAddr,
        destBuffer,&destBufferSize,&remainingSize,&sizeDirListingBytes) < 0)
    {
        free(tftpBuffer);
        free(*destBuffer);
        *destBuffer = NULL;
        sprintf(errorString,"An error occurred while transferring Directory data.");
        reportError(errorString,0,0);
//        MessageBox(G_hWnd,errorString,"Directory Request Error",MB_OK);
        closesocket(sfd);
        return -1;
    }


    /***********/
    /* Cleanup */
    /***********/
    free(tftpBuffer);
    closesocket(sfd);

    return 0;
}




/*****************************************************************************/
/* createDirReqMsg - Creates a Directory Request Message.                    */
/* Returns:  Size of Request Msg.                                            */
/*****************************************************************************/
static int createDirReqMsg(char* dirReqMsg, int bufSize, int p2pMode)
{
    unsigned short id = 0;

    /* Build Directory Request Message */
    /* 98 TFTP_DIRRQ\0 C/S\0  OR  98 TFTP_DIRRQ\0 P2P\0 */
    memset(dirReqMsg,0,20);
    id = htons(OPC_DIRRQ);
    memcpy(&dirReqMsg[0],&id,2);
    strcpy(&dirReqMsg[2],"TFTP_DIRRQ");
    if(!p2pMode)
    {
        strcpy(&dirReqMsg[13],"C/S");
    }
    else
    {
        strcpy(&dirReqMsg[13],"P2P");
    }
    
    return 17;
}




/*****************************************************************************/
/* initiateDirReq - Formulates and sends a DIR Request and associated Acks.  */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
static int initiateDirReq(char* tftpBuf, int tftpBufSize, SOCKET sock, 
                   struct sockaddr_in remote, unsigned short* port, 
                   char** destBuffer, unsigned int* destBufferSize,
                   unsigned int* remainingSize, unsigned int* xferSize,
                   int p2pMode)
{
    int reqSize = 0;
    int dataPktSize = 0;
    int rtn, timeoutcnt, bytesRcvd, type;
    fd_set tset;
    struct sockaddr_in lremoteSockAddr;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = G_DIR_XFER_TIMEOUT;
    timeoutcnt = 0;

    /* Create the TFTP Dir Request Message */
    reqSize = createDirReqMsg(tftpBuf, tftpBufSize, p2pMode);

    /**********************************************************************/
    /* Continue to Send the Request until an OACK is received, or timeout */
    /**********************************************************************/
    while(1)
    {
        /* Send The Directory Request Message */
        if( udp_send(sock,tftpBuf,reqSize,remote) < 0)
        {
            sprintf(errorString,"TFTP Dir Request Msg Send failed.");
        }

        FD_ZERO(&tset);
        FD_SET(sock,&tset);
        rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

        if(rtn == 0)
        {
            timeoutcnt++;
            if(timeoutcnt >= MAX_DIRECTORY_RETRIES)
            {
                sprintf(errorString,"DIR Timeout Occurred while waiting for OACK.");
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
                sprintf(errorString,"DIR OACK Receive Failed. Error %u\n",WSAGetLastError());
                reportError(errorString,0,0);
                return -1;
            }
            else 
            {
                /* Check the OACK/Data Packet and who sent it */
                *port = lremoteSockAddr.sin_port;
                remote.sin_port = *port;
                if((type = parse_DIR_oack(lremoteSockAddr,remote,tftpBuf,bytesRcvd,sock)) < 0)
                    continue;
                else
                    break;
            }
        }
    }


    /**********************************************/
    /* Check to see what the server negotiated to */
    /**********************************************/
    if(validate_DIR_oack(tftpBuf,bytesRcvd, xferSize) < 0)
    {
        sprintf(errorString,"DIR - Server responded with invalid negotation");
        reportError(errorString,0,0);
        return -1;
    }

    /**********************************************************************/
    /* Continue to Send the OACK ACK until 1st Datagram Recvd             */
    /**********************************************************************/
    dataPktSize = DIR_RQST_PSIZE;

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
            sprintf(errorString,"DIR OACK Ack Send failed.");
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
                sprintf(errorString,"DIR - Timeout Occurred while waiting for first Data Packet.");
                reportError(errorString,0,0);
                return -1;
            }
        }
        else if(FD_ISSET(sock, &tset))
        {
            /* Recieve the 1st data from the server */
            if( (bytesRcvd = udp_recv(sock,tftpBuf, dataPktSize, &lremoteSockAddr)) < 0)
            {
                sprintf(errorString,"DIR First Data Receive failed.");
                reportError(errorString,0,0);
                return -1;
            }
            else 
            {
                /* Check the first data packet, and add it to the write buffer */
                if(parse_DIR_data(remote, lremoteSockAddr, tftpBuf, bytesRcvd, sock, 0x1) < 0)
                    continue;
                else
                    break;
            }
        }
    }

    /* Copy the packet to the directory listing buffer */
    if(copyDataToDirBuffer(destBuffer, destBufferSize, remainingSize, 
        &tftpBuf[4], (bytesRcvd-4)) < 0)
    {
        sprintf(errorString,"Failed to copy data to from recv buffer to directory buffer.");
        reportError(errorString,0,0);
        return -1;
    }
    *xferSize -= (bytesRcvd-4);

    return 0;
}




/*****************************************************************************/
/* Function: transferTFTPDirData                                             */
/* Purpose: Controls Directory Listing Xfer. Recvs TFTP Data and sends Acks. */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
static int transferTFTPDirData(char* tftpBuf, int tftpBufSize,
                     SOCKET sock, struct sockaddr_in remote,
                     char** destBuffer, unsigned int* destBufSize,
                     unsigned int* remainingSize, unsigned int* xferSize)
{
    unsigned short* ackBlockPtr = NULL;
    unsigned short currentBlock = 0x1;
    int rtn, timeoutcnt, bytesRcvd, finalPacketCntr;
    fd_set tset;
    struct sockaddr_in lremoteSockAddr;
    struct timeval timeout;

    /* Variables for display updates */
    char pBuf[32], instBuf[32], avgBuf[32];
    __int64 bytesDownloaded = (tftpBufSize-4);
    __int64 prevBytesDownloaded = bytesDownloaded;
    int percentComplete, newPercentComplete;
    DWORD firstTick, newElapsedTime, prevElapsedTime, totalElapsedTime;
    double rate, timeDouble;

    memset(instBuf,0,32);
    memset(avgBuf,0,32);
    percentComplete = newPercentComplete = 0;
    timeout.tv_sec = 0;
    timeout.tv_usec = G_DIR_XFER_TIMEOUT;
    timeoutcnt = 0;

    firstTick = prevElapsedTime = GetTickCount();


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
        /* If only 1 packet, break out */
        if(*xferSize <= 0)
        {
            break;
        }

        /* Build the Ack Message */
        tftpBuf[0] = 0;
        tftpBuf[1] = 4;
        ackBlockPtr = (unsigned short*)&tftpBuf[2];
        *ackBlockPtr = htons(currentBlock);

        /* Send the Ack Message */
        if( udp_send(sock,tftpBuf,4,remote) < 0)
        {
            sprintf(errorString,"DIR Ack Send failed.");
            reportError(errorString,0,0);
            return -1;
        }

        FD_ZERO(&tset);
        FD_SET(sock,&tset);
        rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

        if(rtn == 0)
        {
            timeoutcnt++;
            if(timeoutcnt >= MAX_DIRECTORY_RETRIES)
            {
                sprintf(errorString,"DIR Timeout Occurred while waiting for Data Packet %hd.",(currentBlock+1));
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
                sprintf(errorString,"DIR Data Receive Block %hd failed.",currentBlock);
                reportError(errorString,0,0);
                return -1;
            }
            else 
            {
                /* Check the data packet */
                if(parse_DIR_data(remote, lremoteSockAddr, tftpBuf, bytesRcvd, sock, currentBlock+1) < 0)
                {
                    /* Failed Packet Receive */
                    continue;
                }
                else
                {
                    
                    /* Successful Packet Receive */
                    
                    /* Copy the packet to the directory listing buffer */
                    if(copyDataToDirBuffer(destBuffer, destBufSize, remainingSize, 
                        &tftpBuf[4], (bytesRcvd-4)) < 0)
                    {
                        sprintf(errorString,"Failed to copy data to from recv buffer to directory buffer.");
                        reportError(errorString,0,0);
                        return -1;
                    }
                    currentBlock++;
                    bytesDownloaded += (bytesRcvd-4);

                    /* Update Instantaneous, Average MB/sec, and Progress every half second */
                    newElapsedTime = GetTickCount();
                    if((newElapsedTime - prevElapsedTime) >= 5)
                    {
                        /* Instantaneous Speed */
                        timeDouble = (((double)(newElapsedTime - prevElapsedTime)) / 1000.0);
                        rate = ((double)(bytesDownloaded-prevBytesDownloaded));
                        prevBytesDownloaded = bytesDownloaded;
                        rate /= 1024.0;
                        rate /= 1024.0;
                        rate /= (double)timeDouble;
                        sprintf(pBuf,"%.2f",(float)rate);
                        if(strcmp(pBuf,instBuf) != 0)
                        {
                            strcpy(instBuf,pBuf);
                        }

                        /* Average Speed */
                        totalElapsedTime = GetTickCount() - firstTick;
                        timeDouble = (((double)totalElapsedTime) / 1000.0);
                        rate = ((double)bytesDownloaded);
                        rate /= 1024.0;
                        rate /= 1024.0;
                        rate /= (double)timeDouble;
                        sprintf(pBuf,"%.2f",(float)rate);
                        if(strcmp(pBuf,avgBuf) != 0)
                        {
                              strcpy(avgBuf,pBuf);
                        }
                        
                        prevElapsedTime = newElapsedTime;

                        /* Update Progress, if applicable */
                        if(G_DirListing_Tsize > 0)
                        {
                            newPercentComplete = (int)(((double)bytesDownloaded/(double)G_DirListing_Tsize) * 100.0);
                            if(newPercentComplete != percentComplete)
                            {
                                percentComplete = newPercentComplete;
                            }
                        }
                    }

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
    while(1)
    {
        /* Build the Ack Message */
        tftpBuf[0] = 0;
        tftpBuf[1] = 4;
        ackBlockPtr = (unsigned short*)&tftpBuf[2];
        *ackBlockPtr = htons(currentBlock);

        /* Send the Ack Message */
        if( udp_send(sock,tftpBuf,4,remote) < 0)
        {
            sprintf(errorString,"DIR Final Ack Send failed.");
            reportError(errorString,0,0);
            break;
        }

        FD_ZERO(&tset);
        FD_SET(sock,&tset);
        rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

        if(rtn == 0)
        {
            timeoutcnt++;
            if(timeoutcnt >= MAX_DIRECTORY_RETRIES)
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

    return 0;
}




/****************************************************************************/
/*Function:  validate_DIR_oack                                              */
/*Purpose:   Determines if the input string is a valid oack.                */
/*Input: a buffer fill with the supposed OACK, the buffer size.             */
/*Return: -1 if not a valid OACK, 0 if it is.                               */
/****************************************************************************/
static int validate_DIR_oack(char* oackMsg, int size, unsigned int* sendSize)
{
    char* ptr;
    int blksizeFound, timeoutFound, tsizeFound;
    short opcode;
    int length,cntr;
    int rval = -1;
    blksizeFound = timeoutFound = tsizeFound = FALSE;

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

    //Attempt to Extract Options Information
    //Supports Block Size (8 to 65464), Timeout, and Transfer Size Options
    size = size-2;
    cntr = 2;
    ptr = NULL;
    while(size > 0)
    {
        length  = extract_string(&oackMsg[cntr],&ptr,size);

        /* Check to see if a string was extracted */
        if(length < 0)
        {
            if(ptr != NULL)
                free(ptr);
            break;
        }
        else
        {
            size -= length;
            cntr += length;
        }

        if(strcmp(ptr,"blksize") == 0)
        {
            if(ptr != NULL)
                free(ptr);
            length  = extract_string(&oackMsg[cntr],&ptr,size);
            if(length < 0)
            {
                break;
            }
            size -= length;
            cntr += length;
            blksizeFound = TRUE;
        }
        else if(strcmp(ptr,"timeout") == 0)
        {
            if(ptr != NULL)
                free(ptr);
            length  = extract_string(&oackMsg[cntr],&ptr,size);
            if(length < 0)
            {
                break;
            }
            size -= length;
            cntr += length;
            timeoutFound = TRUE;
        }
        else if(strcmp(ptr,"tsize") == 0)
        {
            if(ptr != NULL)
                free(ptr);
            length  = extract_string(&oackMsg[cntr],&ptr,size);
            if(length < 0)
            {
                break;
            }
            size -= length;
            cntr += length;
            G_DirListing_Tsize = _atoi64(ptr);
            *sendSize = (unsigned int)G_DirListing_Tsize;
            tsizeFound = TRUE;
        }
        else
        {
            ;/*printf("Unknown Option Detected, Ignoring.\n");*/
        }

        /* Free used memory */
        if(ptr != NULL)
        {
            free(ptr);
            ptr = NULL;
        }
    }

    /* Free used memory */
    if(ptr != NULL)
    {
        free(ptr);
        ptr = NULL;
    }

    /* Check for an erroneous response */
    if( (blksizeFound == -1) || (timeoutFound == -1) ||
        (tsizeFound == -1) )
    {
        return -1;
    }

    return 0;  //Everything is okay
}




/****************************************************************************/
/*Function:  parse_DIR_oack                                                 */
/*Purpose:   to parse an oack received from a udp connection. It will ensure*/
/*           that the oack is properly formed and is from the right server  */
/*Return:    OPC_OACK if OACK Packet, OPC_DATA if DATA Packet,              */
/*          -1 if otherwise                                                 */
/****************************************************************************/
static int parse_DIR_oack(struct sockaddr_in remote,struct sockaddr_in expected,
                   char* buf, int size, SOCKET sock)
{
    char tchar[150];
    short temp;

    //Check the ip address and port of the acking client
    if( (memcmp(&remote.sin_addr,&expected.sin_addr,sizeof(&expected.sin_addr))!=0) || 
        (memcmp(&remote.sin_port,&expected.sin_port,sizeof(&expected.sin_port))!=0) )
    {
        reportError("parse_DIR_oack - Ignoring packet, unknown server responded.",0,0);
        return -1;
    }

    //Check the opcode, should be OPC_OACK (0x06)
    memcpy((char*)&temp,buf,2);
    temp = ntohs(temp);
    if(temp == OPC_ERROR)
    {
//        reportTftpError(buf,size);
        return -1;
    }
    else if(temp == OPC_DATA)
    {
        /* If server sent data, should have block #1 */
        memcpy((char*)&temp,&buf[2],2);
        temp = ntohs(temp);
        if(temp != 0x1)
        {
            reportError("DIR - Got malformed data packet",0,0);
            return -1;
        }
        else
        {
            return OPC_DATA;
        }
    }
    else if(temp != OPC_OACK)
    {
        sprintf(tchar,"DIR-Expected an OACK, opcode did not match that of OACK, got %hd\n",temp);
        reportError(tchar,0,0);
        return -1;
    }

#if DEBUG_TFTP
    reportError("Recvd OACK from server",0,0);
#endif

    return OPC_OACK;
}




/****************************************************************************/
/*Function:  parse_DIR_data                                                 */
/*Purpose:   to parse data received from a udp connection.  It will ensure  */
/*           that the data is properly formed and is from the right server  */
/*Return: 0 if the data corresponds to the expected block number.           */
/*        -1 if otherwise                                                   */
/****************************************************************************/
static int parse_DIR_data(struct sockaddr_in remote,struct sockaddr_in expected,
                   char* buf, int size, SOCKET sock, int chk_blocknum)
{
    char tchar[150];
    short temp;

    //Check the ip address and port of the acking client
    if( (memcmp(&remote.sin_addr,&expected.sin_addr,sizeof(&expected.sin_addr))!=0) || 
        (memcmp(&remote.sin_port,&expected.sin_port,sizeof(&expected.sin_port))!=0) )
    {
        /*Send_Error(ERR_UNKNOWN_PORT,"",remote, sock);*/
        reportError("Ignoring packet, unknown server responded.",0,0);
        return -1;
    }

    //Check the opcode, should be OPC_DATA (0x03)
    memcpy((char*)&temp,buf,2);
    temp = ntohs(temp);
    if(temp == OPC_ERROR)
    {
//        reportTftpError(buf,size);
        return -1;
    }
    else if(temp != OPC_DATA)
    {
        sprintf(tchar,"DIR - Expected data, opcode did not match that of data, got %hd\n",temp);
        reportError(tchar,0,0);
        return -1;
    }

    //Check the block number, compare it to the expected block #
    //if they match, return 0.  This will tell the program if old or
    //new data was received.
    memcpy((char*)&temp,&buf[2],2);
    temp = ntohs(temp);
    if(temp != ((short)chk_blocknum))
    {
        sprintf(tchar,"Recvd ack, but with unexpected block number %hd expected %hd\n",temp,chk_blocknum);
        reportError(tchar,0,0);
        return -1;
    }

#if DEBUG_TFTP
    sprintf(tchar,"Recvd data for block %d\n",chk_blocknum);
    reportError(tchar,0,0);
#endif

    return 0;
}




/****************************************************************************/
/*Function:  copyDataToDirBuffer                                            */
/*Purpose:   Copies input data to the directory listing storage buffer.     */
/*Return: 0 on success, -1 on failure.                                      */
/****************************************************************************/
static int copyDataToDirBuffer(char** dest, unsigned int* destBufferSize, 
                        unsigned int* remainingSize, char* data, int copySize)
{

    if( (*remainingSize-copySize) >= 0)
    {
        memcpy( &((*dest)[*destBufferSize-*remainingSize]),data,copySize);
    }
    else
    {
        if( resize_and_copy2(dest, (*destBufferSize), (*destBufferSize * 2)) < 0)
        {
            reportError("resize and copy failed",0,0);
            return -1;
        }
        else
        {
            *remainingSize += *destBufferSize;
            *destBufferSize = *destBufferSize * 2;
            memcpy( &((*dest)[*destBufferSize-*remainingSize]),data,copySize);
        }
    }

    return 0;
}
