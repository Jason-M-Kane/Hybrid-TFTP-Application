/*****************************************************************************/
/* P2P_FileListing.h - Prototypes for functions associated with creation of  */
/*                     the file system directory listing.                    */
/*****************************************************************************/
#ifndef P2P_FILE_LISTING_H
#define P2P_FILE_LISTING_H

/************/
/* Includes */
/************/
#include <stdio.h>
#include "FS_datastructures.h"


/***********************/
/* Function Prototypes */
/***********************/
void destroyP2PFSlinkedList();
int cacheDirListing(char* buffer, char* ipaddress);
int createP2PFileSystemListing(char** fileSystemBuffer);
int readPeerCacheIntoFilesystem(char* file,char* ipaddress);
int p2pAddFileSystemItem(int currentlevel, listItem* currentItem);



#endif
