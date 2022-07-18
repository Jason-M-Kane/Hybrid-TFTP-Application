/*****************************************************************************/
/* initialComms.cpp - Functions used to discover servers, get directory      */
/* listings, initiate connections, etc.                                      */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <windows.h>
#include "opcode_definitions.h"
#include "cmn_util.h"
#include "sockfctns.h"
#include "serverDatabase.h"
#include "clientGetDirListing.h"


/***********************/
/* Function Prototypes */
/***********************/
int sendServerDiscoveryRqst(SOCKET sock, int timeoutSeconds);
int serverFailDetect(char* ipaddress);
static int checkAutoDRMsg(char* inbuff,int* maxClients,int* currentClients);
int getServerDirectoryListing(char* serverIPAddress);
int initiateConnection(SOCKET sock, char* ipaddress, int timeoutSeconds,
                       int desiredConnects, int* allowedConnects);
static int checkConnReplyMsg(char* inbuff,int* accept,int* allowedConnects);




/****************************************************************************/
/*Function:  sendServerDiscoveryRqst                                        */
/*Purpose:   Sends a server discovery request message.                      */
/*Input: the socket to send the packet on, a sockaddr_in struct to          */
/*       determine where to send the packet.                                */
/*Return: # of replies on success, 0 if no replies, -1 if error.            */
/****************************************************************************/
int sendServerDiscoveryRqst(SOCKET sock, int timeoutSeconds)
{
    struct sockaddr_in to;
    char tmp[32];
    char inbuff[MAX_PSIZE];
    int maxClients, currentClients;
    unsigned short id;
    timeval timeoutVal;
    timeoutVal.tv_sec = timeoutSeconds;
    timeoutVal.tv_usec = 0;
    int rval, size;
    int replies = 0;
    fd_set etherfds;

    /* Set up to broadcast to all servers */
    memset(&to,0,sizeof(struct sockaddr_in));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = inet_addr("255.255.255.255");
    to.sin_port = htons(69);

    /* Erase the linked list of server information */
    initServerList();

    /* Create the server discovery request message */
    memset(tmp,0,32);
    id = htons(OPC_AUTOD);
    memcpy(&tmp[0],&id,2);
    strcpy(&tmp[2],"TFTP_AUTOD");

    /* Send the packet */
    udp_send(sock,tmp,13,to);


    /********************************************************************/
    /* Wait to hear back from servers.  Add servers to the linked list. */
    /********************************************************************/
    rval = 1;
    while(rval != 0)
    {
        FD_ZERO(&etherfds);
        FD_SET(sock,&etherfds);
        rval = select(((int)sock)+1,&etherfds,NULL,NULL,&timeoutVal);
        if(rval == SOCKET_ERROR)
        {
            reportError("sendServerDiscoveryRqst Select Failed", WSAGetLastError(), TRUE);
            break;
        }

        /* Check for activity detected on the port. */
        if(FD_ISSET(sock, &etherfds))
        {
            //Receive a Packet
            size = udp_recv(sock, inbuff, MAX_PSIZE,&to);
            if(size < 0)
            {
                continue;
            }

            /* Ensure the Auto detect reply message is formatted properly */
            if(checkAutoDRMsg(inbuff,&maxClients,&currentClients) < 0)
            {
                continue;
            }
            replies++;

            /* Add the server to the linked list of servers */
            if(addToServerList(inet_ntoa(to.sin_addr),maxClients,currentClients)==1)
                replies--;
        }
    }

    return replies;
}




