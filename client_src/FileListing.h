/*****************************************************************************/
/* FileListing.h - Prototypes for functions associated with creation of the  */
/*                 file system directory listing.                            */
/*****************************************************************************/
#ifndef FILE_LISTING_H
#define FILE_LISTING_H

/************/
/* Includes */
/************/
#include <stdio.h>
#include "FS_datastructures.h"


/***********************/
/* Function Prototypes */
/***********************/
int createFileSystemListing(char* rootpath, char** fileSystemBuffer);
int createFSListing(char** fileSystemBuffer, listItem** ptrHeadPtrs);



#endif
