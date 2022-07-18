/****************************************************************************/
/* cmn_util.h - Prototypes of commonly used functions for the htftp server. */
/****************************************************************************/
#ifndef CMN_UTIL_H
#define CMN_UTIL_H


/***********************/
/* Function Prototypes */
/***********************/
void hitAKeyToExit();
int strcasecmp(char* str1, char* str2);
int extract_string(char* instring, char** outstring,int size);
int resize_and_copy(char** address_space, int newsize, char* newdata);
int initLogging();
void reportError(char *msg, DWORD errorCode, int wsockErr);
void logWinsockError(int wsockError);

#endif