/****************************************************************************/
/*Function:  serverFailDetect                                               */
/*Purpose:   Sends server discovery request messages to determine if a      */
/*           particular remote server is still up.                          */
/*Input: ipaddress of the server to locate.                                 */
/*Return: 0 if the server replied, -1 if not.                               */
/****************************************************************************/
int serverFailDetect(char* ipaddress)
{
    SOCKET sock;
    struct sockaddr_in to;
    char tmp[32];
    char inbuff[MAX_PSIZE];
    int maxClients, currentClients;
    short port;
    unsigned short id;
    timeval timeoutVal;
    timeoutVal.tv_sec = 0;
    timeoutVal.tv_usec = 500000;
    int rval, size, x;
    fd_set etherfds;

    port = 8000;
    sock = INVALID_SOCKET;
    while( (sock=udpOpenBindSocket(port)) == INVALID_SOCKET )
    {
        port++;
    }

    for(x=0; x < 3; x++)
    {
        if(x > 0)
            timeoutVal.tv_usec = 100000;

        /* Set up to broadcast to all servers */
        memset(&to,0,sizeof(struct sockaddr_in));
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = inet_addr("255.255.255.255");
        to.sin_port = htons(69);

        /* Create the server discovery request message */
        memset(tmp,0,32);
        id = htons(OPC_AUTOD);
        memcpy(&tmp[0],&id,2);
        strcpy(&tmp[2],"TFTP_AUTOD");

        /* Send the packet */
        udp_send(sock,tmp,13,to);


        /****************************************************************************************/
        /* Wait to hear back from servers.  Search for a reply with ipaddress youre looking for */
        /****************************************************************************************/
        rval = 1;
        while(rval != 0)
        {
            FD_ZERO(&etherfds);
            FD_SET(sock,&etherfds);
            rval = select(((int)sock)+1,&etherfds,NULL,NULL,&timeoutVal);
            if(rval == SOCKET_ERROR)
            {
                reportError("serverFailDetect Select Failed", WSAGetLastError(), TRUE);
                break;
            }

            /* Check for activity detected on the port. */
            if(FD_ISSET(sock, &etherfds))
            {
                //Receive a Packet
                size = udp_recv(sock, inbuff, MAX_PSIZE,&to);
                if(size < 0)
                {
                    continue;
                }

                /* Ensure the Auto detect reply message is formatted properly */
                if(checkAutoDRMsg(inbuff,&maxClients,&currentClients) < 0)
                {
                    continue;
                }

                /* Check to see if the ipaddress matches the one youre looking for */
                if(strcmp(inet_ntoa(to.sin_addr),ipaddress) == 0)
                {
                    closesocket(sock);
                    return 0;
                }
            }
        }
    }

    closesocket(sock);
    return -1;
}




/****************************************************************************/
/*Function:  checkAutoDRMsg                                                 */
/*Purpose:   Determines if the packet is a discovery reply.                 */
/*Input: the buffer containing the packet, max # clients, current # clients.*/
/*Return: -1 if not a valid AUTODR, 0 otherwise.  Puts the max clients and  */
/*        current # of clients into the two variables that are passed in    */
/*        by reference.                                                     */
/****************************************************************************/
static int checkAutoDRMsg(char* inbuff,int* maxClients,int* currentClients)
{
    char tmpA[32];
    unsigned short id;

    /* Create the comparison packet */
    memset(tmpA,0,32);
    id = htons(OPC_AUTODR);
    memcpy(&tmpA[0],&id,2);
    strcpy(&tmpA[2],"TFTP_AUTODR");

    /* Compare */
    if( (memcmp(tmpA,inbuff,14) != 0) )
    {
        return -1;
    }

    /* Appears to be properly formatted packet */
    *maxClients = inbuff[14];
    *currentClients = inbuff[15];

    return 0;
}




/****************************************************************************/
/*Function:  getServerDirectoryListing                                      */
/*Purpose:   Gets the server directory listing and caches it.               */
/*Input:  the IP address of the server to get the directory listing of.     */
/*Return: -1 if a failure occured while trying to transfer the listing.     */
/*        0 on successful transfer and parse.                               */
/****************************************************************************/
int getServerDirectoryListing(char* serverIPAddress)
{
    char* destBuffer = NULL;
    
    /* Send the Packet and Receive the directory listing (normal TFTP) */
    if(getDirListing(serverIPAddress,&destBuffer) < 0)
    {
        return -1;
    }

    /* Cache the directory listing */
    cacheDirListing(destBuffer,serverIPAddress);
    free(destBuffer);

    /* Add the directory listing to the user treeview GUI after */
    /* returning from this function.                            */

    return 0;
}




