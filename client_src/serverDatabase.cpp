/*****************************************************************************/
/* serverDatabase.c - Routines for server database functionality.            */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <windows.h>
#include "cmn_util.h"
#include "sockfctns.h"
#include "opcode_definitions.h"


/* Data structures */
typedef struct serverInfo
{
    char ipaddress[32];
    int currentClients;
    int maxClients;
    int connected;
    serverInfo *next;
}serverInfo;


/***********/
/* Globals */
/***********/
serverInfo* headInfo;
int numServers = 0;


/***********************/
/* Function Prototypes */
/***********************/
int initServerList();
int addToServerList(char* ipaddr,int maxClients,int currentClients);
int getServerIpAddresses(char** ipStr);
int cacheDirListing(char* buffer, char* ipaddress);
int updateServerConnectionTable(char* ipaddress, int connections);
int getServerConnectionStatus(char* ipaddress);
DWORD WINAPI Connection_Keepalive_Thread(LPVOID lpParam);
int Disconnect_All();
char* getLoadValStr(char* ipaddress);
char* getUnconnectedServerIPaddr();




/*****************************************************************************/
/*Function: initServerList                                                   */
/*Purpose: Erase the linked list of server information.                      */
/*Returns: 0 on success.                                                     */
/*****************************************************************************/
int initServerList()
{
    int x;
    
    serverInfo* currentPtr = headInfo;
    serverInfo* nextPtr = NULL;

    if(numServers > 0)
    {
        nextPtr = headInfo->next;
        for(x = 0; x < numServers; x++)
        {
            free(currentPtr);
            if(nextPtr != NULL)
            {
                currentPtr = nextPtr;
                nextPtr = currentPtr->next;
            }
        }
    }

    /* Re-Init Globals */
    headInfo = NULL;
    numServers = 0;

    return 0;
}




/*****************************************************************************/
/*Function: addToServerList                                                  */
/*Purpose: Add the server to the linked list of servers.                     */
/*Returns: >= 0 on success, < 0 on failure.                                  */
/*****************************************************************************/
int addToServerList(char* ipaddr,int maxClients,int currentClients)
{
    serverInfo* ptr, *newEntry;

    if(headInfo == NULL)
    {
        /* This is the first entry in the linked list */
        headInfo = (serverInfo*)malloc(sizeof(serverInfo));
        if(headInfo == NULL)
        {
            return -1;
        }
        headInfo->currentClients = currentClients;
        headInfo->maxClients = maxClients;
        strcpy(headInfo->ipaddress, ipaddr);
        headInfo->connected = FALSE;
        headInfo->next = NULL;
        numServers++;
    }
    else
    {
        /* Go to the last entry, add a new one                   */
        /* If the server entry is found along the way, update it */
        /* But do not increment the total number of servers      */
        ptr = headInfo;
        if(strcmp(ptr->ipaddress,ipaddr) == 0)
        {
            ptr->currentClients = currentClients;
            ptr->maxClients = maxClients;
            ptr->connected = FALSE;
            return 1;
        }

        while(ptr->next != NULL)
        {
            ptr = ptr->next;
            if(strcmp(ptr->ipaddress,ipaddr) == 0)
            {
                ptr->currentClients = currentClients;
                ptr->maxClients = maxClients;
                ptr->connected = FALSE;
                return 1;
            }
        }

        newEntry = (serverInfo*)malloc(sizeof(serverInfo));
        if(newEntry == NULL)
        {
            return -1;
        }
        newEntry->currentClients = currentClients;
        newEntry->maxClients = maxClients;
        strcpy(newEntry->ipaddress, ipaddr);
        newEntry->next = NULL;
        newEntry->connected = FALSE;
        ptr->next = newEntry;
        numServers++;
    }

    return 0;
}




/*****************************************************************************/
/*Function: updateServerConnectionTable                                      */
/*Purpose: Update the connection table now that a connection has been made.  */
/*         Also update the number of connected clients to include you.       */
/*Returns: 0 on success, -1 on failure.                                      */
/*****************************************************************************/
int updateServerConnectionTable(char* ipaddress, int connections)
{
    int found = FALSE;
    serverInfo* ptr;

    if(headInfo == NULL)
        return -1;

    /* Go to the last entry, add a new one */
    ptr = headInfo;
    do
    {
        if(strcmp(ptr->ipaddress,ipaddress) == 0)
        {
            found = TRUE;
            ptr->connected = TRUE;
            ptr->currentClients += connections;
            break;
        }
        ptr = ptr->next;
    }while(ptr != NULL);

    if(!found)
        return -1;

    return 0;
}




