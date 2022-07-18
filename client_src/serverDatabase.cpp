/*****************************************************************************/
/* serverDatabase.c - Routines for server database functionality.            */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <windows.h>


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
    }while(ptr->next != NULL);

    if(!found)
        return -1;

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
