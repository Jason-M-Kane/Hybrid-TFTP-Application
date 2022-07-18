/*****************************************************************************/
/* File name: htftp_server.c                                                 */
/* Author: Jason Kane                                                        */
/* Date:    1/16/10                                                          */
/* Version  1.11                                                             */
/*                                                                           */
/* Program Information                                                       */
/*---------------------------------------------------------------------------*/
/* What it does: a hybrid tftp server that will run concurrently.            */
/* It is based on the protocol developed by Robert Hernandez.                */
/* It takes 4 parameters: port to run on, max # simult connections, file     */
/* share path.                                                               */
/*                                                                           */
/* Useage:  tftp_server.exe port max_conn sharepath readBufferSizeBytes      */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opcode_definitions.h"
#include "sockfctns.h"
#include "cmn_util.h"
#include "tftp_operations.h"
#include "FileListing.h"
#include "connectionManagement.h"


/***********/
/* Globals */
/***********/
unsigned short G_CLIENTPORT = MIN_CLIENT_PORT;
extern int MAX_DISKREAD_BUFFER_SIZE;


/***********************/
/* Function Prototypes */
/***********************/
void begin_server(unsigned short port, int maximumConnections, char* share);
int testAutoDetect(char* buff);
int sendAutoDetectReply(SOCKET sock, struct sockaddr_in to);
int testDirRqst(char* buff);
int sendDirectoryListing(SOCKET sock, struct sockaddr_in to, 
                         char* fsListing);
int testConnRqst(char* buff);
int sendConnRqstReply(SOCKET sock, struct sockaddr_in to, int desiredConn);
int valid_rrq(char* rrq, int size, char** filename, char** mode,optsData* opt);
int valid_resume(char* rrq, int size, char** filename,char** mode,optsData* opt);
DWORD WINAPI Execute_Connection(LPVOID sockdata);




/*****************************************************************************/
/*Function:  Main Function                                                   */
/*Purpose:  Main execution of program.  Starts the process of running the    */
/*          server.                                                          */
/*Input: 1 argument, the port to listen on.                                  */
/*Return: 0 upon successful completion                                       */
/*****************************************************************************/
int main(int argc, char*argv[])
{
    int port, maxConnections;
    char sharePath[MAX_PATH];

    //Handle the command line inputs
    if(argc == 1)
    {
        /* Use the default settings */
        port = 69;
        maxConnections = 100;
        strcpy(sharePath,"C:\\share\\");
        MAX_DISKREAD_BUFFER_SIZE = (4*1024*1024);
    }
    else if(argc != 5)
    {
        printf("This program requires 4 arguments: \n"
            "\t1.) The port to run on.\n"
            "\t2.) The max # of simultaneous connections.\n"
            "\t3.) The root share directory.\n"
            "\t4.) The size of the read buffer in bytes.\n");
        hitAKeyToExit();
        return -1;
    }
    else
    {
        port = atoi(argv[1]);
        maxConnections = atoi(argv[2]);
        memset(sharePath,0,MAX_PATH);
        strncpy(sharePath,argv[3],MAX_PATH-1);
        if(sharePath[strlen(sharePath)-1] != '\\')
            sharePath[strlen(sharePath)] = '\\';
        MAX_DISKREAD_BUFFER_SIZE = atoi(argv[4]);
    }

    //Test the input parameters for invalid values
    if((port < 0) || (port > 65535))
    {
        printf("Atoi returned invalid port to operate on, exiting...\n");
        hitAKeyToExit();
        return -1;
    }
    if(maxConnections <= 0)
    {
        printf("Error, maximum connections must be greater than 0, exiting...\n");
        hitAKeyToExit();
        return -1;
    }

    //Initialize Logging
    if(initLogging() < 0)
    {
        printf("Logging Initialization Failed.\n");
        hitAKeyToExit();
        return -1;
    }

    //Start the server
    begin_server((unsigned short)port, maxConnections, sharePath);

    hitAKeyToExit();
    return 0;
}




