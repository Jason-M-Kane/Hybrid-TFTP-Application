/****************************************************************************/
/* cmn_util.c - Commonly used functions for the htftp server.               */
/****************************************************************************/
#pragma warning(disable : 4996)

/************/
/* Includes */
/************/
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <windowsx.h>
#include "resource.h"


/***********/
/* Globals */
/***********/
HANDLE LOGFILE_MUTEX = NULL;
static char* errorLogText = NULL;
int sizeErrorLogText = 1*1024*1024; /* 1MB */
int remainingSizeErrorLogText = 1*1024*1024;

extern HWND G_hWnd;


/***********************/
/* Function Prototypes */
/***********************/
int createLocalPath(char* fname);
int checkForDirectory(char* dirPathAndName);
void hitAKeyToExit();
int strcasecmp(char* str1, char* str2);
int extract_string(char* instring, char** outstring,int size);
int resize_and_copy(char** address_space, int oldsize, int newsize);
int initLogging();
void reportTftpError(char* tftpMsg, int size);
void reportError(char *msg, DWORD errorCode, int wsockErr);
void logWinsockError(int wsockError);




/****************************************************************************/
/*Function:  createLocalPath                                                */
/*Purpose:   Creates the local path to a file if it does not exist.         */
/*Return:    0 on success, < 0 on failure.                                  */
/****************************************************************************/
int createLocalPath(char* fname)
{
	int err,index = 0;
	char* path,*tmp;
	DWORD rval;

	//Allocate memory
	path = (char*)malloc(MAX_PATH+1);
	tmp = (char*)malloc(MAX_PATH+1);
	if( (path==NULL) || (tmp==NULL) )
		return -1;
	memset(path,0,MAX_PATH+1);
	memset(tmp,0,MAX_PATH+1);
	strncpy(path,fname,MAX_PATH);
	
	//Cut off the filename from the path
	index = (int)strlen(fname) - 1;
	while(index > 0)
	{
		if(path[index] == '\\')
		{
			path[index+1] = '\0';
			break;
		}
		index--;
	}
	if(index <= 0)
	{
		//No directories to create
		return 0;
	}

	//Create all directories that do not currently exist
	index = 0;
	if(path[0] == '\\') //Network directory path check
	{
		index++;
	}

	//Loop, creating the entire path
	while(path[index] != '\0')
	{
		strcpy(tmp,path);
		while( (path[index] != '\\') && (path[index] != '\0') )
			index++;
		tmp[index] = '\0';

		//If the directory does not exist, create it
		if(checkForDirectory(tmp) < 0)
		{
			rval = CreateDirectory(tmp,NULL);
			if(rval == 0) //failed to create
			{
				err = GetLastError();
				if(err == ERROR_ALREADY_EXISTS)
					reportError("Error, Directory already exists, cant create.",err,0);
				else if(err == ERROR_PATH_NOT_FOUND)
					reportError("Error, path to directory does not exist, cant create.",err,0);
				else
					reportError("Directory Creation Failed",err,0);
				free(tmp);
				free(path);
				return -1;
			}
		}

		if(path[index] != '\0')
			index++;
	}


	return 0;
}




/*****************************************************************************/
/* checkForDirectory - Checks to see that the input directory exists.        */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int checkForDirectory(char* dirPathAndName)
{
	DWORD rval;

	//Get the Folder Attributes
	rval = GetFileAttributes(dirPathAndName);
	if(rval == 0xFFFFFFFF)
		return -1;

	//Check if you just got a directory
	if(rval & FILE_ATTRIBUTE_DIRECTORY)
	{
		return 0;
	}

	return -1;
}




/****************************************************************************/
/*Function:  hitAKeyToExit                                                  */
/*Purpose:   Prints a msg asking user to hit enter key.                     */
/*Return:    Nothing                                                        */
/****************************************************************************/
void hitAKeyToExit()
{
    printf("Hit Enter Key To Exit");
    fflush(stdin);
    getchar();
    return;
}




