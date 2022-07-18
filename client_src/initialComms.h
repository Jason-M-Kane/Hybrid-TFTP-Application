/****************************************************************************/
/* initialComms.h - Prototypes for functions to perform inital              */
/*                  client/server communications. (pre-xfer session)        */
/*                  1.) Server Discovery                                    */
/*                  2.) Directory Listing                                   */
/*                  3.) Connection Initiation                               */
/****************************************************************************/
#ifndef INITIAL_COMMS_H
#define INITIAL_COMMS_H

/***********************/
/* Function Prototypes */
/***********************/
int sendServerDiscoveryRqst(SOCKET sock, int timeoutSeconds);
int serverFailDetect(char* ipaddress);
int getServerDirectoryListing(char* serverIPAddress);
int initiateConnection(SOCKET sock, char* ipaddress, int timeoutSeconds,
                       int desiredConnects, int* allowedConnects);



#endif