/****************************************************************************/
/*Function:  begin_server                                                   */
/*Purpose:   startup and initialize the tftp udp server.                    */
/*Input: the port to listen on, maximum # of connections.                   */
/*Return: nothing                                                           */
/****************************************************************************/
void begin_server(unsigned short port, int maximumConnections, char* share)
{
    char* fileSystemListing;
    static char inbuff[MAX_PSIZE];         /* Request Buffer           */
    struct connection_data *data;          /* Ptr to data to be passed */
    SOCKET listeningSocket;                /* Udp Server Socket        */
    struct sockaddr_in from;               /* Used for select */
    fd_set etherfds;                       /* Used for select */
    int rtn, rval, size, clientIndex, numDesiredConnections;
    HANDLE hThread = NULL;
    DWORD ThreadId = 0;
    listeningSocket = INVALID_SOCKET;      /* Init */

    /* Prepare the file system contents */
    fileSystemListing = NULL;
    if(createFileSystemListing(share, &fileSystemListing) < 0)
    {
        reportError("Error creating File System Listing, aborting.\n",0,0);
        return;
    }

    /* Build the connection table */
    if(createConnectionTable(maximumConnections) < 0)
    {
        reportError("Error creating connection table, aborting.\n",0,0);
        return;
    }

    /* Start up WinSock */
    if(initializeWinsock() < 0)
    {
        reportError("Call to initializeWinsock() failed, aborting.\n",0,0);
        return;
    }

    /* Listen on the specified server udp port */
    if((listeningSocket = udpOpenBindSocket(port)) == INVALID_SOCKET)
    {
        reportError("Call to udpOpenBindSocket() failed, aborting.\n",0,0);
        return;
    }

	/* Start up the Connection Monitor Thread */
	hThread = CreateThread(NULL,0,Monitor_Connections,NULL,0,&ThreadId);
    if(hThread == NULL)
    {
        reportError("Error, unable to spawn monitor thread",0,0);
		return;
    }

    printf("Hybrid TFTP Server Running on Port %hd\n",port);
    printf("======================================\n");

    /*************************************************************************/
    /* Set up select to keep waiting for new connections to come in on the   */
    /* listening port.  Respond to: Autodetect packets, directory listing    */
    /* request packets, Connection Request Packets, or RRQ packets.  Only    */
    /* respond to a RRQ packet if a connection was previously established.   */
    /*************************************************************************/
    while(1)
    {
        FD_ZERO(&etherfds);
        FD_SET(listeningSocket,&etherfds);
        rval = select(((int)listeningSocket)+1,&etherfds,NULL,NULL,NULL);


        /* Check for activity detected on the port. */
        if(FD_ISSET(listeningSocket, &etherfds))
        {
            //Receive a Packet
            size = udp_recv(listeningSocket, inbuff, MAX_PSIZE,&from);
            if(size < 0)
            {
                reportError("Error Recving Packet on port 69.\n",0,0);
                continue;
            }

            /***************************************************************/
            /* Handle the Broadcast Packets. (Port 6969)                   */
            /* Check for an Autodetect Request or Directory Request Packet */
            /***************************************************************/

            //Autodetect Rqst Packet
            if(testAutoDetect(inbuff) > 0)
            {
                sendAutoDetectReply(listeningSocket, from);
                continue;
            }

            //Directory Rqst Packet
            if(testDirRqst(inbuff) > 0)
            {
                sendDirectoryListing(listeningSocket, from, fileSystemListing);
                continue;
            }

            /*************************************/
            /* Handle Connection Request Packets */
            /*************************************/
            if( (numDesiredConnections=testConnRqst(inbuff)) != -1)
            {
                sendConnRqstReply(listeningSocket, from, numDesiredConnections);
                continue;
            }


            /********************************************************/
            /* Handle File Request Packets from "Connected" Clients */
            /********************************************************/

            /* Ignore Clients that are not connected */
            if( (clientIndex=searchForConnectedClient(inet_ntoa(from.sin_addr))) < 0)
            {
                reportError("Ignoring request from client that is not connected.\n",0,0);
                continue;
            }

            /* The client is connected, handle its file read requests */
            data = (struct connection_data *)malloc(sizeof(struct connection_data));
            if(data == NULL)
            {
                reportError("Error allocating memory to connection_data struct",0,0);
                continue; //dont quit the program, just start over at the while loop
            }
            data->filename = data->mode = NULL;   //init

            memcpy(&data->remote,&from,sizeof(from));
            data->options.blkSizeNeg = FALSE;
			data->options.timeoutNeg = FALSE;
			data->options.tsizeNeg = FALSE;
			data->options.blkSize = MAX_DATA_SIZE;

            //Start a new thread to handle the connection, if its a RRQ
            if((rtn = valid_rrq(inbuff,size,&data->filename,&data->mode,&data->options)) > 0)
            {
                printf("Valid RRQ recevd from a client, beginning connection...\n");

                hThread = CreateThread(NULL,0,Execute_Connection,data,0,&ThreadId);
	            if(hThread == NULL)
	            {
		            reportError("Error, unable to spawn tftp thread",0,0);
                    if(data->filename != NULL)
                        free(data->filename);
                    if(data->mode != NULL)
                        free(data->mode);
                    free(data);
                    continue;
	            }
            }
            else if((rtn = valid_resume(inbuff,size,&data->filename,&data->mode,&data->options)) > 0)
            {
                printf("Valid Resume RQ recevd from a client, beginning connection...\n");

                hThread = CreateThread(NULL,0,Execute_Connection,data,0,&ThreadId);
	            if(hThread == NULL)
	            {
		            reportError("Error, unable to spawn tftp thread",0,0);
                    if(data->filename != NULL)
                        free(data->filename);
                    if(data->mode != NULL)
                        free(data->mode);
                    free(data);
                    continue;
	            }
            }
            else if(rtn < 0)
            {   
                //Invalid TFTP operation from client
                reportError("Invalid Request from client recvd on server port.\n",0,0);
                Send_Error(ERR_ILLEGAL_TFTP_OPERATION,"",from,listeningSocket);
                if(data->filename != NULL)
                    free(data->filename);
                if(data->mode != NULL)
                    free(data->mode);
                free(data);
            }
        }
        else if(rval == SOCKET_ERROR)
        {
            reportError("Select Error Detected.",WSAGetLastError(),1);
            break;
        }
    }

    /* Free the file system listing */
    if(fileSystemListing != NULL)
    {
        free(fileSystemListing);
        fileSystemListing = NULL;
    }

    return;
}




