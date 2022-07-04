/*****************************************************************************/
/* fileSystemType.h - Data strcture used for remote file system storage.     */
/*****************************************************************************/
#ifndef FILE_SYSTEM_TYPE_H
#define FILE_SYSTEM_TYPE_H
#include <commctrl.h>

#define MD5_HASH      0
#define MAX_FS_LEVELS 256        //Limit # of directory levels to 256

/* Virtual File System Data Structure */
typedef struct fsItem
{
    int folder;                  //Is the current item a folder?  True or false
    int checked;                 //Is the checkbox checked for the item
    unsigned int ID;             //ID - A unique number to identify a folder
    unsigned int parentID;       //ID of the parent directory of the item
    int level;                   //# of levels down from the root directory
    unsigned __int64 fileSize;   //If the item is a file, the filesize in bytes
    unsigned int hashType;       //Type of file hash
    char hashValue[257];         //Value of the hash function (Hex String)
    char name[400];                  //Name of the file or directory
    char fullpathAndName[400];       //Remote Full path and name of the file or directory
    char localfullpathAndName[400];  //Local full path and name
    fsItem* currentlevelNextItem;   //Next Item assoc. with the current FS level
    struct fsItem* nextLevelDown;   //Pointer to the next item
    HTREEITEM hTreeItem;            //Handle to the inserted tree item
} fsItem;


/* Download Queue Linked List Datatype */
typedef struct queueItem
{
	char ipaddress[32];
    SOCKET* sockArray;
    int numConnectionsToUse;
    unsigned short firstPort;
    unsigned short firstBlock;
    unsigned __int64 bytesRemain;
    unsigned __int64 fileOffset;
    int percentComplete;
    int status;
    int lvIndex;         //Listview index associated with item
    float avgSpeed;
    float instSpeed;
    fsItem dLItem;      //Copy of item to be downloaded
    struct queueItem* nextItem;
} queueItem;


#endif
