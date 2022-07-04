/*****************************************************************************/
/* P2P_FileListing.c - Contains functions associated with creation of the    */
/*                     file system directory listing.                        */
/*****************************************************************************/

/************/
/* Includes */
/************/
#pragma warning(disable : 4996)
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "FS_datastructures.h"
#include "createMD5.h"
#include "FileListing.h"


/* Defines */
#define MD5_HASH      0
#define MAX_P2P_FS_LEVELS 256        //Limit # of directory levels to 256
                                 //To simplify implementation
#undef DEBUG_PEER_CACHE          //For debugging


/* Globals */
static listItem** G_P2PfileSystemLvl_HeadPtrs = NULL;
static listItem** G_P2PfileSystemLvl_CurrentPtrs = NULL;
static int G_numP2PFileSystemLevels = MAX_P2P_FS_LEVELS;      //Default to 256
static int G_Replace_nodeId = 1;


/* Function Prototypes */
void destroyP2PFSlinkedList();
int cacheDirListing(char* buffer, char* ipaddress);
int createP2PFileSystemListing(char** fileSystemBuffer);
int readPeerCacheIntoFilesystem(char* file,char* ipaddress);
int p2pAddFileSystemItem(int currentlevel, listItem* currentItem);




/*****************************************************************************/
/*Function: cacheDirListing                                                  */
/*Purpose: caches a directory listing to disk.                               */
/*Returns: 0 on success, -1 if an error occurred while saving.               */
/*****************************************************************************/
int cacheDirListing(char* buffer, char* ipaddress)
{
    FILE* outFile;
    int bufferSize;
    char temp[50];

    bufferSize = (int)strlen(buffer);

    /* Check if the ServerCache directory exists, if not, create it */
    CreateDirectory(".\\PeerCache",NULL);

    /* Write the buffer out to a file */
    sprintf(temp,".\\PeerCache\\%s_cached.dat",ipaddress);
    outFile = fopen(temp,"wb");
    if(outFile == NULL)
        return -1;

    fwrite(buffer,1,bufferSize,outFile);
    fclose(outFile);

    return 0;
}