/*****************************************************************************/
/*Function: getServerConnectionStatus                                        */
/*Purpose: Determine if a given server is currently connected.               */
/*Returns: 0 if connected, -1 otherwise.                                     */
/*****************************************************************************/
int getServerConnectionStatus(char* ipaddress)
{
    int found = FALSE;
    serverInfo* ptr;

    if(headInfo == NULL)
        return -1;

    /* Look for the given server */
    ptr = headInfo;
    do
    {
        if(strcmp(ptr->ipaddress,ipaddress) == 0)
        {
            if(ptr->connected == TRUE)
            {
                found = TRUE;
                break;
            }
        }
        ptr = ptr->next;
    }while(ptr != NULL);

    if(!found)
        return -1;

    return 0;
}




/*****************************************************************************/
/*Function: getLoadValStr                                                    */
/*Purpose: Returns a string containing the server load.                      */
/*Returns: Load of a given server formatted as a string."Error" if not found */
/*****************************************************************************/
char* getLoadValStr(char* ipaddress)
{
    static char tmp[32];
    serverInfo* ptr;

    sprintf(tmp,"Error");

    if(headInfo == NULL)
        return tmp;

    /* Look for the given server */
    ptr = headInfo;
    do
    {
        if(strcmp(ptr->ipaddress,ipaddress) == 0)
        {
            sprintf(tmp,"%.2f",((float)ptr->currentClients / (float)ptr->maxClients));
        }
        ptr = ptr->next;
    }while(ptr != NULL);

    return tmp;
}




/*****************************************************************************/
/*Function: getUnconnectedServerIPaddr                                       */
/*Purpose: Returns a string containing an unconnected server.                */
/*Returns: ip address of unconnected server, NULL if none exist.             */
/*****************************************************************************/
char* getUnconnectedServerIPaddr()
{
    static char tmp[32];
    serverInfo* ptr;
    int found = FALSE;

    sprintf(tmp,"");

    if(headInfo == NULL)
        return tmp;

    /* Look for the given server */
    ptr = headInfo;
    do
    {
        if(ptr->connected == FALSE)
        {
            found = TRUE;
            strcpy(tmp,ptr->ipaddress);
            break;
        }
        ptr = ptr->next;
    }while(ptr != NULL);

    if(found == FALSE)
        return NULL;

    return tmp;
}




/*****************************************************************************/
/* Connection_Keepalive_Thread - Sends Keepalives for existing connections.  */
/*                               This is a P2P thread.                       */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
DWORD WINAPI Connection_Keepalive_Thread(LPVOID lpParam)
{
    int x;
    serverInfo* ptr;
    char tmp[32];
    unsigned short id;
    unsigned short port;
    static char inbuff[MAX_PSIZE];         /* Request Buffer    */
    SOCKET listeningSocket;                /* Udp Server Socket */
    struct sockaddr_in to,from;            /* Used for select   */
    fd_set etherfds;                       /* Used for select   */
    int rval, size;
    HANDLE hThread = NULL;
    DWORD ThreadId = 0;
    listeningSocket = INVALID_SOCKET;      /* Init */

    /* Bind to a port to send/recv on */
    port = 700;
    while(1)
    {
        if((listeningSocket = udpOpenBindSocket(port)) == INVALID_SOCKET)
        {
            reportError("Local p2p server: Call to udpOpenBindSocket() failed, aborting.\n",0,0);
            return -1;
        }
        else
        {
            break;
        }
        port++;
    }

    /********************************************************/
    /* Send keepalives on all connections once per minute.  */
    /********************************************************/
    while(1)
    {
        /* Wait 1 minute */
        Sleep(60000);

        if(headInfo == NULL)
            continue;

        /* Search through the server array for connected servers */
        ptr = headInfo;
        do
        {
            if(ptr->connected == TRUE)
            {
                /* Create remote info */
                to.sin_family = AF_INET;
                to.sin_port = 69;
                to.sin_addr.s_addr = inet_addr(ptr->ipaddress);

                /* Create the keepalive packet */
                memset(tmp,0,32);
                id = htons(OPC_KEEPALIVE);
                memcpy(&tmp[0],&id,2);
                strcpy(&tmp[2],"TFTP_KEEPALIVE");

                /* Try 5x max */
                for(x=0; x < 5; x++)
                {
                    /* Send the packet */
                    udp_send(listeningSocket,tmp,17,to);

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
                            reportError("Error Recving Packet on port.\n",0,0);
                            continue;
                        }

                        //Check the sender
                        if( strcmp( inet_ntoa(from.sin_addr), inet_ntoa(to.sin_addr) ) != 0 )
                        {
                            continue;
                        }
                        
                        /* Check that the packet is a keepalive ack */
                        if( *((unsigned short*)&inbuff) != htons(OPC_KEEPA_RPLY))
                        {
                            continue;
                        }

                        /* Success */
                        break;
                    }
                    else if(rval == SOCKET_ERROR)
                    {
                        reportError("Select Error Detected.",WSAGetLastError(),1);
                        break;
                    }
                }
            }
            ptr = ptr->next;
        }while(ptr != NULL);
    }

    return 0;
}




