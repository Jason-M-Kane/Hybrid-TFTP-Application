/*****************************************************************************/
/* FileListing.c - Contains functions associated with creation of the file   */
/*                 system directory listing.                                 */
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


/* Defines */
#define MD5_HASH      0
#define MAX_FS_LEVELS 256        //Limit # of directory levels to 256
                                 //To simplify implementation
#define DEFAULT_FS_BUFF_SIZE     (1024*1024) //1MB
#undef DEBUG_FILE_LISTING        //For debugging


/* Globals */
static listItem** G_fileSystemLvl_HeadPtrs = NULL;
static listItem** G_fileSystemLvl_CurrentPtrs = NULL;
static int G_numFileSystemLevels = MAX_FS_LEVELS;      //Default to 256
static int G_nextFreeID = 1;


/* Function Prototypes */
int createFileSystemListing(char* rootpath, char** fileSystemBuffer);
void destroyFSlinkedList(listItem* fsLinkedList);
int addFileSystemItem(char* directory, int currentlevel, int maxlevel,
                      int parentFolderID, listItem* currentItem);
int createFSListing(char** fileSystemBuffer);
int increaseAllocatedMemory(char** pAddrMemory, int oldSize, int newSize);




/*****************************************************************************/
/* Function: createFileSystemListing                                         */
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
int createFileSystemListing(char* rootpath, char** fileSystemBuffer)
{
    DWORD fileattrib;                   //File attributes
    listItem *fileSystemlist = NULL;
    int ID = 0;
    G_nextFreeID = 1;

    /* Double-check that rootpath is a real directory */
    fileattrib = GetFileAttributes(rootpath);
    if(!(fileattrib & FILE_ATTRIBUTE_DIRECTORY))
    {
        printf("Error, Path is not a directory.\n");
        return -1;
    }


    /*******************************************************************/
    /* Create & Init the file system "level" head pointers to NULL.    */
    /* These pointers are used for searching through file system items */
    /* during the buffer output phase                                  */
    /*******************************************************************/
    G_fileSystemLvl_HeadPtrs = 
        (listItem**)malloc(G_numFileSystemLevels*sizeof(listItem*));
    G_fileSystemLvl_CurrentPtrs = 
        (listItem**)malloc(G_numFileSystemLevels*sizeof(listItem*));
    if(G_fileSystemLvl_HeadPtrs == NULL)
    {
        printf("Error allocating memory for FS Head pointers\n");
        return -1;
    }
    if(G_fileSystemLvl_CurrentPtrs == NULL)
    {
        printf("Error allocating memory for FS Current pointers\n");
        free(G_fileSystemLvl_HeadPtrs);
        return -1;
    }
    memset(G_fileSystemLvl_HeadPtrs,0,G_numFileSystemLevels*sizeof(listItem*));
    memset(G_fileSystemLvl_CurrentPtrs,0,G_numFileSystemLevels*sizeof(listItem*));


    /* Create the Linked List of File System Items */
    if(addFileSystemItem(rootpath, 1, G_numFileSystemLevels, 0,
        fileSystemlist) < 0)
    {
        printf("Error adding items to create file system linked list.\n");
    }
    else
    {
        /* Output the file system to a buffer in the specified format */
        if(createFSListing(fileSystemBuffer) < 0)
        {
            printf("Error while creating text listing of dir contents.\n");
        }
    }

    /* Destroy the linked list */
    destroyFSlinkedList(fileSystemlist);

    /* Destroy the head/current "level" pointers */
    free(G_fileSystemLvl_HeadPtrs);
    free(G_fileSystemLvl_CurrentPtrs);
    G_fileSystemLvl_HeadPtrs = G_fileSystemLvl_CurrentPtrs = NULL;

    return 0;
}




/*****************************************************************************/
/* Function: destroyFSlinkedList                                             */
/* Purpose: Destroys and frees all memory associated with the filesystem     */
/*          linked list.                                                     */
/* Input:   A pointer to the first item in the linked list.                  */
/* Returns:  Void.                                                           */
/*****************************************************************************/
void destroyFSlinkedList(listItem* fsLinkedList)
{
    listItem* itemToDelete = fsLinkedList;
    listItem* temp;

    /* Nothing was ever allocated */
    if(itemToDelete == NULL)
        return;

    do
    {
        if(itemToDelete->name != NULL)
        {
            free(itemToDelete->name);
            itemToDelete->name = NULL;
        }
        if(itemToDelete->fullpathAndName != NULL)
        {
            free(itemToDelete->fullpathAndName);
            itemToDelete->fullpathAndName = NULL;
        }
        temp = itemToDelete;
        itemToDelete = itemToDelete->nextItem;
        free(temp);
    } while(itemToDelete != NULL);

    return;
}