/****************************************************************************/
/*Function:  testAutoDetect                                                 */
/*Purpose:   Determines if the packet is an autodetect packet.              */
/*Input: the buffer containing the packet.                                  */
/*Return: -1 if not a valid Autodetect, 1 if it is a valid Autodetect.      */
/****************************************************************************/
int testAutoDetect(char* buff)
{
    char tmp[32];
    unsigned short id;

    /* Create the comparison packet */
    memset(tmp,0,32);
    id = htons(OPC_AUTOD);
    memcpy(&tmp[0],&id,2);
    strcpy(&tmp[2],"TFTP_AUTOD");

    if(memcmp(tmp,buff,13) != 0)
    {
        return -1;
    }

    /* Match Detected */
    return 1;
}




/****************************************************************************/
/*Function:  sendAutoDetectReply                                            */
/*Purpose:   Sends an autodetect reply packet.                              */
/*Input: the socket to send the packet on, a sockaddr_in struct to          */
/*       determine where to send the packet.                                */
/*Return: 0 on success.                                                     */
/****************************************************************************/
int sendAutoDetectReply(SOCKET sock, struct sockaddr_in to)
{
    char tmp[32];
    char maxClients, currentClients;
    unsigned short id;

    /* Create the autodetect reply packet */
    memset(tmp,0,32);
    id = htons(OPC_AUTODR);
    memcpy(&tmp[0],&id,2);
    strcpy(&tmp[2],"TFTP_AUTODR");
    maxClients = (char)getMaxConnections();
    currentClients = (char)getNumAvailConnections();
    tmp[15] = maxClients;
    tmp[16] = currentClients;

    /* Send the packet */
    udp_send(sock,tmp,17,to);

    return 0;
}




