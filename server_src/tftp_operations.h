/****************************************************************************/
/* tftp_operations.h - Prototypes for core tftp operations.                 */
/****************************************************************************/
#ifndef TFTP_OPERATIONS_H
#define TFTP_OPERATIONS_H


/***********************/
/* Function Prototypes */
/***********************/
void dir_transfer(SOCKET local,struct sockaddr_in c_remote,char* buffer,
                  optsData opt);
int octet_transfer(SOCKET local,struct sockaddr_in c_remote,char* filename,
                    optsData opt);
int parse_ack(struct sockaddr_in remote,struct sockaddr_in check, char* buf, 
              int size, SOCKET sock, int chk_block);
void Send_Error(int errortype, char* add_msg,struct sockaddr_in remote,
                SOCKET sock);
int sendSeverOptAck(SOCKET sock, struct sockaddr_in c_remote,
                    struct sockaddr_in remote, optsData opt);



#endif