/****************************************************************************/
/*Function:  strcasecmp                                                     */
/*Purpose:   Case insensitive str compare.  Linux has this built-in.        */
/*           Windows just plain SUCKS!!!!!!!!!!!                            */
/*Return:    0 if a case insensitive match.                                 */
/****************************************************************************/
int strcasecmp(char* str1, char* str2)
{
    return ( (int)_stricmp(str1,str2) );
}




/****************************************************************************/
/*Function:  extract_string                                                 */
/*Purpose:   extracts the first null terminated string from a buffer        */
/*Input: a buffer to extract from, a ptr to an output string, the buffer    */
/*       size.                                                              */
/*Return: number of copied char's if extraction of string is successful.    */
/*        -1 if unsuccessful                                                */
/****************************************************************************/
int extract_string(char* instring, char** outstring,int size)
{
    int outsize = 0;

    //Check size to make sure its a positive #
    if(size <= 0)
    {
        reportError("Extract string error, size <= 0",0,0);
        return -1;
    }

    //Determine how many characters to copy
    while(1)
    {
        if(instring[outsize] == '\0')
            break;
        outsize++;
        if(outsize >= size)
        {
            reportError("Extract string error, outsize >= size",0,0);
            return -1;
        }
    }

    //Copy the determined # of chars and return the # copied
    if( (*outstring = (char*)malloc(outsize+1)) == NULL)
    {
        reportError("Allocating memory in extract_string function",0,0);
        return -1;
    }
    memset(*outstring, 0,outsize+1); //zero the data
    strncpy(*outstring,instring,outsize);

    return (((int)strlen(*outstring))+1);
}




/****************************************************************************/
/*Function:  resize_and_copy                                                */
/*Purpose:  Reallocates memory for a string and copies new data into it     */
/*Input:  A pointer to a string to resize, a new size for it, a string to   */
/*        copy new data from.                                               */
/*Return: 0 on success, -1 on failure                                       */
/****************************************************************************/
int resize_and_copy(char** address_space, int oldsize, int newsize)
{
    char* temp = NULL;

    //Copy the existing data over to a temporary variable
    temp = (char*)malloc(oldsize);
    if(temp == NULL)
    {
        reportError("Allocating temp memory in resize function",0,0);
        return -1;
    }
    memcpy(temp,*address_space,oldsize);

    //Free the old address
    if(*address_space != NULL)
        free(*address_space);

    //Resize the passed in address space
    if( ((*address_space) = (char*)malloc(newsize)) == NULL)
    {
        reportError("Allocating memory in resize function",0,0);
        return -1;
    }
    memset(*address_space,0,newsize); //zero the data to be safe

    //Copy the old data back into the address space
    memcpy(*address_space,temp,oldsize);

    //Free the temp buffer
    free(temp);

    return 0;
}




/****************************************************************************/
/*Function: initLogging                                                     */
/*Purpose:  Logs error messages to a file/screen.  Mutual Exclusion is      */
/*          guaranteed.                                                     */
/****************************************************************************/
int initLogging()
{
    LOGFILE_MUTEX = CreateMutex(NULL,FALSE,NULL);
    if(LOGFILE_MUTEX == NULL)
	{
        printf("Error, could not create required log mutex\n");
        return -1;
    }

    errorLogText = (char*)malloc(sizeErrorLogText);
    if(errorLogText == NULL)
    {
        CloseHandle(LOGFILE_MUTEX);
        LOGFILE_MUTEX = NULL;
        return -1;
    }
    strcpy(errorLogText,"Error Log Initialized\r\n");
    remainingSizeErrorLogText -= (23);

    return 0;
}




