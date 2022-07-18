/*****************************************************************************/
/* connectionManagement.h - Prototypes for functions associated with managing*/
/*                          "connections".                                   */
/*****************************************************************************/
#ifndef CONNECTION_MANAGEMENT_H
#define CONNECTION_MANAGEMENT_H

/************/
/* Includes */
/************/
#include <stdio.h>


/***********************************/
/* Defines for connection database */
/***********************************/
#define CONNECTION_TIMER_RELOAD 300         //5 minute timer value
#define CONNECTION_IDLE         0
#define CONNECTION_DLING        1


/*******************/
/* Data Structures */
/*******************/
typedef struct connectStruct
{
    int inUse;              //1=In Use
    char ipaddress[32];     //IP address
    int numPorts;           //# of ports being used; 1 port == 1 connection
    unsigned short* ports;  //Port
    unsigned int secondTimer;
    int status;             //1 = downloading, 0 = idle
} connectStruct;


/***********************/
/* Function Prototypes */
/***********************/
int createConnectionTable(int maximumConnections);
int setConnectionStatus(char* ipaddress, int status);
void destroyConnectionTable();
int addConnection(int numConnections, unsigned short firstPort,
                  char* ipaddress);
int updateClientEntry(int clientIndex,int desiredConnections, 
                      unsigned short firstPort);
int removeConnection(char* ipaddress);
int getNumAvailConnections();
int getMaxConnections();
int searchForConnectedClient(char* ipaddress);
int getClientInfo(char* ipaddress, connectStruct* data);
int getNumSimultConnections(char* ipaddress);
unsigned short getFirstPort(char* ipaddress);
DWORD WINAPI Monitor_Connections(LPVOID sockdata);



#endif