/****************************************************************************/
/*Function:  testDirRqst                                                    */
/*Purpose:   Determines if the packet is a directory request.               */
/*Input: the buffer containing the packet.                                  */
/*Return: -1 if not a valid DIRRQ, 1 if it is a valid DIRRQ.                */
/****************************************************************************/
int testDirRqst(char* buff)
{
    char tmpA[32];
    char tmpB[32];
    unsigned short id;

    /* Create the comparison packets */
    memset(tmpA,0,32);
    id = htons(OPC_DIRRQ);
    memcpy(&tmpA[0],&id,2);
    strcpy(&tmpA[2],"TFTP_DIRRQ");
    strcpy(&tmpA[13],"C/S");
    memcpy(tmpB,tmpA,32);
    strcpy(&tmpB[13],"P2P");

    if( (memcmp(tmpA,buff,17) != 0) &&
        (memcmp(tmpB,buff,17) != 0) )
    {
        return -1;
    }

    /* Match Detected */
    return 1;
}




/****************************************************************************/
/*Function:  sendDirectoryListing                                           */
/*Purpose:   Sends a directory listing using normal TFTP with hardcoded     */
/*           options.                                                       */
/*Input: the socket to send the packet on, a sockaddr_in struct to          */
/*       determine where to send the packet.                                */
/*Return: 0 on success.                                                     */
/****************************************************************************/
int sendDirectoryListing(SOCKET sock, struct sockaddr_in to, 
                         char* fsListing)
{
    struct connection_data *data;          /* Ptr to data to be passed */
    HANDLE hThread = NULL;
    DWORD ThreadId = 0;

    /* Allocate data to be passed into transfer function */
    data = (struct connection_data *)malloc(sizeof(struct connection_data));
    if(data == NULL)
    {
        reportError("Error allocating memory to connection_data struct",0,0);
        return -1;
    }
    data->filename = data->mode = NULL;   //init
    data->mode = malloc(32);
    data->filename = malloc(32);
    if((data->mode == NULL) || (data->filename == NULL) )
    {
        reportError("Error could not allocate memory for mode/fname",0,0);
        return -1;
    }
    strcpy(data->mode,"dirrq");
    strcpy(data->filename,"dirrq");

    memcpy(&data->remote,&to,sizeof(to));
    data->options.blkSizeNeg = TRUE;
	data->options.timeoutNeg = TRUE;
	data->options.tsizeNeg = TRUE;
	data->options.blkSize = 65464;
    data->options.tsize = strlen(fsListing)+1;
    data->options.timeout = 5;
    data->dirListing = fsListing;

    //Start a new thread to handle the directory connection
    hThread = CreateThread(NULL,0,Execute_Connection,data,0,&ThreadId);
    if(hThread == NULL)
    {
	    reportError("Error, unable to spawn thread for dirlisting",0,0);
        if(data->filename != NULL)
            free(data->filename);
        if(data->mode != NULL)
            free(data->mode);
        free(data);
        return -1;
    }

    return 0;
}




/****************************************************************************/
/*Function:  testConnRqst                                                   */
/*Purpose:   Determines if the packet is a connection request.              */
/*Input: the buffer containing the packet.                                  */
/*Return: -1 if not a valid CONRQ, # of desired connections if valid.       */
/****************************************************************************/
int testConnRqst(char* buff)
{
    char tmpA[32];
    char tmpB[32];
    unsigned short id;

    /* Create the comparison packets */
    memset(tmpA,0,32);
    id = htons(OPC_CON_RQST);
    memcpy(&tmpA[0],&id,2);
    strcpy(&tmpA[2],"TFTP_CONRQ");
    strcpy(&tmpA[13],"C/S");
    memcpy(tmpB,tmpA,32);
    strcpy(&tmpB[13],"P2P");

    if( (memcmp(tmpA,buff,17) != 0) &&
        (memcmp(tmpB,buff,17) != 0) )
    {
        return -1;
    }

    /* Match Detected */
    return ((int)buff[17]);
}