/****************************************************************************/
/*Function: reportTftpError                                                 */
/*Purpose:  Reports a received TFTP Error Message                           */
/*Input: 	TFTP Error message and its size                                 */
/*Return:   Nothing															*/
/****************************************************************************/
void reportTftpError(char* tftpMsg, int size)
{
    char tchar[200];

    /* Ensure Error String is null terminated */
    tftpMsg[(size -1)] = '\0';

    /* Print out the error code */
    sprintf(tchar,"TFTP Error Message Received:  Code 0x%X\n\tError String:%s\n",
        ntohs(*((unsigned short*)&tftpMsg[2])) & 0xFFFF,
        &tftpMsg[4]);

    reportError(tchar,0,0);

    return;
}




/****************************************************************************/
/*Function: reportError                                                     */
/*Purpose:  Logs error messages to a file/screen.  Mutual Exclusion is      */
/*          ensured so that the log file will not be scrambled.             */
/*Input: 	An error message, error code, winsockError? 0=no, other=yes     */
/*Return: Nothing															*/
/****************************************************************************/
void reportError(char *msg, DWORD errorCode, int wsockErr)
{
    char tchar[200];
    FILE *logfile = NULL;

    if(LOGFILE_MUTEX == NULL)
    {
        printf("Error LOG MUTEX == NULL, log aborted.\n"
            "Error msg to log was: %s.\n",msg);
        return;
    }

    WaitForSingleObject(LOGFILE_MUTEX,INFINITE);
    logfile = fopen("log.txt", "a");
    if(logfile == NULL)
    {
        printf("Error Opening log file for writting.\n"
            "Error trying to log was: %s\n",msg);
        return;
    }

    //Determine whether to output Winsock Debug Information
    if(wsockErr)
    {
        sprintf(tchar,"%s, errno is %d",msg, errorCode);
        printf(tchar);
        fprintf(logfile,"%s\r\n",tchar);
        fclose(logfile);
        logWinsockError(errorCode);
    } 
    else
    {
		if(errorCode != 0)
		{
			sprintf(tchar,"%s, errno is %d",msg, errorCode);
		}
		else
		{
			sprintf(tchar,"%s",msg);
		}
	    printf(tchar);
    	fprintf(logfile,"%s\r\n",tchar);
       	fclose(logfile);
    }

    /* Update the User viewable error log */
    remainingSizeErrorLogText -= ((int)strlen(tchar) - 2);
    if(remainingSizeErrorLogText < 50)
    {
        remainingSizeErrorLogText = sizeErrorLogText;
        strcpy(errorLogText,"Error Log Re-Initialized\r\n");
        remainingSizeErrorLogText -= (23);
        remainingSizeErrorLogText -= ((int)strlen(tchar) - 2);
    }

    strcat(errorLogText,tchar);
    strcat(errorLogText,"\r\n");
    SetDlgItemText(G_hWnd,IDC_ERRORLOG,errorLogText);
    
    /* Scroll the edit box automatically */
    SendDlgItemMessage(G_hWnd,IDC_ERRORLOG,EM_LINESCROLL,0,0xFFFF);

    ReleaseMutex(LOGFILE_MUTEX);
    return;
}




