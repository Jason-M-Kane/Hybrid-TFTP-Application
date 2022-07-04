/****************************************************************************/
/* tftp_operations.c - Contains code that handles directory and hybrid      */
/*                     tftp data transfers.                                 */
/****************************************************************************/
#pragma warning(disable : 4996)

/************/
/* Includes */
/************/
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "opcode_definitions.h"
#include "cmn_util.h"
#include "sockfctns.h"



/***********/
/* Defines */
/***********/
#define MAX_TIMEOUT_COUNT        5             //#Timeouts before giving up
#define DEFAULT_SEC_TIMEOUT      1             //Timeout = this + 100ms



/***********************/
/* Function Prototypes */
/***********************/
void dir_transfer(SOCKET local,struct sockaddr_in c_remote,char* buffer,
                  optsData opt);
int get_next_dir_data(char* buffer,int *current_block,char** send_buf,
                      int blkSize, int totalBytes);
int parse_ack(struct sockaddr_in remote,struct sockaddr_in check, char* buf, 
              int size, SOCKET sock, int chk_block);
int sendPeerMsg(char* filename,unsigned __int64 offset,
                unsigned short current_block,char* ipaddr,short port);
void Send_Error(int errortype, char* add_msg,struct sockaddr_in remote,
                SOCKET sock);
int sendSeverOptAck(SOCKET sock, struct sockaddr_in c_remote,
                    struct sockaddr_in remote, optsData opt);




/****************************************************************************/
/*Function:  sendSeverOptAck                                                */
/*Purpose:   Sets up an Option Ack                                          */
/*Return: 0 on success, -1 on failure.                                      */
/****************************************************************************/
int sendSeverOptAck(SOCKET sock, struct sockaddr_in c_remote,
                    struct sockaddr_in remote, optsData opt)
{
    char* serverOptAck = NULL;
    char ackbuf[4];
    fd_set tset;
    struct timeval timeout;
    int index, packetSize, rtn, timeoutcnt;
    timeoutcnt = rtn = 0;

    serverOptAck = (char*)malloc(MAX_PSIZE);
    if(serverOptAck == NULL)
    {
        printf("Error allocating memory for server option ack.\n");
        return -1;
    }
    memset(serverOptAck,0,MAX_PSIZE);

    serverOptAck[0] = 0;
    serverOptAck[1] = 6;
    index = 2;

    if(opt.blkSizeNeg == TRUE)
    {
        strcpy(&serverOptAck[index],"blksize");
        index += (int)strlen(&serverOptAck[index])+1;
        itoa(opt.blkSize,&serverOptAck[index],10);
        index += (int)strlen(&serverOptAck[index])+1;
    }

    if(opt.timeoutNeg == TRUE)
    {
        strcpy(&serverOptAck[index],"timeout");
        index += (int)strlen(&serverOptAck[index])+1;
        itoa(opt.timeout,&serverOptAck[index],10);
        index += (int)strlen(&serverOptAck[index])+1;
    }

    if(opt.tsizeNeg == TRUE)
    {
        strcpy(&serverOptAck[index],"tsize");
        index += (int)strlen(&serverOptAck[index])+1;
        _i64toa(opt.tsize,&serverOptAck[index],10);
        index += (int)strlen(&serverOptAck[index])+1;
    }
    packetSize = index;


    /***************************************/
    /* Send the Server's Option Ack Packet */
    /***************************************/
    while(1)
    {
        //Send Option ACK
        udp_send(sock,serverOptAck,packetSize,remote);
        while(1)
        {
            if(opt.timeoutNeg == TRUE)
            {
                timeout.tv_sec = opt.timeout;
                timeout.tv_usec = 0;
            }
            else
            {
                timeout.tv_sec = DEFAULT_SEC_TIMEOUT;
                timeout.tv_usec = 100000;  /* 100ms */
            }
            FD_ZERO(&tset);
            FD_SET(sock,&tset);
            rtn = select(((int)sock+1),&tset, NULL, NULL, &timeout);

            if(rtn == 0)
            {
                timeoutcnt++;
                if(timeoutcnt >= 5)
                {
                    free(serverOptAck);
                    return -1;
                }
            }
            else if(FD_ISSET(sock, &tset))
            {
                if(udp_recv(sock,ackbuf,4,&remote) < 0)
                {
                    continue;
                }
                else 
                {
                    break;
                }
            }
        }
        if(parse_ack(remote,c_remote,ackbuf,4,sock,0) < 0)
        {
            continue;  //bad ACK
        }
        break;
    }
    