/*****************************************************************************/
/* Function: addFileSystemItem                                               */
/* Purpose: Adds an item (directory or file) to the filesystem linked list.  */
/* Input:   A pointer to the next unallocated item in the linked list.       */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int addFileSystemItem(char* directory, int currentlevel, int maxlevel,
                      int parentFolderID, listItem* currentItem)
{
    DWORD fileattrib;                   //File attributes
    HANDLE hFile;
    HANDLE hSearch;					    //Search Handle
	char search_path[MAX_PATH];	        //The Search Path
    char next_path[MAX_PATH];	        //The Next Path
    char fname[MAX_PATH];
    LARGE_INTEGER fileSize;
	WIN32_FIND_DATA FindFileData;	    //Find file data structure
	int numFiles = 0;

	//Set up the search path for finding hbr files
	strcpy(search_path, directory);
	strcat(search_path, "*.*");

	hSearch = FindFirstFile(search_path, &FindFileData);
	while(hSearch != INVALID_HANDLE_VALUE)
	{
        /* Get the attributes associated with the current item */
        sprintf(fname,"%s%s",directory,FindFileData.cFileName);
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

        /*******************************************************/
        /* Determine if the item is a file or a directory.     */
        /* Fill in different information based on the finding. */
        /*******************************************************/

        /* Directory */
        else if(fileattrib & FILE_ATTRIBUTE_DIRECTORY)
        {
#ifdef DEBUG_FILE_LISTING
            printf("Directory: %s\n",fname);
#endif
            currentItem = (listItem*)malloc(sizeof(listItem));
            if(currentItem == NULL)
            {
                printf("Error, Cant allocate memory to add filesystem item\n");
                return -1;
            }

            /* Fill out the directory information */
            currentItem->folder = TRUE;
            currentItem->ID = G_nextFreeID;
            G_nextFreeID++;
            currentItem->parentID = parentFolderID;
            currentItem->level = currentlevel;
            currentItem->fullpathAndName = NULL;
            currentItem->name = NULL;
            currentItem->fullpathAndName = (char*)malloc(strlen(fname)+1);
            currentItem->name =(char*)malloc(strlen(FindFileData.cFileName)+1);
            if( (currentItem->fullpathAndName == NULL) ||
                (currentItem->name == NULL) )
            {
                printf("Error allocating filename for a directory.\n");
                return -1;
            }
            strcpy(currentItem->fullpathAndName,fname);
            strcpy(currentItem->name,FindFileData.cFileName);
            currentItem->nextItem = NULL;


            /* Update the "level" pointer linkage */
            currentItem->currentlevelNextItem = NULL;
            if(G_fileSystemLvl_HeadPtrs[currentlevel-1] == NULL)
            {
                G_fileSystemLvl_HeadPtrs[currentlevel-1] = currentItem;
                G_fileSystemLvl_CurrentPtrs[currentlevel-1] = currentItem;
            }
            else
            {
                G_fileSystemLvl_CurrentPtrs[currentlevel-1]->
                    currentlevelNextItem = currentItem;
                G_fileSystemLvl_CurrentPtrs[currentlevel-1] = currentItem;
            }


            /* Call function recursively to get the directory's contents */
            memset(next_path,0,MAX_PATH);
            strcpy(next_path,currentItem->fullpathAndName);
            if(next_path[strlen(next_path)-1] != '\\')
                next_path[strlen(next_path)] = '\\';
            if((currentlevel+1) <= 256)
            {
                if(addFileSystemItem(next_path,
                    currentlevel+1, MAX_FS_LEVELS, currentItem->ID, 
                    currentItem->nextItem) < 0)
                {
                    return -1;
                }
            }
        }

        /* File */
        else
        {
#ifdef DEBUG_FILE_LISTING
            printf("File: %s\n",fname);
#endif
            currentItem = (listItem*)malloc(sizeof(listItem));
            if(currentItem == NULL)
            {
                printf("Error, Cant allocate memory to add filesystem item\n");
                return -1;
            }

            /* Fill out the file information */
            currentItem->folder = FALSE;
            currentItem->ID = -1;
            currentItem->parentID = parentFolderID;
            currentItem->level = currentlevel;
            currentItem->fullpathAndName = NULL;
            currentItem->name = NULL;
            currentItem->fullpathAndName = (char*)malloc(strlen(fname)+1);
            currentItem->name =(char*)malloc(strlen(FindFileData.cFileName)+1);
            if( (currentItem->fullpathAndName == NULL) ||
                (currentItem->name == NULL) )
            {
                printf("Error allocating filename for a file.\n");
                return -1;
            }
            strcpy(currentItem->fullpathAndName,fname);
            strcpy(currentItem->name,FindFileData.cFileName);
            currentItem->nextItem = NULL;
            currentItem->hashType = MD5_HASH;
            if(createMD5hash(currentItem->fullpathAndName, currentItem->hashValue) < 0)
            {
                printf("Error creating MD5 hash.\n");
                return -1;
            }

            /* Get the filesize */
            hFile = CreateFile(
                fname,                    /* lpFileName      */
                GENERIC_READ,             /* dwDesiredAccess */
                FILE_SHARE_READ,          /* dwShareMode */
                NULL,                     /* lpSecurityAttributes */
                OPEN_EXISTING,            /* dwCreationDisposition */
                0 ,                       /* dwFlagsAndAttributes */
                NULL                      /* hTemplateFile */
            );
            if(hFile != INVALID_HANDLE_VALUE)
            {
                GetFileSizeEx(hFile,&fileSize);
                currentItem->fileSize = (unsigned __int64)fileSize.QuadPart;
            }
            else
            {
                printf("Error retrieving file size.\n");
                return -1;
            }
            CloseHandle(hFile);


            /* Update the "level" pointer linkage */
            currentItem->currentlevelNextItem = NULL;
            if(G_fileSystemLvl_HeadPtrs[currentlevel-1] == NULL)
            {
                G_fileSystemLvl_HeadPtrs[currentlevel-1] = currentItem;
                G_fileSystemLvl_CurrentPtrs[currentlevel-1] = currentItem;
            }
            else
            {
                G_fileSystemLvl_CurrentPtrs[currentlevel-1]->
                    currentlevelNextItem = currentItem;
                G_fileSystemLvl_CurrentPtrs[currentlevel-1] = currentItem;
            }
        }   

        //Search For the Next File
		if(!FindNextFile(hSearch,&FindFileData))
		{
			FindClose(hSearch);
			hSearch = INVALID_HANDLE_VALUE;
		}
	}

	return 0;
}




