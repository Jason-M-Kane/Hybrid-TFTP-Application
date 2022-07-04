/****************************************************************************/
/* opcode_definitions.h - Contains common TFTP OPCODE/ERROR Definitions     */
/****************************************************************************/
#ifndef OPCODE_DEFINITIONS_H
#define OPCODE_DEFINITIONS_H
#include <winsock.h>

/* Global Defines */
#define MAX_DATA_SIZE 512 //Normal Data size = 512 Bytes
#define MAX_PSIZE     (MAX_DATA_SIZE+4)   //4 control bytes + data bytes
#define DIR_RQST_PSIZE (65464+4)
#define ACK_SIZE       4
#define MIN_CLIENT_PORT  (unsigned short) 2000
#define MAX_CLIENT_PORT  (unsigned short) 60000

#define DEBUG_TFTP 0

/*TFTP OP CODES (16-bit integer) */
#define OPC_READ_REQ    (short) 0x1    /* Read request (RRQ)   */
#define OPC_WRITE_REQ   (short) 0x2    /* Write request (WRQ)  */
#define OPC_DATA        (short) 0x3    /* Data (DATA)          */
#define OPC_ACK         (short) 0x4    /* Acknowledgment (ACK) */
#define OPC_ERROR       (short) 0x5    /* Error (ERROR)        */
#define OPC_OACK        (short) 0x6    /* OACK                 */

/* Hybrid TFTP OP CODES (16-bit integer) */
#define OPC_RESUME      (short) 0x90   /* Resume Download      */
#define OPC_CON_RQST    (short) 0x95   /* Connection Request   */
#define OPC_CON_RPLY    (short) 0x94   /* Connection Reply     */
#define OPC_DIRRQ       (short) 0x97   /* Directory Request    */
#define OPC_AUTOD       (short) 0x99   /* Auto Detect Rqst     */
#define OPC_AUTODR      (short) 0x98   /* Auto Detect Reply    */
#define OPC_RESTRT      (short) 0x90   /* Transfer Restart Msg */
#define OPC_P2P_SENDREQ (short) 0x91   /* P2P Send Request Msg */
#define OPC_DISCONNECT  (short) 0x80   /* Disconnect Msg       */
#define OPC_DISCON_RPLY (short) 0x81   /* Disconnect Reply     */
#define OPC_KEEPALIVE   (short) 0x82   /* Keepalive Msg        */
#define OPC_KEEPA_RPLY  (short) 0x83   /* Keepalive Reply      */

/* TFTP Error Codes (16-bit integer) */
#define ERR_NOT_DEFINED                (short) 0x0 /*See ERR message (if any)*/
#define ERR_FILE_NOT_FOUND             (short) 0x1
#define ERR_ACCESS_VIOLATION           (short) 0x2
#define ERR_DISK_FULL                  (short) 0x3
#define ERR_ILLEGAL_TFTP_OPERATION     (short) 0x4
#define ERR_UNKNOWN_PORT               (short) 0x5
#define ERR_FILE_ALREADY_EXISTS        (short) 0x6
#define ERR_NO_SUCH_USER               (short) 0x7
#define ERR_OPTION                     (short) 0x8


/**************/
/* Structures */
/**************/

/* Structure to hold TFTP Options Parameters */
typedef struct
{
    int blkSizeNeg;    /* BLK Size Negotion on?        */
    int timeoutNeg;    /* Timeout Size Negotion on?    */
    int tsizeNeg;      /* TX Size Negotion on?         */
    int blkSize;       /* Block size to use            */
    int timeout;       /* Timeout in seconds to use    */
    __int64 tsize;     /* Size of transferring file    */
    int resume;        /* Is this a resumed file xfer? */
    unsigned __int64 offset; /* File offset for resume */
}optsData;


typedef struct
{
    unsigned int serverIP;          /* Address of Server in Dotted Decimal Notation */
    unsigned short remotePort;      /* Remote Server Port*/
    unsigned short blksize;         /* Block Size Option: 8 to 65464*/
    __int64 tsize;                  /* Transfer Size Option: 1=Yes, 0=No */
    int timeout;                    /* 1-255, 0=dont use option */
    char fileToGet[MAX_PATH+1];     /* More than enough space */
    char outputDir[MAX_PATH+1];     /* More than enough space */
}clientParameters;


/* Global Structure Definitions */
struct connection_data{
  struct sockaddr_in remote;
  char* filename;
  char* mode;
  optsData options;
};

/* Struct for Peer File Transfers */
typedef struct
{
    char ipaddr[32];
    char filename[400];
    unsigned __int64 offset;
    unsigned short port;
    unsigned short blockID;
    unsigned int sessionID;
}remoteP2PStruct;


#endif