    free(serverOptAck);


    return 0;
}




/****************************************************************************/
/*Function:  dir_transfer                                                   */
/*Purpose:   Perfoms a directory listing transfer.                          */
/*Input:  the local socket file descriptor, the sockaddr_in struct that     */
/*        corresponds to the remote client, the buffer containing the       */
/*        virtual directory listing, tftp options data.                     */
/*Return: nothing                                                           */
/****************************************************************************/
void dir_transfer(SOCKET local,struct sockaddr_in c_remote,char* buffer,
                  optsData opt)
{
    fd_set tset;
    int current_block,timeoutcnt,nextsize,len,asize,rtn,blkSize,fileSize;
    struct timeval timeout;
    struct sockaddr_in remote;
    char* sendbuf = NULL;
    char ackbuf[4];
    len = sizeof(remote);
    memcpy(&remote,&c_remote,len);

    /* Get Size of the transfer in Bytes */
    fileSize = (int)(strlen(buffer)+1);

    /************************************************************/
    /* Send the OACK, and wait to get an Ack of 0 back from the */
    /* client.  Try 5x before giving up.                        */
    /************************************************************/
    if( (opt.blkSizeNeg == TRUE) || (opt.timeoutNeg == TRUE) || 
        (opt.tsizeNeg   == TRUE) )
    {
        if(sendSeverOptAck(local,c_remote,remote,opt) < 0)
        {
            printf("Option Negotiation Failed.\n");
            Send_Error(ERR_OPTION,"",c_remote,local);
            return;
        }
    }

    //Init Variables
    blkSize = opt.blkSize;
    current_block = 0;
    timeoutcnt = 0;

    //Initial Send
    nextsize = get_next_dir_data(buffer,&current_block,&sendbuf,blkSize,
        fileSize);
    udp_send(local,sendbuf,nextsize,c_remote);
#if DEBUG_TFTP
    printf("Sent DATA <block = %d, Data = %d bytes>\n",current_block,nextsize);
#endif

    //Begin transferring the rest of the file using select()
    while(1)
    { 
        if(opt.timeoutNeg == TRUE)
        {
            timeout.tv_sec = opt.timeout;
            timeout.tv_usec = 0;
        }
        else
        {
            timeout.tv_sec = DEFAULT_SEC_TIMEOUT;
            timeout.tv_usec = 100000;  /* 100ms */
        }
        FD_ZERO(&tset);
        FD_SET(local,&tset);
        rtn = select(((int)local+1),&tset, NULL, NULL, &timeout);

        if(FD_ISSET(local, &tset))
        {
            //Get the ACK
            asize = udp_recv(local,ackbuf,4,&remote);
            if(asize < 0)
            {
                timeoutcnt++;
                udp_send(local,sendbuf,nextsize,c_remote); //resend last packet
                continue;
            }
            if(parse_ack(remote,c_remote,ackbuf,asize,local,current_block) < 0)
            {
                udp_send(local,sendbuf,nextsize,c_remote); //resend last packet
                continue;  //bad ACK
            }
            if(nextsize < (blkSize+4))  //the previously sent data was the last chunk   
            {
                printf("SUCCESS, DIRSEND DONE!  lastSize = %d\n",nextsize);
                break;          //now that the ack is recvd, connection is done
            }

            //Get and send the next chunk of data
            nextsize = get_next_dir_data(buffer,&current_block,&sendbuf,blkSize,
                fileSize);
            udp_send(local,sendbuf,nextsize,remote);
#if DEBUG_TFTP
            printf("Sent DATA <block=%d, Data = %d bytes>\n",current_block,
                nextsize);
#endif
            timeoutcnt = 0; //reset timeout delay
        }

        if(rtn == 0)
        { 
            //Timeout
            timeoutcnt++;
            if(timeoutcnt == 5)
            { 
                //kill connection
                printf("Session timed out, killing session with client\n");
                return;
            }
            else
            {  
                //Re-send the last packet
                udp_send(local,sendbuf,nextsize,c_remote);
#if DEBUG_TFTP
                printf("Sent DATA <block=%d, Data=%d bytes>\n",
                    current_block,nextsize);
#endif
            }
        }
        else if(rtn < 0)
        { 
            //Select failed -- nonfatal error
            reportError("Select failed in a thread, continuing...",
                WSAGetLastError(),1);
            timeoutcnt++;
        }
    }

    /* Free sendbuf buffer */
    if(sendbuf != NULL)
        free(sendbuf);

    return;
}