/*****************************************************************************/
/* Function: createP2PFileSystemListing                                      */
/* Purpose: First creates a linked list of the file system.  An array of     */
/*          head pointers are maintained while creating this list.  These    */
/*          pointers are used to link all files/directories located at a     */
/*          certain file system depth.  Finally, a buffer is filled with all */
/*          necessary file system information.  This buffer will be in the   */
/*          ascii format described for directory/filesystem retrieval.       */
/*          After this, all temporary data structures are removed and        */
/*          success will be returned to the calling function.  The calling   */
/*          function should be able to access the populated buffer.  It is   */
/*          the job of the calling function to deallocate the buffer.        */
/* Input:   The root path of the files to be shared, a char* pointer passed  */
/*          in by reference to be populated with the file system listing.    */
/* Returns: Returns 0 on success, -1 on failure.                             */
/*****************************************************************************/
int createP2PFileSystemListing(char** fileSystemBuffer)
{
    DWORD fileattrib;                   //File attributes
    HANDLE hSearch;					    //Search Handle
	char search_path[MAX_PATH];	        //The Search Path
    char fname[MAX_PATH];
    char ipaddr[MAX_PATH];
	WIN32_FIND_DATA FindFileData;	    //Find file data structure
    int ID = 0;
    char* extPtr = NULL;

    G_Replace_nodeId = 1;

    /* Double-check that rootpath is a real directory */
    fileattrib = GetFileAttributes(".\\PeerCache");
    if(!(fileattrib & FILE_ATTRIBUTE_DIRECTORY))
    {
        printf("Error, Peer Cache Path is not a directory.\n");
        return -1;
    }


    /*******************************************************************/
    /* Create & Init the file system "level" head pointers to NULL.    */
    /* These pointers are used for searching through file system items */
    /* during the buffer output phase                                  */
    /*******************************************************************/
    G_P2PfileSystemLvl_HeadPtrs = 
        (listItem**)malloc(G_numP2PFileSystemLevels*sizeof(listItem*));
    G_P2PfileSystemLvl_CurrentPtrs = 
        (listItem**)malloc(G_numP2PFileSystemLevels*sizeof(listItem*));
    if(G_P2PfileSystemLvl_HeadPtrs == NULL)
    {
        printf("Error allocating memory for FS Head pointers\n");
        return -1;
    }
    if(G_P2PfileSystemLvl_CurrentPtrs == NULL)
    {
        printf("Error allocating memory for FS Current pointers\n");
        free(G_P2PfileSystemLvl_HeadPtrs);
        return -1;
    }
    memset(G_P2PfileSystemLvl_HeadPtrs,0,G_numP2PFileSystemLevels*sizeof(listItem*));
    memset(G_P2PfileSystemLvl_CurrentPtrs,0,G_numP2PFileSystemLevels*sizeof(listItem*));

    /*** Loop opening up the cache files and adding each item to the file sytem ***/
    //Set up the search path
	strcpy(search_path, ".\\PeerCache\\");
	strcat(search_path, "*.*");

	hSearch = FindFirstFile(search_path, &FindFileData);
	while(hSearch != INVALID_HANDLE_VALUE)
	{
        /* Get the attributes associated with the current item */
        sprintf(fname,"%s%s",".\\PeerCache\\",FindFileData.cFileName);
        fileattrib = GetFileAttributes(fname);

        /* Ignore hidden files, symbolic links, and weird system files */
        if( (fileattrib & FILE_ATTRIBUTE_HIDDEN) ||
            (fileattrib & FILE_ATTRIBUTE_DEVICE) ||
            (fileattrib & FILE_ATTRIBUTE_REPARSE_POINT) ||
            (fileattrib & FILE_ATTRIBUTE_TEMPORARY) ||
            (strcmp(FindFileData.cFileName,".") == 0) ||
            (strcmp(FindFileData.cFileName,"..") == 0)
            )
        {
            ; /* Dont do anything */
        }
        /* Directory */
        else if(fileattrib & FILE_ATTRIBUTE_DIRECTORY)
        {
#ifdef DEBUG_PEER_CACHE
            printf("Directory: %s\n",fname);
#endif
            ; /* Dont do anything */
        }

        /* File */
        else
        {
#ifdef DEBUG_PEER_CACHE
            printf("File: %s\n",fname);
#endif
            /* If the file ends in _cached.dat, add the contents to the filesystem */
            extPtr = strrchr(fname,'_');
            if(strcmp(extPtr,"_cached.dat") == 0)
            {
                strcpy(ipaddr,fname);
                extPtr = strrchr(ipaddr,'_');
                *extPtr = '\0';
                while(*extPtr != '\\')
                    extPtr--;
                extPtr++;
                readPeerCacheIntoFilesystem(fname,ipaddr);                
            }
        }

        //Search For the Next File
		if(!FindNextFile(hSearch,&FindFileData))
		{
			FindClose(hSearch);
			hSearch = INVALID_HANDLE_VALUE;
		}
    }

    /* Output the file system to a buffer in the specified format */
    if(createFSListing(fileSystemBuffer,G_P2PfileSystemLvl_HeadPtrs) < 0)
    {
        printf("Error while creating text listing of dir contents.\n");
    }

    return 0;
}