/****************************************************************************/
/*Function:  sendConnRqstReply                                              */
/*Purpose:   Sends a connection reply packet.                               */
/*Input: the socket to send the packet on, a sockaddr_in struct to          */
/*       determine where to send the packet.                                */
/*Return: 0 on success.                                                     */
/****************************************************************************/
int sendConnRqstReply(SOCKET sock, struct sockaddr_in to, int desiredConn)
{
    char tmp[32];
    char allowedConnections;
    unsigned short id;
    int clientIndex, sendSize = 0;
    int deny = FALSE;

    /* Determine if the connection already exists, Update if so. */
    /* Otherwise, add the connection to the table.               */
    if( (clientIndex=searchForConnectedClient(inet_ntoa(to.sin_addr))) != -1)
    {
        /* Update the connection */
		if( (allowedConnections = 
            updateClientEntry(clientIndex, desiredConn, ntohs(to.sin_port))) < 0)
		{
            deny = TRUE;
        }
		else
		{
			deny = FALSE;
		}
    }
    else
    {
        if( (allowedConnections=(char)addConnection(desiredConn, 
            ntohs(to.sin_port), inet_ntoa(to.sin_addr))) < 0)
        {
            deny = TRUE;
        }
    }

    /* Create the connection reply packet */
    memset(tmp,0,32);
    id = htons(OPC_CON_RPLY);
    memcpy(&tmp[0],&id,2);
    strcpy(&tmp[2],"TFTP_CONRQR");
    if(deny)
    {
        strcpy(&tmp[14],"DECLINE");
        tmp[22] = 0;
        sendSize = 23;
    }
    else
    {
        strcpy(&tmp[14],"ACCEPT");
        tmp[21] = allowedConnections;
        sendSize = 22;
    }

    /* Send the packet */
    udp_send(sock,tmp,sendSize,to);

    return 0;
}