/****************************************************************************/
/*Function:  get_next_dir_data                                              */
/*Purpose: gets the next chunk of directory data to send & puts it in a     */
/*         buffer.                                                          */
/*Input:  ptr to the buffer containing the file system, the current block,  */
/*        a buffer to place the next chunk of data in, blksize, totalbytes. */
/*Return:the integer corresponding to the size of data in the buffer to send*/
/****************************************************************************/
int get_next_dir_data(char* buffer,int *current_block,char** send_buf,
                      int blkSize, int totalBytes)
{
    static int numBytesSent = 0;
    char* send_packet = NULL;
    DWORD bytesRead;
    int total;
    short opcode,blk;

    if(*send_buf == NULL)
    {
        numBytesSent = 0;
        *send_buf = (char*)malloc(blkSize+4);
        if(*send_buf == NULL)
        {
            printf("Error allocating send_buf\n");
            return -1;
        }
    }

    //Create the initial 2 byte data opcode of OPC_DATA (0x03)
    opcode = OPC_DATA;
    opcode = htons(opcode);
    memcpy(*send_buf,(char*)&opcode,2);

    //Create the 2 byte block number
    *current_block = *current_block+1;
    if(*current_block >= 65536)
        *current_block = 0;
    blk = *current_block;
    blk = htons(blk);
    memcpy(&((*send_buf)[2]),(char*)&blk,2);

    //Grab up to blkSize bytes of data.
    if((totalBytes - numBytesSent - blkSize) >= 0)
    {
        memcpy( (&((*send_buf)[4])), &buffer[numBytesSent], blkSize);
        bytesRead = blkSize;
        numBytesSent += bytesRead;
    }
    else
    {
        memcpy( (&((*send_buf)[4])), &buffer[numBytesSent],
            (totalBytes - numBytesSent));
        bytesRead = (totalBytes - numBytesSent);
        numBytesSent += bytesRead;
    }

    total = bytesRead;
    total = total+4;

    return total;
}




/****************************************************************************/
/*Function:  parse_ack                                                      */
/*Purpose:   to parse an ack received from a udp connection.  It will ensure*/
/*           that the ack is properly formed and is from the right client   */
/*Input:     the sockaddr_in struct from the recvfrom client, the           */
/*           sockaddr_in struct from the initial client recvfrom, the buffer*/
/*           from the last recvfrom, the size of the buffer, the locally    */
/*           bound socket, and the previously sent block number             */
/*Return: 0 if the ack corresponds to the last sent block number,           */
/*        -1 if otherwise                                                   */
/****************************************************************************/
int parse_ack(struct sockaddr_in remote,struct sockaddr_in check, char* buf, 
              int size, SOCKET sock, int chk_block)
{
    unsigned short temp;

    //Check the ip address and port of the acking client
    if( (memcmp(&remote.sin_addr,&check.sin_addr,sizeof(&check.sin_addr))!=0) )
    {
        Send_Error(ERR_UNKNOWN_PORT,"Unexpected_Port",remote, sock);  //BAD TID
        printf("Expected packet from port %hd, got from port %hd.\n",htons(remote.sin_port),htons(check.sin_port));
        return -1;
    }
#if DEBUG_TFTP
    else
    {
        printf("Success, Expected packet from port %hd, got from port %hd.\n",htons(remote.sin_port),htons(check.sin_port));
    }
#endif

    //Check the opcode, should be OPC_ACK (0x04)
    memcpy((char*)&temp,buf,2);
    temp = ntohs(temp);
    if(temp != OPC_ACK)
    {
        printf("Expected an ack, opcode did not match that of ack\n");
        return -1;
    }

    //Check the block number, compare it to the expected block #
    //if they match, return 0.  This will tell the program that an ACK
    //for the previously sent block of data was received.
    memcpy((char*)&temp,&buf[2],2);
    temp = ntohs(temp);
    if(temp != ((unsigned short)chk_block))
    {
#if DEBUG_TFTP
        printf("Recvd ack, but with unexpected block number %hu\n",temp);
#endif
        return -1;
    }

    return 0;
}