/*****************************************************************************/
/*Function: readPeerCacheIntoFilesystem                                      */
/*Purpose:  Reads a server directory listing from a cached file.  The listing*/
/*          is parsed and a linked filesystem list is created.               */
/*Input:  Path to a cached filesystem listing file, pointer to the root node */
/*        of the virtual filesystem that is to be created.                   */
/*Return: Returns 0 on success, -1 on failure                                */
/*****************************************************************************/
int readPeerCacheIntoFilesystem(char* file,char* ipaddress)
{
    listItem* newFSItem;
    FILE* inFile = NULL;
    int x = 1;
    int rtnval = 0;
    int isFile,isDir,nodeId,hashType,index,newIndex;
    int parentNode;
    __int64 i64filesize;
    char* tbuf,*ptr;

    static char levelCmp[100];
    static char itemName[400];
    static char fullPath[400];
    static char hashval[100];


    /* Allocate temp storage for reading each line */
    tbuf = (char*)malloc(1000);
    if(tbuf == NULL)
        return -1;

    /* Open the file */
    inFile = fopen(file,"r");
    if(inFile == NULL)
    {
        return -1;
    }

    while(1)
    {
        isFile = isDir = FALSE;
        nodeId = hashType = -1;
        i64filesize = 0;
        strcpy(hashval,"NONE");


        memset(tbuf,0,1000);
        fgets(tbuf,999,inFile);
        if(feof(inFile))
            break;

        /* Check for new level */
        sprintf(levelCmp,"<level%d>\n",x);
        if(strcmp(tbuf,levelCmp) == 0)
        {
            x++;
        }

        /**************************************************/
        /* Get the parent node if its a file or directory */
        /**************************************************/
        if(strncmp(&tbuf[1],"parentNode=",11) == 0)
        {
            ptr = &tbuf[12];
            while(*ptr != '>')
                ptr++;
            *ptr = '\0';
            parentNode = atoi(&tbuf[12]);
        }
        else
        {
            /* If its not a file or directory, we dont care */
            continue;
        }
        

        /**************************************/
        /* Check if its a file or a directory */
        /**************************************/
        index = newIndex = (int)strlen(tbuf)+2;
        if(strncmp(&tbuf[index],"fname",5) == 0)
        {
            isFile = TRUE;
        }
        else if(strncmp(&tbuf[index],"dname",5) == 0)
        {
            isDir = TRUE;
        }
        else
        {
            printf("Warning cache sync lost.\n");
        }
        index+=7;
        newIndex+=7;

        /**********************/
        /* If its a directory */
        /**********************/
        if(isDir)
        {
            /* Get directory name */
            ptr = &tbuf[index];
            while(*ptr != '\'')
            {
                ptr++;
                newIndex++;
            }
            *ptr = '\0';
            strcpy(itemName,&tbuf[index]);

            /* Get directory path */
            while(*ptr != '\'')
            {
                ptr++;
                newIndex++;
            }
            index = newIndex+1;
            ptr++;
            newIndex++;
            while(*ptr != '\'')
            {
                ptr++;
                newIndex++;
            }
            *ptr = '\0';
            strcpy(fullPath,&tbuf[index]);
            
            /* Get the directory node ID */
            while(*ptr != '=')
            {
                ptr++;
                newIndex++;
            }
            index = newIndex+1;
            while(*ptr != '>')
                ptr++;
            *ptr = '\0';
            nodeId = atoi(&tbuf[index]);
        }
        
        
        /*****************/
        /* If its a file */
        /*****************/
        if(isFile)
        {
            /* Get file name */
            ptr = &tbuf[index];
            while(*ptr != '\'')
            {
                ptr++;
                newIndex++;
            }
            *ptr = '\0';
            strcpy(itemName,&tbuf[index]);

            /* Get file path */
            while(*ptr != '\'')
            {
                ptr++;
                newIndex++;
            }
            index = newIndex+1;
            ptr++;
            newIndex++;
            while(*ptr != '\'')
            {
                ptr++;
                newIndex++;
            }
            *ptr = '\0';
            strcpy(fullPath,&tbuf[index]);
            
            /* Get the filesize */
            while(*ptr != '=')
            {
                ptr++;
                newIndex++;
            }
            index = newIndex+1;
            while(*ptr != '>')
            {
                ptr++;
                newIndex++;
            }
            *ptr = '\0';
            i64filesize = _atoi64(&tbuf[index]);
            
            /* Determine the hashtype */
            while(*ptr != '=')
            {
                ptr++;
                newIndex++;
            }
            index = newIndex+1;
            while(*ptr != '>')
            {
                ptr++;
                newIndex++;
            }
            *ptr = '\0';

            /* Only 1 hashtype supported for now */
            if(strcmp(&tbuf[index],"MD5") == 0)
                hashType = MD5_HASH;
            else
                hashType = -1;

            /* Grab the hash value */
            while(*ptr != '=')
            {
                ptr++;
                newIndex++;
            }
            index = newIndex+1;
            while(*ptr != '>')
                ptr++;
            *ptr = '\0';
            strcpy(hashval,&tbuf[index]);
        }

        /***************************************************/
        /* Fill in all information related to the new node */
        /***************************************************/
        newFSItem = NULL;
        newFSItem = (listItem*)malloc(sizeof(listItem));
        if(newFSItem == NULL)
        {
            //Error mallocing
            return -1;
        }
        newFSItem->folder = isDir;                     //Is the current item a folder?  True or false
        newFSItem->ID = G_Replace_nodeId;              //ID - A unique number to identify a folder
        newFSItem->parentID = parentNode;              //ID of the parent directory of the item
        newFSItem->level = x-1;                        //# of levels down from the root directory
        newFSItem->fileSize = i64filesize;             //If the item is a file, the filesize in bytes
        newFSItem->hashType = hashType;                //Type of file hash
        strcpy(newFSItem->hashValue,hashval);          //Value of the hash function (Hex String)
        newFSItem->name = (char*)malloc(strlen(itemName)+1);
        strcpy(newFSItem->name,itemName);              //Name of the file or directory
        newFSItem->fullpathAndName = (char*)malloc(strlen(fullPath)+1);
        strcpy(newFSItem->fullpathAndName,fullPath);   //Full path and name of the file or directory
        strcpy(newFSItem->peerPathAndName[0],fullPath);
        newFSItem->currentlevelNextItem = NULL;        //Next Item assoc. with the current FS level
        newFSItem->numPeers = 1;
        strcpy(newFSItem->peerIP[newFSItem->numPeers - 1],ipaddress);


        /* Place the new node in the Linked List of File System Items */
        if(rtnval = p2pAddFileSystemItem(newFSItem->level,newFSItem) < 0)
        {
            printf("Error adding items to create P2P file system linked list.\n");
            free(newFSItem);
        }
        else if(rtnval == 2)
        {
            /* Duplicate Item */
            if(newFSItem->fullpathAndName != NULL)
                free(newFSItem->fullpathAndName);
            if(newFSItem->name != NULL)
                free(newFSItem->name);
            if(newFSItem != NULL)
                free(newFSItem);
        }
        else
        {
            G_Replace_nodeId++;
        }
    }

    /* Close the file and cleanup */
    fclose(inFile);

    return 0;
}




