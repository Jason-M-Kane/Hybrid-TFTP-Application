/*****************************************************************************/
/* connectionManagement.c - Contains functions associated with managing      */
/*                          "connections".                                   */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "cmn_util.h"
#include "connectionManagement.h"


/***********/
/* Globals */
/***********/
static connectStruct* connectionDetails = NULL;
static G_numConnections = 0;
static G_availConnections = 0;




/*****************************************************************************/
/* Function: createConnectionTable                                           */
/* Purpose: Creates a table for all possible connections.                    */
/* Input:   The maximum allowed number of simultaneous connections on the    */
/*          system.                                                          */
/* Returns: Returns 0 on success, -1 on failure.                             */
/*****************************************************************************/
int createConnectionTable(int maximumConnections)
{
    /* Allocate the connection table */
    connectionDetails = (connectStruct*)malloc(maximumConnections*
        sizeof(connectStruct));
    if(connectionDetails == NULL)
    {
        reportError("Error creating connection table, could not malloc.",0,0);
        return -1;
    }

    /* Initialize the connection table */
    memset(connectionDetails,0,(maximumConnections*sizeof(connectStruct)));

    G_numConnections = G_availConnections = maximumConnections;

    return 0;
}




/*****************************************************************************/
/* Function: destroyConnectionTable                                          */
/* Purpose: Destroys and frees the memory associated with the connection     */
/*          table.                                                           */
/* Input:   Void.                                                            */
/* Returns: Void.                                                            */
/*****************************************************************************/
void destroyConnectionTable()
{
    int x;

    /* Free up memory allocated inside the connection table */
    for(x=0; x < G_numConnections; x++)
    {
        if(connectionDetails[x].ports != NULL)
            free(connectionDetails[x].ports);
        connectionDetails[x].ports = NULL;
    }

    /* Reset the number of available connections to 0 */
    G_numConnections = G_availConnections = 0;

    /* Destroy the connection table */
    if(connectionDetails!= NULL)
        free(connectionDetails);
    connectionDetails = NULL;

    return;
}




/*****************************************************************************/
/* Function: addConnection                                                   */
/* Purpose: Adds a connetion to the table.                                   */
/*          table.                                                           */
/* Input:   The desired number of connections, the first port to be taken,   */
/*          the ipaddress of the client.                                     */
/* Returns: # of connections granted on success, < 0 on error.               */
/*****************************************************************************/
int addConnection(int numConnections, unsigned short firstPort,
                  char* ipaddress)
{
    int allowedConnections,x,y;
    int found = 0;

    /* Check to see how many (if any) connections can be added */
    if(G_availConnections <= 0)
    {
        return -1;
    }
    else if(G_availConnections < numConnections)
    {
        allowedConnections = G_availConnections;
    }
    else
    {
        allowedConnections = numConnections;
    }

    /* We can add a connection to the server */
    for(x=0; x < G_numConnections; x++)
    {
        if(connectionDetails[x].inUse == FALSE)
        {
            connectionDetails[x].inUse = TRUE;
            connectionDetails[x].secondTimer = CONNECTION_TIMER_RELOAD;
            connectionDetails[x].status = CONNECTION_IDLE;
            strcpy(connectionDetails[x].ipaddress,ipaddress);
            connectionDetails[x].numPorts = allowedConnections;
            connectionDetails[x].ports = (unsigned short*)malloc
                (connectionDetails[x].numPorts*sizeof(unsigned short));
            if(connectionDetails[x].ports == NULL)
            {
                connectionDetails[x].inUse = FALSE;
                return -3;
            }
            for(y=0;y<connectionDetails[x].numPorts;y++)
                connectionDetails[x].ports[y] = firstPort+y;

            G_availConnections-=allowedConnections;
            found = 1;
			break;
        }
    }

    /* Check to make sure we found an empty slot, this shouldnt be an issue */
    if(found != 1)
        return -4;

    /* Success */
    return allowedConnections;
}