/****************************************************************************/
/*Function: sendPeerMsg                                                     */
/*Purpose:  Sends a requested block of data to a peer.                      */
/*Return: 0 on success, -1 on failure.                                      */
/****************************************************************************/
int sendPeerMsg(char* filename,unsigned __int64 offset,
                unsigned short current_block,char* ipaddr,short port)
{
    HANDLE hFile = NULL;
    char* sendbuf = NULL;
    char ackbuf[32];
    int nextsize,asize,timeoutcnt,rtn;
    unsigned short tmp;
    unsigned short localport = 9000;
    DWORD bytesRead, bytesToRead;
    LARGE_INTEGER dist;
    SOCKET p2pSock = INVALID_SOCKET;
    struct sockaddr_in c_remote, remote;
    fd_set tset;
    struct timeval timeout;

    /* Set up remote peer info */
    c_remote.sin_family = AF_INET;
    c_remote.sin_port = htons(port);
    c_remote.sin_addr.s_addr = inet_addr(ipaddr);

    /* Open a new socket */
    while(1)
    {
        p2pSock = udpOpenBindSocket(localport);
        if(p2pSock != INVALID_SOCKET)
            break;
        localport++;
    }

    /* Allocate temporary send buffer */
    sendbuf = (char*)malloc(65464+4);
    if(sendbuf == NULL)
    {
        printf("Error allocating memory for P2P send buffer.\n");
        return -1;
    }

    /* Fill in data header */
    tmp = htons(OPC_DATA);
    memcpy(&sendbuf[0],&tmp,2);
    tmp = htons(current_block);
    memcpy(&sendbuf[2],&tmp,2);

    /* Attempt to open file */
    hFile = CreateFile(
       filename,                 /* lpFileName      */
       GENERIC_READ,             /* dwDesiredAccess */
       FILE_SHARE_READ,          /* dwShareMode */
       NULL,                     /* lpSecurityAttributes */
       OPEN_EXISTING,            /* dwCreationDisposition */
       FILE_FLAG_SEQUENTIAL_SCAN ,  /* dwFlagsAndAttributes */
       NULL                      /* hTemplateFile */
    );
    if(hFile == INVALID_HANDLE_VALUE)
    {
       printf("Cant open file %s.  Windows Error = %u\n",
           filename,GetLastError());
       Send_Error(ERR_FILE_NOT_FOUND,"",c_remote,p2pSock);
       return -1;
    }

    /* Move the file pointer */
    dist.QuadPart = offset;
    SetFilePointerEx(
        hFile,      /* HANDLE hFile, */
        dist,       /* LARGE_INTEGER lDistanceToMove, */
        NULL,       /* PLARGE_INTEGER lpNewFilePointer, */
        FILE_BEGIN  /* DWORD dwMoveMethod */
    );

    /* Read from disk */
    bytesToRead = 65464;
	if(ReadFile(hFile, &sendbuf[4], bytesToRead, &bytesRead, NULL) == 0)
	{
		printf("ReadFile failed.  Windows Error = %u\n",GetLastError());
		return -1;
	}
    nextsize = bytesRead;

    /* Close the file */
    CloseHandle(hFile);

    /*******************************/
    /* Send the data, wait for ack */
    /*******************************/
    udp_send(p2pSock,sendbuf,nextsize,c_remote);
#if DEBUG_TFTP
    printf("Sent DATA <block = %d, Data = %d bytes, FromPort=%hd ToPort=%hd>\n",
        current_block,nextsize,localport,htons(c_remote.sin_port));
#endif

    //Begin transferring the rest of the file using select()
    while(1)
    { 
        timeout.tv_sec = DEFAULT_SEC_TIMEOUT;
        timeout.tv_usec = 100000;  /* 100ms */
        
        FD_ZERO(&tset);
        FD_SET(p2pSock,&tset);
        rtn = select(((int)p2pSock+1),&tset, NULL, NULL, &timeout);

        if(FD_ISSET(p2pSock, &tset))
        {
            //Get the ACK
            asize = udp_recv(p2pSock,ackbuf,4,&remote);
            if(asize < 0)
            {
                timeoutcnt++;
                break;
            }
            if(parse_ack(remote,c_remote,ackbuf,asize,p2pSock,current_block) < 0)
            {
                udp_send(p2pSock,sendbuf,nextsize,c_remote); //resend last packet
                continue;  //bad ACK
            }
            else
            {
                //Successful P2P packet send
#if DEBUG_TFTP
                printf("Got Ack <block = %d, Data = %d bytes>\n",current_block,asize);
#endif
                break;
            }
        }

        if(rtn == 0)
        { 
            //Timeout
            timeoutcnt++;
            if(timeoutcnt == MAX_TIMEOUT_COUNT)
            { 
                //kill connection
                printf("P2P Session timed out, killing session with client\n");
                if(sendbuf != NULL)
                    free(sendbuf);
                closesocket(p2pSock);
				return -1;
            }
            else
            {  
                //Re-send the last packet
                udp_send(p2pSock,sendbuf,nextsize,c_remote);
#if DEBUG_TFTP
                printf("P2P Sent DATA <block = %d, Data = %d bytes>\n",
                    current_block,nextsize);
#endif
            }
        }
        else if(rtn < 0)
        { 
            //Select failed -- nonfatal error
            reportError("Select failed in a thread, continuing...",
                WSAGetLastError(),1);
            timeoutcnt++;
        }
    }

    /* Free Resources and exit */
    closesocket(p2pSock);
    if(sendbuf != NULL)
    {
        free(sendbuf);
        sendbuf = NULL;
    }

    return 0;
}