/*****************************************************************************/
/* Function: destroyFSlinkedList                                             */
/* Purpose: Destroys and frees all memory associated with the filesystem     */
/*          linked list.                                                     */
/* Input:   A pointer to the first item in the linked list.                  */
/* Returns:  Void.                                                           */
/*****************************************************************************/
void destroyP2PFSlinkedList()
{
    int x = 0;
    listItem* tempBegin, *tempTrace, *itemToFree;
    tempBegin = tempTrace = itemToFree = NULL;

    while(G_P2PfileSystemLvl_HeadPtrs[x] != NULL)
    {
        tempBegin = G_P2PfileSystemLvl_HeadPtrs[x];
        tempTrace = tempBegin->currentlevelNextItem;
        while(tempTrace != NULL)
        {
            itemToFree = tempTrace;
            tempTrace = tempTrace->currentlevelNextItem;
            if(itemToFree->name != NULL)
                free(itemToFree->name);
            if(itemToFree->fullpathAndName != NULL)
                free(itemToFree->fullpathAndName);
            itemToFree->name = itemToFree->fullpathAndName = NULL;
            free(itemToFree);
            itemToFree = NULL;
        }
        free(tempBegin);
        tempBegin = NULL;
        x++;
    }

    free(G_P2PfileSystemLvl_HeadPtrs);
    free(G_P2PfileSystemLvl_CurrentPtrs);
    G_P2PfileSystemLvl_HeadPtrs = G_P2PfileSystemLvl_CurrentPtrs = NULL;

    return;
}