/*****************************************************************************/
/* Function: updateClientEntry                                               */
/* Purpose:  Updates a clients entry in the table.                           */
/* Input:   Index to the existing client, # desired connections, first port  */
/*          to be taken.                                                     */
/* Returns: # of connections granted on success, < 0 on error.               */
/*****************************************************************************/
int updateClientEntry(int clientIndex,int desiredConnections, 
                      unsigned short firstPort)
{
    int currentNumPorts,y;
	int allowedConnections = -1;

    currentNumPorts = connectionDetails[clientIndex].numPorts;
	connectionDetails[clientIndex].secondTimer = CONNECTION_TIMER_RELOAD;

    if((currentNumPorts == desiredConnections) && 
        (connectionDetails[clientIndex].ports[0] == firstPort))
    {
		// No change
		allowedConnections = desiredConnections;
    }
	else if( (currentNumPorts == desiredConnections) && 
        (connectionDetails[clientIndex].ports[0] != firstPort) )
	{
		//Port numbering change only
		for(y=0;y < connectionDetails[clientIndex].numPorts;y++)
			connectionDetails[clientIndex].ports[y] = firstPort+y;
		allowedConnections = desiredConnections;
	}
    else
    {
		//Remove the old allocated connections
		if(connectionDetails[clientIndex].ports != NULL)
		{
		    free(connectionDetails[clientIndex].ports);
			connectionDetails[clientIndex].ports = NULL;
		}

		//Desired # of connections is not equal to the current # of connections
		//Determine the new # of allowed connections and update data structures
		G_availConnections = G_availConnections+currentNumPorts;
		if(G_availConnections < desiredConnections)
	    {
	        allowedConnections = G_availConnections;
	    }
	    else
	    {
	        allowedConnections = desiredConnections;
	    }
		G_availConnections =- allowedConnections;

		connectionDetails[clientIndex].numPorts = allowedConnections;
        connectionDetails[clientIndex].ports = (unsigned short*)malloc
			(connectionDetails[clientIndex].numPorts*sizeof(unsigned short));
        if(connectionDetails[clientIndex].ports == NULL)
        {
            connectionDetails[clientIndex].inUse = FALSE;
            return -3;
        }
        for(y=0;y<connectionDetails[clientIndex].numPorts;y++)
            connectionDetails[clientIndex].ports[y] = firstPort+y;

		//Update the global # of available connections
        G_availConnections-=allowedConnections;
    }

	return allowedConnections;
}




/*****************************************************************************/
/* Function: removeConnection                                                */
/* Purpose: Removes a connetion from the table.                              */
/* Input:   The ip address of the client to be removed.                      */
/* Returns: 0 on success, -1 on error (cant find entry).                     */
/*****************************************************************************/
int removeConnection(char* ipaddress)
{
    int found = -1;
    int x;

    for(x=0; x < G_numConnections; x++)
    {
        if(connectionDetails[x].inUse == TRUE)
        {
            if(strcmp(ipaddress,connectionDetails[x].ipaddress) == 0)
            {
                if(connectionDetails[x].ports != NULL)
                    free(connectionDetails[x].ports);
                connectionDetails[x].ipaddress[0] = '\0';
                connectionDetails[x].ports = NULL;
                connectionDetails[x].numPorts = 0;
                connectionDetails[x].inUse = 0;
                connectionDetails[x].secondTimer = 0;
                connectionDetails[x].status = CONNECTION_IDLE;
                G_availConnections+=connectionDetails[x].numPorts;
                found = 0;
                break;
            }
        }
    }    

    return found;
}




/*****************************************************************************/
/* Function: getNumAvailConnections                                          */
/* Purpose: Gets the number of available connections on the server.          */
/* Input:   Void.                                                            */
/* Returns: The number of available connections.                             */
/*****************************************************************************/
int getNumAvailConnections()
{
    return G_availConnections;
}



/*****************************************************************************/
/* Function: getMaxConnections                                               */
/* Purpose: Gets the max number of connections on the server.                */
/* Input:   Void.                                                            */
/* Returns: The max number of simultaneous connections supported.            */
/*****************************************************************************/
int getMaxConnections()
{
    return G_numConnections;
}




