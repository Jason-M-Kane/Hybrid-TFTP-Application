/****************************************************************************/
/* FS_datastructures.h - Contains the virtual file system data structure.   */
/****************************************************************************/
#ifndef FS_DATASTRUCTURES_H
#define FS_DATASTRUCTURES_H


/* Data Structures */
typedef struct listItem
{
    int folder;                  //Is the current item a folder?  True or false
    unsigned int ID;             //ID - A unique number to identify a folder
    unsigned int parentID;       //ID of the parent directory of the item
    int level;                   //# of levels down from the root directory
    unsigned __int64 fileSize;   //If the item is a file, the filesize in bytes
    unsigned int hashType;       //Type of file hash
    char hashValue[257];         //Value of the hash function (Hex String)
    char* name;                  //Name of the file or directory
    char* fullpathAndName;       //Full path and name of the file or directory
    struct listItem* currentlevelNextItem;   //Next Item assoc. with the 
                                             //current FS level
    struct listItem* nextItem;   //Pointer to the next item
} listItem;



#endif