/*****************************************************************************/
/* Function: p2pAddFileSystemItem                                            */
/* Purpose: Adds an item (directory or file) to the filesystem linked list.  */
/* Input:   A pointer to the next unallocated item in the linked list.       */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int p2pAddFileSystemItem(int currentlevel, listItem* currentItem)
{
    char parentFolderName[100];
    char relativePathPrecedingDir[300];
    char *tptr;//,*start;
    listItem* searchPtr,*prevDirPtr;
    int found = FALSE;
    int prevDirFound = FALSE;
    int index = 0;
//    int parentFolderEnd = 0;
//    int parentFolderExist = TRUE;
    int x = 0;

    int endParentPathOffset = -1;
    int endParentFolderOffset = -1;

    if(currentlevel == 3)
        found = FALSE;

    /**************************************/
    /* Determine the file's relative path */
    /**************************************/
    index = (int)strlen(currentItem->fullpathAndName);
    tptr = &currentItem->fullpathAndName[index];
    for(x= 0; x < currentlevel; x++)
    {
        if(*tptr == '\\')
            tptr--;
        while(*tptr != '\\')
        {
            tptr--;
            if(tptr < currentItem->fullpathAndName)
            {
                tptr = currentItem->fullpathAndName;
                break;
            }
        }
    }
    tptr++;
    if( (currentlevel == 1) && (currentItem->folder == TRUE) )
    {
        strcpy(currentItem->relativePath,tptr);
    }
    else if(currentlevel == 1)
    {
        strcpy(currentItem->relativePath,"\0");
    }
    else
    {
        strcpy(currentItem->relativePath,tptr);
    }
#ifdef DEBUG_PEER_CACHE
    printf("Item Relative Path = %s\n",currentItem->relativePath);
#endif

    /*********************************************************/
    /* Determine the containing folder and its relative path */
    /*********************************************************/
    endParentPathOffset = -1;
    endParentFolderOffset = -1;
    index = 0;

    while(currentItem->relativePath[index] != '\0')
    {
        if(currentItem->relativePath[index] == '\\')
        {
            endParentPathOffset = endParentFolderOffset;
            endParentFolderOffset = index;
        }
        index++;
    }

    /* Copy the Parent Folder's Relative directory if it exists */
    if(endParentPathOffset != -1)
    {
        strcpy(relativePathPrecedingDir,currentItem->relativePath);
        relativePathPrecedingDir[endParentPathOffset+1] = '\0';
    }
    else
    {
        strcpy(relativePathPrecedingDir,"\0");
    }

    /* Copy the Parent Folder if it exists */
    if(endParentFolderOffset != -1)
    {
        strcpy(parentFolderName,&currentItem->relativePath[endParentPathOffset+1]);
        if(endParentPathOffset >= 0)
        {
            parentFolderName[endParentFolderOffset-endParentPathOffset-1] = '\0';
        }
        else
        {
            parentFolderName[endParentFolderOffset] = '\0';
        }
#ifdef DEBUG_PEER_CACHE
        printf("Parent Folder = %s\n",parentFolderName);
#endif
    }
    else
    {
        strcpy(parentFolderName,"\0");
    }

    /* Add the parent folder name to the path */
    strcat(relativePathPrecedingDir,parentFolderName);
#ifdef DEBUG_PEER_CACHE
    printf("Parent Folder Dir = %s\n",relativePathPrecedingDir);
#endif


    /*********************************/
    /* Determine how to add the item */
    /*********************************/

    /***********************************************************/
    /* Search to see if the item exists                        */
    /* If the item exists, update the # of peers with the file */
    /***********************************************************/
    searchPtr = G_P2PfileSystemLvl_HeadPtrs[currentlevel-1];
    while(searchPtr != NULL)
    {
        if( (strcmp(searchPtr->name,currentItem->name) == 0 ) &&
            (strcmp(searchPtr->hashValue,currentItem->hashValue) == 0) &&
            (searchPtr->hashType == currentItem->hashType) &&
            (searchPtr->folder == currentItem->folder) &&
            (strcmp(searchPtr->relativePath,currentItem->relativePath) == 0) )
        {
            found = TRUE;
            break;
        }

        searchPtr = searchPtr->currentlevelNextItem;
    }


    /***************************************************/
    /* Verify/fix directory linkage with 1 level above */
    /***************************************************/
    if(currentlevel > 1)
    {
        /* search for containing directory and update parentID */
        prevDirPtr = G_P2PfileSystemLvl_HeadPtrs[currentlevel-2];
        while(prevDirPtr != NULL)
        {
            if( (strcmp(prevDirPtr->name,parentFolderName) == 0 ) &&
                (prevDirPtr->folder == TRUE) &&
                (strcmp(prevDirPtr->relativePath,relativePathPrecedingDir) == 0) )
            {
                currentItem->parentID = prevDirPtr->ID;
                prevDirFound = TRUE;
                break;
            }
            prevDirPtr = prevDirPtr->currentlevelNextItem;
        }
    }
    else
    {
        prevDirFound = TRUE;
    }


    /******************************************/
    /* Determine action to take with the item */
    /******************************************/
    if( (found) && (prevDirFound))
    {
        /* Update the peers, file paths, and IP addresses */
        searchPtr->numPeers++;
        strcpy(searchPtr->peerIP[searchPtr->numPeers-1],currentItem->peerIP[0]);
        strcpy(searchPtr->peerPathAndName[searchPtr->numPeers-1],currentItem->peerPathAndName[0]);
       // G_Replace_nodeId--;
        return 2;
    }
    else if(!prevDirFound)
    {
        printf("Error, cannot insert filesystem item, parent directory not found.\n");
       // G_Replace_nodeId--;
        return -1;
    }
    else
    {
        /* Item did not exist, add it */
        if(G_P2PfileSystemLvl_HeadPtrs[currentlevel-1] == NULL)
        {
            G_P2PfileSystemLvl_HeadPtrs[currentlevel-1] = currentItem;
            G_P2PfileSystemLvl_CurrentPtrs[currentlevel-1] = currentItem;
        }
        else
        {
            G_P2PfileSystemLvl_CurrentPtrs[currentlevel-1]->
                currentlevelNextItem = currentItem;
            G_P2PfileSystemLvl_CurrentPtrs[currentlevel-1] = currentItem;
        }
    }

	return 0;
}