/*****************************************************************************/
/* Disconnect_All - Closes all existing P2P connections.                     */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int Disconnect_All()
{
    int x;
    serverInfo* ptr;
    char tmp[32];
    unsigned short id;
    unsigned short port;
    static char inbuff[MAX_PSIZE];         /* Request Buffer    */
    SOCKET listeningSocket;                /* Udp Server Socket */
    struct sockaddr_in to,from;            /* Used for select   */
    fd_set etherfds;                       /* Used for select   */
    int rval, size;
    HANDLE hThread = NULL;
    DWORD ThreadId = 0;
    listeningSocket = INVALID_SOCKET;      /* Init */

    /* Bind to a port to send/recv on */
    port = 900;
    while(1)
    {
        if((listeningSocket = udpOpenBindSocket(port)) == INVALID_SOCKET)
        {
            reportError("Local p2p server: Call to udpOpenBindSocket() failed, aborting.\n",0,0);
            return -1;
        }
        else
        {
            break;
        }
        port++;
    }

    /*****************************************/
    /* Send disconnects on all connections.  */
    /*****************************************/

    if(headInfo == NULL)
        return 0;

    /* Search through the server array for connected servers */
    ptr = headInfo;
    do
    {
        if(ptr->connected == TRUE)
        {
            /* Create remote info */
            to.sin_family = AF_INET;
            to.sin_port = 69;
            to.sin_addr.s_addr = inet_addr(ptr->ipaddress);

            /* Create the keepalive packet */
            memset(tmp,0,32);
            id = htons(OPC_DISCONNECT);
            memcpy(&tmp[0],&id,2);
            strcpy(&tmp[2],"TFTP_DISCONNECT");

            /* Try 5x max */
            for(x=0; x < 5; x++)
            {
                /* Send the packet */
                udp_send(listeningSocket,tmp,18,to);

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
                        reportError("Error Recving Packet on port.\n",0,0);
                        continue;
                    }

                    //Check the sender
                    if( strcmp( inet_ntoa(from.sin_addr), inet_ntoa(to.sin_addr) ) != 0 )
                    {
                        continue;
                    }
                    
                    /* Check that the packet is a disconnect ack */
                    if( *((unsigned short*)&inbuff) != htons(OPC_DISCON_RPLY))
                    {
                        continue;
                    }

                    /* Success */
                    break;
                }
                else if(rval == SOCKET_ERROR)
                {
                    reportError("Select Error Detected.",WSAGetLastError(),1);
                    break;
                }
            }

            /* Now Disconnected */
            ptr->connected = FALSE;
        }
        
        ptr = ptr->next;
    }while(ptr != NULL);

    return 0;
}




/*****************************************************************************/
/*Function: getServerIpAddresses                                             */
/*Purpose: Gets the ipaddresses of the servers in the database.              */
/*Returns: # of servers or -1 if there are none in the database.             */
/*****************************************************************************/
int getServerIpAddresses(char** ipStr)
{
    int x;
    char* localStrPtr;
    serverInfo* ptr;

    if(headInfo == NULL)
        return -1;

    *ipStr = NULL;
    *ipStr = (char*)malloc(numServers*16);
    if(*ipStr == NULL)
        return -1;
    memset(*ipStr,0,numServers*16);

    localStrPtr = *ipStr;
    ptr = headInfo;
    for(x=0; x < numServers; x++)
    {
        strcpy(localStrPtr,ptr->ipaddress);
        localStrPtr += (int)strlen(ptr->ipaddress) + 1;
        ptr = ptr->next;
    }

    return numServers;
}




/*****************************************************************************/
/*Function: cacheDirListing                                                  */
/*Purpose: caches a directory listing to disk.                               */
/*Returns: 0 on success, -1 if an error occurred while saving.               */
/*****************************************************************************/
int cacheDirListing(char* buffer, char* ipaddress)
{
    FILE* outFile;
    int bufferSize;
    char temp[50];

    bufferSize = (int)strlen(buffer);

    /* Check if the ServerCache directory exists, if not, create it */
    CreateDirectory(".\\ServerCache",NULL);

    /* Write the buffer out to a file */
    sprintf(temp,".\\ServerCache\\%s_cached.dat",ipaddress);
    outFile = fopen(temp,"wb");
    if(outFile == NULL)
        return -1;

    fwrite(buffer,1,bufferSize,outFile);
    fclose(outFile);

    return 0;
}
