/****************************************************************************/
/* cmn_util.h - Prototypes of commonly used functions for the tftp server.  */
/****************************************************************************/
#ifndef CMN_UTIL_H
#define CMN_UTIL_H


/***********************/
/* Function Prototypes */
/***********************/
int createLocalPath(char* fname);
void hitAKeyToExit();
int strcasecmp(char* str1, char* str2);
int extract_string(char* instring, char** outstring,int size);
int resize_and_copy(char** address_space, int oldsize, int newsize);
int initLogging();
void reportTftpError(char* tftpMsg, int size);
void reportError(char *msg, DWORD errorCode, int wsockErr);
void logWinsockError(int wsockError);

#endif