/****************************************************************************/
/*Function:  initiateConnection                                             */
/*Purpose:   Initiates a "connection" with a server.                        */
/*Input:  the IP address of the server to get the directory listing of,     */
/*        the desired number of simultaneous connections, and an int that   */
/*        is passed by reference to store the allowed number of connections.*/
/*Return: -1 if a failure occured during connection initation.              */
/*        0 on successful connection initiation.                            */
/****************************************************************************/
int initiateConnection(SOCKET sock, char* ipaddress, int timeoutSeconds,
                       int desiredConnects, int* allowedConnects)
{
    struct sockaddr_in to;
    fd_set etherfds;
    char tmp[32];
    char inbuff[MAX_PSIZE];
    unsigned short id;
    int rtnval = -1;
    int rval, size;
    int accept = FALSE;
    timeval timeoutVal;
    timeoutVal.tv_sec = timeoutSeconds;
    timeoutVal.tv_usec = 0;


    /***********************************/
    /* Send connection request message */
    /***********************************/
    memset(&to,0,sizeof(struct sockaddr_in));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = inet_addr(ipaddress);
    to.sin_port = htons(69);

    /* Create the server discovery request message */
    memset(tmp,0,32);
    id = htons(OPC_CON_RQST);
    memcpy(&tmp[0],&id,2);
    strcpy(&tmp[2],"TFTP_CONRQ");
    strcpy(&tmp[13],"C/S");
    tmp[17] = (char)desiredConnects;

    /* Send the packet */
    udp_send(sock,tmp,18,to);


    /*********************************/
    /* Recv connection reply message */
    /*********************************/
    rval = 1;
    while(rval != 0)
    {
        FD_ZERO(&etherfds);
        FD_SET(sock,&etherfds);
        rval = select(((int)sock)+1,&etherfds,NULL,NULL,&timeoutVal);
        if(rval == SOCKET_ERROR)
        {
            reportError("Select Failed", WSAGetLastError(), TRUE);
            break;
        }

        /* Check for activity detected on the port. */
        if(FD_ISSET(sock, &etherfds))
        {
            //Receive a Packet
            size = udp_recv(sock, inbuff, MAX_PSIZE,&to);
            if(size < 0)
            {
                continue;
            }

            /* Check originating server */
            if(strcmp(inet_ntoa(to.sin_addr),ipaddress) != 0)
            {
                continue;
            }

            /* Ensure the connection reply message is formatted properly */
            if(checkConnReplyMsg(inbuff,&accept,allowedConnects) < 0)
            {
                continue;
            }
            else
            {
                if((accept == TRUE) && (allowedConnects > 0))
                {
                    updateServerConnectionTable(ipaddress,*allowedConnects);
                    rtnval = 0;
                }
                break;
            }
        }
    }

    return rtnval;
}




/****************************************************************************/
/*Function:  checkConnReplyMsg                                              */
/*Purpose:   Determines if the packet is a connection reply.                */
/*Input: the buffer containing the packet, accept?, allowed connections.    */
/*Return: -1 if not a valid Connection Reply, 0 otherwise.                  */
/****************************************************************************/
static int checkConnReplyMsg(char* inbuff,int* accept,int* allowedConnects)
{
    char tmpA[32];
    unsigned short id;

    /* Create the comparison packet */
    memset(tmpA,0,32);
    id = htons(OPC_CON_RPLY);
    memcpy(&tmpA[0],&id,2);
    strcpy(&tmpA[2],"TFTP_CONRQR");

    /* Compare */
    if( (memcmp(tmpA,inbuff,14) != 0) )
    {
        return -1;
    }

    /* Compare ACCEPT or DECLINE */
    if(inbuff[14] == 'A')
    {
        *accept = TRUE;
        *allowedConnects = inbuff[21];
    }
    else
    {
        *accept = FALSE;
        *allowedConnects = inbuff[22];
    }


    return 0;
}