/*****************************************************************************/
/* Function: createFSListing                                                 */
/* Purpose: Creates the file listing expected by a client, using the         */
/*          information gathered earlier in the linked lists.                */
/* Input:   The address of a char* pointer to create the string buffer.      */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
int createFSListing(char** fileSystemBuffer)
{
    listItem* currentItem;
    unsigned int bufferSize = DEFAULT_FS_BUFF_SIZE;
    unsigned int usedBufferSize = 0;
    unsigned int level;
    int x = 0;
    int firstTime;
    char* temp, *buffer;

    /* Temporary scratch space for each individual entry.  Sized such that */
    /* the buffer will not be overrun.                                     */
    temp = (char*)malloc(MAX_PATH*4);
    if(temp == NULL)
    {
        printf("Error mallocing temporary storage in createFSListing\n");
        return -1;
    }

    /* Allocate Memory for the FS buffer */
    buffer = (char*)malloc(bufferSize);
    if(buffer == NULL)
    {
        printf("Error mallocing FS buffer in createFSListing\n");
        free(temp);
        return -1;
    }

    /* Add directory structure begin marker */
    strcpy(buffer,"<directory_structure>\r\n");
    usedBufferSize += (unsigned int)strlen(buffer);

    /*********************************************************************/
    /* Loop, adding to the buffer (making sure that enough space exists) */
    /* Resize the buffer when/if necessary.                              */
    /*********************************************************************/
    while(G_fileSystemLvl_HeadPtrs[x] != NULL)
    {
        level = x+1;
        currentItem = G_fileSystemLvl_HeadPtrs[x];

        /* Add level begin marker */
        sprintf(temp,"<level%d>\r\n",level);
#ifdef DEBUG_FILE_LISTING
        printf("%s",temp);
#endif
        usedBufferSize += (unsigned int)strlen(temp);
        if((usedBufferSize+1) > bufferSize)
        {
            if(increaseAllocatedMemory(&buffer, bufferSize, bufferSize*2) < 0)
            {
                printf("Error increasing memory for listing\n");
                free(buffer);
                free(temp);
                return -1;
            }
            bufferSize = bufferSize*2;
        }
        strcat(buffer,temp);
        
        /* Loop for a particular level */
        firstTime = TRUE;
        do
        {
            if(firstTime)
            {
                firstTime = FALSE;
            }
            else
            {
                currentItem = currentItem->currentlevelNextItem;
            }

            /* Create a file or directory addition */
            if(currentItem->folder == TRUE)
            {
                sprintf(temp,"<parentNode=%d><dname='%s'><dpath='%s'>"
                    "<nodeid=%d>\r\n",
                    currentItem->parentID, currentItem->name, 
                    currentItem->fullpathAndName, currentItem->ID);
            }
            else
            {
                sprintf(temp,"<parentNode=%d><fname='%s'><fpath='%s'>"
                    "<fsize=%I64u><hashtype=%s><hashval=0x%s>\r\n",
                    currentItem->parentID, currentItem->name, 
                    currentItem->fullpathAndName,currentItem->fileSize,
                    "MD5",currentItem->hashValue);
            }
#ifdef DEBUG_FILE_LISTING
            printf("%s",temp);
#endif

            /* Add the file or directory to the buffer, incr size if needed */
            usedBufferSize += (unsigned int)strlen(temp);
            if((usedBufferSize+1) > bufferSize)
            {
                if(increaseAllocatedMemory(&buffer,bufferSize,bufferSize*2)<0)
                {
                    printf("Error increasing memory for listing\n");
                    free(buffer);
                    free(temp);
                    return -1;
                }
                bufferSize = bufferSize*2;
            }
            strcat(buffer,temp);

        }while(currentItem->currentlevelNextItem != NULL);

        /* Add level end marker */
        sprintf(temp,"</level%d>\r\n",level);
#ifdef DEBUG_FILE_LISTING
        printf("%s",temp);
#endif
        usedBufferSize += (unsigned int)strlen(temp);
        if((usedBufferSize+1) > bufferSize)
        {
            if(increaseAllocatedMemory(&buffer, bufferSize, bufferSize*2) < 0)
            {
                printf("Error increasing memory for listing\n");
                free(buffer);
                free(temp);
                return -1;
            }
            bufferSize = bufferSize*2;
        }
        strcat(buffer,temp);
            
        x++;
    }

    /* Add directory structure end marker */
    strcpy(temp,"</directory_structure>\r\n");
    usedBufferSize += (unsigned int)strlen(temp);
    if((usedBufferSize+1) > bufferSize)
    {
        if(increaseAllocatedMemory(&buffer, bufferSize, bufferSize*2) < 0)
        {
            printf("Error increasing memory for listing\n");
            free(buffer);
            free(temp);
            return -1;
        }
        bufferSize = bufferSize*2;
    }
    strcat(buffer,temp);

    /* Assign the buffer */
    *fileSystemBuffer = buffer;
    free(temp);
    return 0;
}




/*****************************************************************************/
/* Function: increaseAllocatedMemory                                         */
/* Purpose: Microsoft's realloc function is evil.                            */
/* Input: Address of an existing char* pointer, previous size, new size.     */
/* Returns: 0 on success, -1 on error.                                       */
/*****************************************************************************/
int increaseAllocatedMemory(char** pAddrMemory, int oldSize, int newSize)
{
    char* temp = NULL;
    char* pMemory = *pAddrMemory;

    /* Attempt to allocate the new memory block. */
    /* Then copy over the contents of the previous memory block. */
    temp = malloc(newSize);  
    if(temp == NULL)
    {
        printf("Error re-mallocing for directory listing\n");
        return -1;
    }
    memset(temp,0,newSize);
    memcpy(temp,pMemory,oldSize);

    /* Free the old block of memory */
    if(pMemory != NULL)
    {
        free(pMemory);
        pMemory = NULL;
    }

    /* Point the existing pointer to the new memory block */
    *pAddrMemory = temp;

    return newSize;
}
