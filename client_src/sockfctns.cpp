/*****************************************************************************/
/* sockfctns.c - Contains code that handles Socket                           */
/*               send/recv/startup/shtdowns                                  */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <windows.h>
#include "cmn_util.h"


/***********************/
/* Function Prototypes */
/***********************/
int initializeWinsock();                          /* Initializes Winsock     */
void shutdownWinsock(SOCKET listeningSocket);     /* Shuts down Winsock      */
SOCKET udpOpenBindSocket(unsigned short port);    /* Creates a socket & binds*/
/* UDP Send function */
int udp_send(SOCKET sock,char* sendbuf,int len, struct sockaddr_in remote);
/* UDP Recv function */
int udp_recv(SOCKET sock,char* recvbuf,int len,struct sockaddr_in* remote);




/*****************************************************************************/
/*Function: initializeWinsock                                                */
/*Purpose:  Initializes Winsock on the computer                              */
/*Input:  Nothing                                                            */
/*Return: Returns 0 on success, -1 on failure                                */
/*****************************************************************************/
int initializeWinsock()
{
    WORD version = MAKEWORD(2,2); //version of winsock
    WSADATA stWSAData;			  //winsock structure

    if(WSAStartup(version, &stWSAData) != 0) //start winsock
    {
        reportError("WSAStartup error\n",WSAGetLastError(),1);
        return -1;
    }

    return 0;
}




/*****************************************************************************/
/*Function: shutdownWinsock                                                  */
/*Purpose:  Shuts down Winsock on the computer and closes the given socket   */
/*          that is being used to listen for TCP connections                 */
/*Input:  A listening socket to close                                        */
/*Return: Nothing                                                            */
/*****************************************************************************/
void shutdownWinsock(SOCKET listeningSocket)
{
    // Shutdown Winsock
    closesocket(listeningSocket);
    WSACleanup();
    return;
}




/*****************************************************************************/
/*Function:  udpOpenBindSocket                                               */
/*Purpose:   creates a socket and binds it to the given port                 */
/*Input:  a port number                                                      */
/*Return: the integer corresponding to the socket file descriptor if         */
/*        successful, -1 if unsuccessful                                     */
/*****************************************************************************/
SOCKET udpOpenBindSocket(unsigned short port)
{
    struct sockaddr_in server;
    SOCKET listeningSocket;
    int broacastOpt = TRUE;

    //Set up the socket
    listeningSocket = socket(AF_INET,SOCK_DGRAM,0);
    if (listeningSocket == INVALID_SOCKET)
    {
        reportError("Error Opening Socket for Binding",WSAGetLastError(),1);
        closesocket(listeningSocket);
        return INVALID_SOCKET;
    }

    //Fill in the address information
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);		//convert little endian to big endian

    // Bind the socket
    if( bind(listeningSocket, (struct sockaddr*)&server, sizeof(server)) != 0)
    {
        reportError("Binding Socket Error",WSAGetLastError(),1);
        closesocket(listeningSocket);
        return INVALID_SOCKET;
    }

    //Enable UDP Broadcasting on the socket 
    if(setsockopt(listeningSocket, SOL_SOCKET, SO_BROADCAST, 
        (char*)&broacastOpt, sizeof(broacastOpt)) != 0)
    {
        reportError("Error enabling UDP broadcasting",WSAGetLastError(),1);
        closesocket(listeningSocket);
        return INVALID_SOCKET;
    }

    return listeningSocket;
}




/*****************************************************************************/
/*Function:  udp_send                                                        */
/*Purpose: wrapper function to send data using UDPs sendto function          */
/*Input:  the locally bound socket, the send buffer, the size of the buffer, */
/*        the client's sockaddr_in struct                                    */
/*Return: 0 on success, -1 on error                                          */
/*****************************************************************************/
int udp_send(SOCKET sock,char* sendbuf,int len, struct sockaddr_in remote)
{
    int rval = 0;

    rval = sendto(sock, sendbuf, len, 0,
        (struct sockaddr*)&remote, sizeof(struct sockaddr));

    if(rval == SOCKET_ERROR)
    {
        reportError("Could not send UDP Data",WSAGetLastError(),1);
        return -1;
    }

    return 0;
}




/*****************************************************************************/
/*Function:  udp_recv                                                        */
/*Purpose: wrapper function to recv data using UDPs recvfrom function        */
/*Input:  the locally bound socket, the recv buffer, the size of the buffer, */
/*        the client's sockaddr_in struct (passed by reference)              */
/*Return: Number of bytes recvd or Error (-1)                                */
/*****************************************************************************/
int udp_recv(SOCKET sock,char* recvbuf,int len,struct sockaddr_in* remote)
{
    int rval;
    int fromlen = sizeof(struct sockaddr_in);


    rval = recvfrom(sock, recvbuf, len, 0,(struct sockaddr*)remote, &fromlen);
    if(rval == SOCKET_ERROR)
    {
        //reportError("Error receiving UDP Data",WSAGetLastError(),1);
        return -1;
    }
    else if(rval == 0)
    {
        reportError("Warning:  UDP connection unexpectedly closed."
            ,WSAGetLastError(),1);
        return -1;
    }
    

    return rval;
}
