/****************************************************************************/
/* serverDatabase.h - Prototypes for server database functionality.         */
/****************************************************************************/
#ifndef SERVER_DATABASE_H
#define SERVER_DATABASE_H

/* Function Prototypes */
int initServerList();
int addToServerList(char* ipaddr,int maxClients,int currentClients);
int getServerIpAddresses(char** ipStr);
int cacheDirListing(char* buffer, char* ipaddress);
int updateServerConnectionTable(char* ipaddress, int connections);


#endif