/****************************************************************************/
/*Function:  logWinsockError                                                */
/*Purpose:  Logs a Winsock Error Message related to the given input Error.  */
/*Input: 	A Winsock Error ID.                                             */ 
/*Return: Nothing															*/
/****************************************************************************/
void logWinsockError(int wsockError)
{
    FILE *logfile;
    logfile = fopen("log.txt", "a");
    if(logfile == NULL)
    {
        return;
    }
    printf("Winsock Error Type: ");
    fprintf(logfile,"Winsock Error Type: ");

    /* Print out a descriptive error for each type of socket error */
    switch(wsockError)
    {
    case WSABASEERR:
        {
            printf("No Error");
            fprintf(logfile,"No Error");
        }
        break;
    case WSAEINTR:
        {
            printf("Interrupted system call");
            fprintf(logfile,"Interrupted system call");
        }
        break;
    case WSAEBADF:
        {
            printf("Bad file number");
            fprintf(logfile,"Bad file number");
        }
        break;
    case WSAEACCES:
        {
            printf("Permission Denied");
            fprintf(logfile,"Permission Denied");
        }
        break;
    case WSAEFAULT:
        {
            printf("Bad address");
            fprintf(logfile,"Bad address");
        }
        break;
    case WSAEINVAL:
        {
            printf("Invalid Argument");
            fprintf(logfile,"Invalid Argument");
        }
        break;
    case WSAEMFILE:
        {
            printf("Too many open files");
            fprintf(logfile,"Too many open files");
        }
        break;
    case WSAEWOULDBLOCK:
        {
            printf("Operation would block");
            fprintf(logfile,"Operation would block");
        }
        break;
    case WSAEINPROGRESS:
        {
            printf("Operation now in progress");
            fprintf(logfile,"Operation now in progress");
        }
        break;
    case WSAEALREADY:
        {
            printf("Operation already in progress");
            fprintf(logfile,"Operation already in progress");
        }
        break;
    case WSAENOTSOCK:
        {
            printf("Socket operation on nonsocket");
            fprintf(logfile,"Socket operation on nonsocket");
        }
        break;
    case WSAEDESTADDRREQ:
        {
            printf("Destination address required");
            fprintf(logfile,"Destination address required");
        }
        break;
    case WSAEMSGSIZE:
        {
            printf("Message too long");
            fprintf(logfile,"Message too long");
        }
        break;
    case WSAEPROTOTYPE:
        {
            printf("Protocol wrong type for socket");
            fprintf(logfile,"Protocol wrong type for socket");
        }
        break;
    case WSAENOPROTOOPT:
        {
            printf("Bad Protocol Option");
            fprintf(logfile,"Bad Protocol Option");
        }
        break;
    case WSAEPROTONOSUPPORT:
        {
            printf("Protocol not supported");
            fprintf(logfile,"Protocol not supported");
        }
        break;
    case WSAESOCKTNOSUPPORT:
        {
            printf("Socket type not supported");
            fprintf(logfile,"Socket type not supported");
        }
        break;
    case WSAEOPNOTSUPP:
        {
            printf("Operation not supported on socket");
            fprintf(logfile,"Operation not supported on socket");
        }
        break;
    case WSAEPFNOSUPPORT:
        {
            printf("Protocol family not supported");
            fprintf(logfile,"Protocol family not supported");
        }
        break;
    case WSAEAFNOSUPPORT:
        {
            printf("Address family not supported by protocol family");
            fprintf(logfile,"Address family not supported by protocol family");
        }
        break;
    case WSAEADDRINUSE:
        {
            printf("Address already in use");
            fprintf(logfile,"Address already in use");
        }
        break;
    case WSAEADDRNOTAVAIL:
        {
            printf("Cannot assign requested address");
            fprintf(logfile,"Cannot assign requested address");
        }
        break;
    case WSAENETDOWN:
        {
            printf("Network is down");
            fprintf(logfile,"Network is down");
        }
        break;
    case WSAENETUNREACH:
        {
            printf("Network is unreachable");
            fprintf(logfile,"Network is unreachable");
        }
        break;
    case WSAENETRESET:
        {
            printf("Net dropped connection or reset");
            fprintf(logfile,"Net dropped connection or reset");
        }
        break;
    case WSAECONNABORTED:
        {
            printf("Software caused connection abort");
            fprintf(logfile,"Software caused connection abort");
        }
        break;
    case WSAECONNRESET:
        {
            printf("Connection reset by peer");
            fprintf(logfile,"Connection reset by peer");
        }
        break;
    case WSAENOBUFS:
        {
            printf("No buffer space available");
            fprintf(logfile,"No buffer space available");
        }
        break;
    case WSAEISCONN:
        {
            printf("Socket is already connected");
            fprintf(logfile,"Socket is already connected");
        }
        break;
    case WSAENOTCONN:
        {
            printf("Socket is not connected");
            fprintf(logfile,"Socket is not connected");
        }
        break;
    case WSAESHUTDOWN:
        {
            printf("Cannot send after socket shutdown");
            fprintf(logfile,"Cannot send after socket shutdown");
        }
        break;
    case WSAETOOMANYREFS:
        {
            printf("Too many references, cannot splice");
            fprintf(logfile,"Too many references, cannot splice");
        }
        break;
    case WSAETIMEDOUT:
        {
            printf("Connection timed out");
            fprintf(logfile,"Connection timed out");
        }
        break;
    case WSAECONNREFUSED:
        {
            printf("Connection refused");
            fprintf(logfile,"Connection refused");
        }
        break;
    case WSAELOOP:
        {
            printf("Too many levels of symbolic links");
            fprintf(logfile,"Too many levels of symbolic links");
        }
        break;
    case WSAENAMETOOLONG:
        {
            printf("File name too long");
            fprintf(logfile,"File name too long");
        }
        break;
    case WSAEHOSTDOWN:
        {
            printf("Host is down");
            fprintf(logfile,"Host is down");
        }
        break;
    case WSAEHOSTUNREACH:
        {
            printf("No route to host");
            fprintf(logfile,"No route to host");
        }
        break;
    case WSAENOTEMPTY:
        {
            printf("Directory not empty");
            fprintf(logfile,"Directory not empty");
        }
        break;
    case WSAEPROCLIM:
        {
            printf("Too many processes");
            fprintf(logfile,"Too many processes");
        }
        break;
    case WSAEUSERS:
        {
            printf("Too many users");
            fprintf(logfile,"Too many users");
        }
        break;
    case WSAEDQUOT:
        {
            printf("Disk quota exceeded");
            fprintf(logfile,"Disk quota exceeded");
        }
        break;
    case WSAESTALE:
        {
            printf("Stale NFS file handle");
            fprintf(logfile,"Stale NFS file handle");
        }
        break;
    case WSASYSNOTREADY:
        {
            printf("Network subsystem is unavailable");
            fprintf(logfile,"Network subsystem is unavailable");
        }
        break;
    case WSAVERNOTSUPPORTED:
        {
            printf("WINSOCK DLL version out of range");
            fprintf(logfile,"WINSOCK DLL version out of range");
        }
        break;
    case WSANOTINITIALISED:
        {
            printf("Successful WSASTARTUP not yet performed");
            fprintf(logfile,"Successful WSASTARTUP not yet performed");
        }
        break;
    case WSAEREMOTE:
        {
            printf("Too many levels of remote in path");
            fprintf(logfile,"Too many levels of remote in path");
        }
        break;
    case WSAHOST_NOT_FOUND:
        {
            printf("Host not found");
            fprintf(logfile,"Host not found");
        }
        break;
    case WSATRY_AGAIN:
        {
            printf("Nonauthoritative host not found");
            fprintf(logfile,"Nonauthoritative host not found");
        }
        break;
    case WSANO_RECOVERY:
        {
            printf("Nonrecoverable errors: FORMERR, REFUSED, NOTIMP");
            fprintf(logfile,"Nonrecoverable errors: FORMERR, REFUSED, NOTIMP");
        }
        break;
    case WSANO_DATA: /* Same Error Code as WSANO_ADDRESS */
        {
            printf("Valid name, no data record of requested type OR ");
            printf("No address, look for MX record");
            fprintf(logfile,"Valid name, no data record of requested type OR ");
            fprintf(logfile,"No address, look for MX record");
        }
        break;
    default:
        {
            printf("No Error Description Available, See http://msdn.microsoft.com");
            fprintf(logfile,"No Error Description Available, See http://msdn.microsoft.com");
        }
        break;
    }

    printf("\n");
    fprintf(logfile,"\n");
    fclose(logfile);

    return;
}
