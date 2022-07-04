/*****************************************************************************/
/* createMD5.c - Functions used to create an MD5 hash for a file.  The MD5   */
/* hash is used to distinguish different files with the same name/size.      */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <stdio.h>
#include <windows.h>
#include <Wincrypt.h> /* Need to include advapi32.lib and/or crypt32.lib */
#include "cmn_util.h"


/***********/
/* Defines */
/***********/
#define BUFSIZE 1024
#define MD5LEN  16




/*****************************************************************************/
/* Function: createMD5hash                                                   */
/* Purpose: Creates the MD5 hash associated with a file.                     */
/* Input:   The path and filename of the hash value to be computed, a string */
/*          passed by reference to hold the hash value.                      */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int createMD5hash(char* pathAndFilename, char* md5hash)
{
    char tchar[200];
    char temp[32];
    DWORD dwStatus = 0;
    BOOL bResult = FALSE;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = NULL;
    BYTE rgbFile[BUFSIZE];
    DWORD cbRead = 0;
    BYTE rgbHash[MD5LEN];
    DWORD cbHash = 0;
    CHAR rgbDigits[] = "0123456789abcdef";
    unsigned int i;

    /* Erase the input string */
    strcpy(md5hash,"\0");

    hFile = CreateFile(pathAndFilename,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        sprintf(tchar,"Error opening file %s to compute md5 hash.",
            pathAndFilename); 
        reportError(tchar,0,0);
        return -1;
    }

    // Get handle to the crypto provider
    if (!CryptAcquireContext(&hProv, NULL, NULL,PROV_RSA_FULL,
        CRYPT_VERIFYCONTEXT))
    {
        dwStatus = GetLastError();
        sprintf(tchar,"CryptAcquireContext failed: %d\n", dwStatus); 
        reportError(tchar,0,0);
        CloseHandle(hFile);
        return -1;
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
    {
        dwStatus = GetLastError();
        sprintf(tchar,"CryptAcquireContext failed: %d\n", dwStatus); 
        reportError(tchar,0,0);
        CloseHandle(hFile);
        CryptReleaseContext(hProv, 0);
        return -1;
    }

    while (bResult = ReadFile(hFile, rgbFile, BUFSIZE, 
		&cbRead, NULL))
    {
        if(cbRead == 0)
        {
            break;
        }

        if (!CryptHashData(hHash, rgbFile, cbRead, 0))
        {
            dwStatus = GetLastError();
            sprintf(tchar,"CryptHashData failed: %d\n", dwStatus); 
            reportError(tchar,0,0);
            CryptReleaseContext(hProv, 0);
            CryptDestroyHash(hHash);
            CloseHandle(hFile);
            return -1;
        }
    }

    if (!bResult)
    {
        dwStatus = GetLastError();
        sprintf(tchar,"ReadFile failed: %d\n", dwStatus); 
        reportError(tchar,0,0);
        CryptReleaseContext(hProv, 0);
        CryptDestroyHash(hHash);
        CloseHandle(hFile);
        return -1;
    }

    cbHash = MD5LEN;
    if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0))
    {
        for (i = 0; i < cbHash; i++)
        {
            sprintf(temp,"%c%c", rgbDigits[rgbHash[i] >> 4],
                rgbDigits[rgbHash[i] & 0xf]);
            strcat(md5hash,temp);
        }
    }
    else
    {
        dwStatus = GetLastError();
        sprintf(tchar,"CryptGetHashParam failed: %d\n", dwStatus); 
        reportError(tchar,0,0);
        return -1;
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);

    return 0; 
}   