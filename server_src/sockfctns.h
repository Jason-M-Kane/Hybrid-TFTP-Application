/****************************************************************************/
/* sockfctns.h - Prototypes for code that handles Socket                    */
/*               send/recv/startup/shtdowns                                 */
/****************************************************************************/
#ifndef SOCKFCTNS_H
#define SOCKFCTNS_H
#include <winsock.h>

/* Function Prototypes */
int initializeWinsock();                          /* Initializes Winsock     */
void shutdownWinsock(SOCKET listeningSocket);     /* Shuts down Winsock      */
SOCKET udpOpenBindSocket(unsigned short port);    /* Creates a socket & binds*/
/* UDP Send function */
int udp_send(SOCKET sock,char* sendbuf,int len, struct sockaddr_in remote);
/* UDP Recv function */
int udp_recv(SOCKET sock,char* recvbuf,int len,struct sockaddr_in* remote);



#endif