/****************************************************************************/
/*Function: Send_Error                                                      */
/*Purpose:  Informs the client an error has occurred.                       */
/*Input:    Type of error, an error message, the client's sockaddr_in info, */
/*          and the locally bound socket to use for sending the data        */
/*Return: nothing                                                           */
/****************************************************************************/
void Send_Error(int errortype, char* add_msg,struct sockaddr_in remote,
                SOCKET sock)
{  

    int strlength;
    short temp;
    char* sending_buf = NULL;
    
    char* errors[] = {"File not found.","Access violation.",
        "Disk full or allocation exceeded.",
        "Illegal TFTP operation","Unknown transfer ID.",
        "File already exists.","No such user.","Option Error"};

    if((errortype > ERR_NOT_DEFINED) && (errortype <= ERR_OPTION))
    {
        strlength = 4+(int)strlen(errors[errortype-1])+1;
    }
    else if(errortype == ERR_NOT_DEFINED)
    {
        strlength = 4+(int)strlen(add_msg)+1;
    }
    else
    {
        printf("Send_Error: Argument Error\n");
    }

    if( (sending_buf = (char*)malloc(strlength)) == NULL)
    {
        reportError("Error allocating memory in Send_Error for error msg, returning.",0,0);
        return;
    }

    //Copy in OPC_ERROR (0x05) into the buffer and the error code in the first 4 bytes
    temp = htons(OPC_ERROR);
    memcpy(sending_buf,(char*)&temp,2);
    temp = htons(errortype);
    memcpy(&sending_buf[2],(char*)&temp,2);

    //Copy the string into the buffer with its null terminator
    if(errortype > 0)
        strncpy(&sending_buf[4],errors[errortype-1],strlen(errors[errortype-1])+1);
    else
        strncpy(&sending_buf[4],add_msg,strlen(add_msg)+1);

    //Send the Error Message
    udp_send(sock,sending_buf,strlength,remote);
    printf("Sent error message to client, error number %d: %s\n",errortype,&sending_buf[4]);

    //Free the buffer
    if(sending_buf != NULL)
        free(sending_buf);
    return;
}