/****************************************************************************/
/*Function:  valid_rrq                                                      */
/*Purpose:   Determines if the input string is a valid read request.        */
/*           Will also parse out the filename and mode of the request.      */
/*Format is:  01 filename 0 mode 0, 01 is in network byte order             */
/*                                                                          */
/*Input: a buffer fill with the supposed RRQ, the buffer size, a char       */
/*       pointer to hold the filename, and a char ptr to hold the mode.     */
/*Return: -1 if not a valid RRQ, 0 if its an error msg from the client,     */
/*        1 if it is a valid RRQ.                                           */
/****************************************************************************/
int valid_rrq(char* rrq, int size, char** filename, char** mode,optsData* opt)
{
    char* ptr;
    int filestrLen, modestrLen;
    short opcode;
    int length,cntr;
    int rval = -1;

    //Minimum possible size check: opcode and 2 null characters
    if(size < 4)
    {
        return -1;
    }

    //Extract the first two bytes and put them in host byte order
    //Then check to see if they are equal to 0x01
    memcpy((char*)&opcode,rrq,2);
    opcode = ntohs(opcode);

    if(opcode == OPC_READ_REQ)
    {  
        //If its a rrq, this is not a resumed download
        opt->resume = FALSE;
        opt->offset = 0;

        //Its a RRQ,extract mode & filename
        filestrLen = extract_string(&rrq[2],filename,size-2);
        if(filestrLen < 0)
        {
            return -1;
        }
        modestrLen  = extract_string(&rrq[2+filestrLen],mode,size-filestrLen-2);
        if(modestrLen < 0)
        {
            return -1;
        }

        if((strcasecmp(*mode,"octet")!=0))
            return -1;

        //Attempt to Extract Options Information
        //Supports Block Size (8 to 65464), Timeout, and Transfer Size Options
        size = size - modestrLen - filestrLen-2;
        cntr = 2+filestrLen+modestrLen;
        ptr = NULL;
        while(size > 0)
        {
            length  = extract_string(&rrq[cntr],&ptr,size);

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
                length  = extract_string(&rrq[cntr],&ptr,size);
                if(length < 0)
                {
                    break;
                }
                size -= length;
                cntr += length;
                opt->blkSize = atoi(ptr);
                if((opt->blkSize >= 8) && (opt->blkSize <= 65464))
                {
                    opt->blkSizeNeg = TRUE;
                }
            }
            else if(strcmp(ptr,"timeout") == 0)
            {
                if(ptr != NULL)
                    free(ptr);
                length  = extract_string(&rrq[cntr],&ptr,size);
                if(length < 0)
                {
                    break;
                }
                size -= length;
                cntr += length;
                opt->timeout = atoi(ptr);
                if((opt->timeout >= 1) && (opt->timeout <= 255))
                {
                    opt->timeoutNeg = TRUE;
                }
            }
            else if(strcmp(ptr,"tsize") == 0)
            {
                if(ptr != NULL)
                    free(ptr);
                length  = extract_string(&rrq[cntr],&ptr,size);
                if(length < 0)
                {
                    break;
                }
                size -= length;
                cntr += length;
                opt->tsize = _atoi64(ptr);
                opt->tsizeNeg = TRUE;
            }
            else
            {
                printf("Unknown Option Detected, Ignoring.\n");
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

        return 1;  //Everything is okay
    }
    else if(opcode == OPC_ERROR)
    {
        return 0;  //Client sent back some sort of error, ignore it
    }

    //If you get here, you encountered an unexpected opcode
    return -1;
}




/****************************************************************************/
/*Function:  valid_resume                                                   */
/*Purpose:   Determines if the input string is a valid resume request.      */
/*           Will also parse out the filename and mode of the request.      */
/*Format is:  90 TFTP_P2P_RESTART\0 filename \0 file_start_offset \0        */
/*                                                                          */
/*Input: a buffer fill with the supposed ResumeRQ, the buffer size, a char  */
/*       pointer to hold the filename, and a char ptr to hold the mode.     */
/*Return: -1 if not a valid RRQ, 0 if its an error msg from the client,     */
/*        1 if it is a valid RRQ.                                           */
/****************************************************************************/
int valid_resume(char* rrq, int size, char** filename,char** mode,optsData* opt)
{
	static char temp[300];
    char* tmpOffsetBuf = NULL;
    int filestrLen, idstrLen, offsetstrLen;
    short opcode;
    int rval = -1;

    //Minimum possible size check: opcode,IDstring, and 4 other characters
    if(size < 23)
    {
        return -1;
    }

    //Extract the first two bytes and put them in host byte order
    //Then check to see if they are equal to 0x90
    memcpy((char*)&opcode,rrq,2);
    opcode = ntohs(opcode);

	memcpy(temp,rrq,size);
    if(opcode == OPC_RESUME)
    {  
        //Extract ID name
        idstrLen = extract_string(&rrq[2],mode,size-2);
        if(idstrLen < 0)
        {
            return -1;
        }
        if(strcmp(*mode,"TFTP_P2P_RESTART") != 0)
        {
            return -1;
        }
        else
        {
            free(*mode);
            *mode = NULL;
        }

        //Extract Filename
        filestrLen  = extract_string(&rrq[2+idstrLen],filename,size-idstrLen-2);
        if(filestrLen < 0)
        {
            return -1;
        }
       
        //Extract File offset to start from
        offsetstrLen  = extract_string(&rrq[2+idstrLen+filestrLen],
            &tmpOffsetBuf,size-idstrLen-filestrLen-2);
        if(offsetstrLen < 0)
        {
            if(tmpOffsetBuf != NULL)
            {
                free(tmpOffsetBuf);
                tmpOffsetBuf = NULL;
            }
            return -1;
        }
        opt->resume = TRUE;
        opt->offset = (unsigned __int64)_atoi64(tmpOffsetBuf);
        if(tmpOffsetBuf != NULL)
        {
            free(tmpOffsetBuf);
            tmpOffsetBuf = NULL;
        }

        ////////////////////////////////////////////////////////////////////
        // Its a resume, Set up the default options - Make the mode octet //
        ////////////////////////////////////////////////////////////////////
        if( extract_string("octet",mode,6) < 0)
        {
            return -1;
        }
        opt->blkSize = 65464;
        opt->blkSizeNeg = TRUE;
        opt->timeoutNeg = FALSE;
        opt->tsizeNeg = TRUE;
        opt->tsize = 0;

        return 1;  //Everything is okay
    }
    else if(opcode == OPC_ERROR)
    {
        return 0;  //Client sent back some sort of error, ignore it
    }

    //If you get here, you encountered an unexpected opcode
    return -1;
}




/****************************************************************************/
/*Function:  Execute_Connection                                             */
/*Purpose:   Starts thread execution.  Binds to a new port and runs the     */
/*           function that corresponds with the proper transfer mode        */
/*Input: void pointer to thread data.                                       */
/*Return: nothing                                                           */
/****************************************************************************/
DWORD WINAPI Execute_Connection(LPVOID sockdata)
{
    SOCKET local;
    int len,fail,x;
    struct sockaddr_in remote;
    unsigned short secondPort;
    char *filename = NULL;

	/* Set the connection status to downloading */
	setConnectionStatus(
        inet_ntoa(((struct connection_data*)sockdata)->remote.sin_addr),
        CONNECTION_DLING);

    //Copy the values passed in by the void pointer
    len = sizeof(struct sockaddr_in);
    memcpy(&remote,&(((struct connection_data*)sockdata)->remote),len);

    //Obtain a new port to use for the new connection and bind it
    x=0;
    local = INVALID_SOCKET;
    while(local == INVALID_SOCKET)
    {
        secondPort = G_CLIENTPORT++;
        if(G_CLIENTPORT >= MAX_CLIENT_PORT)
            G_CLIENTPORT = MIN_CLIENT_PORT;

        local = udpOpenBindSocket(secondPort);
        if((local == INVALID_SOCKET) && (x==100))
        {
            reportError("Error, could not open 2nd connection socket",0,0);
            x=0;
        }
        x++;
    }

    //Get the filename that will be read
    len = (int)strlen( (((struct connection_data*)sockdata)->filename) );
    if( (filename = malloc(len+1)) == NULL)
    {
        reportError("Error, allocating filename in Execute_Connection",0,0);
        fail = 1;
    }
    else
    {
        strncpy(filename,((struct connection_data*)sockdata)->filename,len+1);
        fail = 0;
    }

    //Mode is either dirrq, netascii or octet by this point.  Dont need to do
    //any further checking to ensure this
    if((!fail) && (local != -1))
    {
        if(strcasecmp("dirrq",((struct connection_data*)sockdata)->mode)==0)
        {
            //run directory send mode
            dir_transfer(local,remote,((struct connection_data*)sockdata)->dirListing,
                ((struct connection_data*)sockdata)->options); 
        }
        else if(strcasecmp("octet",((struct connection_data*)sockdata)->mode)==0)
        {
            //run octect mode
            octet_transfer(local,remote,filename,((struct connection_data*)sockdata)->options);
        }
        else
        {
            printf("Error, unknown mode.\n"); //Weird mode, shouldnt get here
            setConnectionStatus(inet_ntoa(((struct connection_data*)sockdata)->remote.sin_addr), CONNECTION_IDLE);
            //Free the data pointed to by the void pointer
            if( (((struct connection_data*)sockdata)->mode) != NULL)
                free((((struct connection_data*)sockdata)->mode));
            if( (((struct connection_data*)sockdata)->filename) != NULL)
                free((((struct connection_data*)sockdata)->filename));
            if((struct connection_data*)sockdata != NULL)
                free((struct connection_data*)sockdata);
            if(filename != NULL)
                free(filename);
            closesocket(local);
            ExitThread(0);
        }
    }

	/* Set the connection status back to idle */
	setConnectionStatus(inet_ntoa(((struct connection_data*)sockdata)->remote.sin_addr), CONNECTION_IDLE);
    printf("File Transfer of %s Completed.\n",(((struct connection_data*)sockdata)->filename));

    //Free the data pointed to by the void pointer
    if( (((struct connection_data*)sockdata)->mode) != NULL)
        free((((struct connection_data*)sockdata)->mode));
    if( (((struct connection_data*)sockdata)->filename) != NULL)
        free((((struct connection_data*)sockdata)->filename));
    if((struct connection_data*)sockdata != NULL)
        free((struct connection_data*)sockdata);

    //Done with connection, terminate the thread
    if(filename != NULL)
        free(filename);
    closesocket(local);

    ExitThread(0);
    return 0;
}