/*****************************************************************************/
/* Function: searchForConnectedClient                                        */
/* Purpose: Searches the connection table for a particular client.           */
/* Input:   The ip address of the client in string form.                     */
/* Returns: The index into the connection table of the connected client if   */
/*          found.  If not found, returns -1.                                */
/*****************************************************************************/
int searchForConnectedClient(char* ipaddress)
{
    int found = -1;
    int x;

    for(x=0; x < G_numConnections; x++)
    {
        if(connectionDetails[x].inUse == TRUE)
        {
            if(strcmp(ipaddress,connectionDetails[x].ipaddress) == 0)
            {
                found = x;
                break;
            }
        }
    }    

    return found;
}




/*****************************************************************************/
/* Function: getClientInfo                                                   */
/* Purpose: Searches the connection table for a particular client, then      */
/*          fills in a connectStruct.                                        */
/* Input:   The ip address of the client in string form, a connectStruct     */
/*          that has been passed by reference into the function.             */
/* Returns: The index into the connection table of the connected client if   */
/*          found.  If not found, returns -1.                                */
/*****************************************************************************/
int getClientInfo(char* ipaddress, connectStruct* data)
{
    int found = -1;
    int x;

    for(x=0; x < G_numConnections; x++)
    {
        if(connectionDetails[x].inUse == TRUE)
        {
            if(strcmp(ipaddress,connectionDetails[x].ipaddress) == 0)
            {
                memcpy(data,&connectionDetails[x],sizeof(connectStruct));
                found = x;
                break;
            }
        }
    }    

    return found;
}




/*****************************************************************************/
/* Function: getNumSimultConnections                                         */
/* Purpose: Returns the number of simultaneous connections allowed for a     */
/*          particular connected client.                                     */
/* Input:   The ip address of the client in string form.                     */
/* Returns: The number of simultaneous connections allowed for the client.   */
/*          If the client is not found, returns -1.                          */
/*****************************************************************************/
int getNumSimultConnections(char* ipaddress)
{
    connectStruct client;
    if( getClientInfo(ipaddress,&client) < 0)
        return -1;
    return client.numPorts;
}




/*****************************************************************************/
/* Function: getFirstPort                                                    */
/* Purpose: Returns the first port associated with a client.                 */
/* Input:   The ip address of the client in string form.                     */
/* Returns: The first port used by the client.                               */
/*          If the client is not found, returns -1.                          */
/*****************************************************************************/
unsigned short getFirstPort(char* ipaddress)
{
    connectStruct client;
    if( getClientInfo(ipaddress,&client) < 0)
        return -1;
    return client.ports[0];
}




/*****************************************************************************/
/* Function: setConnectionStatus                                             */
/* Purpose: Sets the connection status of a client connection.               */
/* Input:   The ip address of the client in string form.                     */
/* Returns: The first port used by the client.                               */
/*          If the client is not found, returns -1.                          */
/*****************************************************************************/
int setConnectionStatus(char* ipaddress, int status)
{
	int index;

	if( (index=searchForConnectedClient(ipaddress)) < 0)
		return -1;
	else
	{
		connectionDetails[index].status = status;
		connectionDetails[index].secondTimer = CONNECTION_TIMER_RELOAD;
	}
	return 0;
}




/*****************************************************************************/
/* Function: Monitor_Connections                                             */
/* Purpose: Threaded process to remove old connections.                      */
/* Input:   Void.                                                            */
/*****************************************************************************/
DWORD WINAPI Monitor_Connections(LPVOID sockdata)
{
	int x;
	char temp[100];

	while(1)
	{
		/* Wait 1 second */
		Sleep(1000);

		/* Decrement */
		for(x=0; x < G_numConnections; x++)
        {
			if( (connectionDetails[x].inUse == TRUE) && 
                (connectionDetails[x].status != CONNECTION_DLING) )
            {
				/* Decrement idle connections once each second */
                /* and remove expired connections              */
				if(connectionDetails[x].secondTimer > 0)
				{
					connectionDetails[x].secondTimer--;
				}
				if(connectionDetails[x].secondTimer == 0)
				{
					sprintf(temp,"Connection from %s expired and removed.\r\n"
                        ,connectionDetails[x].ipaddress);
					reportError(temp,0,0);
					removeConnection(connectionDetails[x].ipaddress);
				}
			}
		}
	}
}
