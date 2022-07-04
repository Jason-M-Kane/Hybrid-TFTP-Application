/*****************************************************************************/
/* serverDirListingRoutines.c - Routines involved in the storage and display */
/*                              of file systems on remote servers.           */
/*****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <windows.h>
#include <stdio.h>
#include <commctrl.h>
#include "fileSystemType.h"


/***********************/
/* Function Prototypes */
/***********************/
int readTreeIntoBuffer(char* file, fsItem* root);
int propagateCheckState(HWND hwndTV, fsItem* root,int newCheckstate);
int insertTreeviewItem(fsItem* root, HTREEITEM Parent, HWND hWnd,int tvID);
fsItem* insertFSNode(fsItem* root, fsItem* newFSItem, int searchId);
void removeFs(fsItem* root);




/*****************************************************************************/
/*Function: readTreeIntoBuffer                                               */
/*Purpose:  Reads a server directory listing from a cached file.  The listing*/
/*          is parsed and a linked filesystem list is created.               */
/*Input:  Path to a cached filesystem listing file, pointer to the root node */
/*        of the virtual filesystem that is to be created.                   */
/*Return: Returns 0 on success, -1 on failure                                */
/*****************************************************************************/
int readTreeIntoBuffer(char* file, fsItem* root)
{
    fsItem* newFSItem;
    FILE* inFile = NULL;
    int x = 1;
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
        index += 7;
        newIndex += 7;


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

        /**************************************************/
        /* Add the new node to the virtual directory tree */
        /**************************************************/
        newFSItem = NULL;
        newFSItem = (fsItem*)malloc(sizeof(fsItem));
        if(newFSItem == NULL)
        {
            //Error mallocing
            return -1;
        }
        newFSItem->folder = isDir;                     //Is the current item a folder?  True or false
        newFSItem->checked = FALSE;                    //Init to not checked
        newFSItem->ID = nodeId;                        //ID - A unique number to identify a folder
        newFSItem->parentID = parentNode;              //ID of the parent directory of the item
        newFSItem->level = x-1;                        //# of levels down from the root directory
        newFSItem->fileSize = i64filesize;             //If the item is a file, the filesize in bytes
        newFSItem->hashType = hashType;                //Type of file hash
        strcpy(newFSItem->hashValue,hashval);          //Value of the hash function (Hex String)
        strcpy(newFSItem->name,itemName);              //Name of the file or directory
        strcpy(newFSItem->fullpathAndName,fullPath);   //Full path and name of the file or directory
        newFSItem->currentlevelNextItem = NULL;        //Next Item assoc. with the current FS level
        newFSItem->nextLevelDown = NULL;               //Pointer to the next item

        if( insertFSNode(root, newFSItem,newFSItem->parentID) == NULL )
        {
            MessageBox(NULL,newFSItem->name,"Failed",MB_OK);
        }
    }

    fclose(inFile);

    return 0;
}




/*****************************************************************************/
/*Function: insertFSNode                                                     */
/*Purpose:  Searches a given filesystem linked list for where to insert      */
/*          a new node.  When the slot has been found, it adds the item.     */
/*Input:  Pointer to the beginning of where to search in the filesystem,     */
/*        the new file system item to be inserted, the ID of the parent      */
/*        directory of the item to be inserted.                              */
/*Return: Returns pointer to the item on success, NULL on failure.           */
/*****************************************************************************/
fsItem* insertFSNode(fsItem* root, fsItem* newFSItem, int searchId)
{
    fsItem* currentDirectory = root;
    fsItem* currentFSItem, *dirTest;

    /* Bad Ptr */
    if(root == NULL)
        return NULL;

    /* Check to see if this is the parent directory of the item */
    if(currentDirectory->ID == searchId)
    {
        /* Insert the item at the next level down */
        currentFSItem = currentDirectory;
        if(currentFSItem->nextLevelDown == NULL)
        {
            currentFSItem->nextLevelDown = newFSItem;
            return newFSItem;
        }
        else
        {
            currentFSItem = currentFSItem->nextLevelDown;
        }

        /* Iterate through all items at this level */
        while(1)
        {
            if(currentFSItem->currentlevelNextItem == NULL)
            {
                currentFSItem->currentlevelNextItem = newFSItem;
                return newFSItem;
            }
            else
            {
                currentFSItem = currentFSItem->currentlevelNextItem;
            }
        }
    }
    else
    {
        if((dirTest = currentDirectory->nextLevelDown) != NULL)
        {
                if(insertFSNode(dirTest,newFSItem,searchId) == newFSItem)
                {
                    return newFSItem;
                }
        }

        if((dirTest = currentDirectory->currentlevelNextItem) != NULL)
        {
                if(insertFSNode(dirTest,newFSItem,searchId) == newFSItem)
                {
                    return newFSItem;
                }
        }
    }

    return NULL;
}




/*****************************************************************************/
/*Function: insertTreeviewItem                                               */
/*Purpose:  Adds the contents of a virtual file system to a Windows treeview */
/*Input:  Pointer to the root of the virtual FS, Parent of the node to be    */
/*        added to the treeview, HWND to the dialog, ID of the treeview.     */
/*Return: 0 on success.                                                      */
/*****************************************************************************/
int insertTreeviewItem(fsItem* root, HTREEITEM Parent, HWND hWnd, int tvID)
{
    fsItem* dirTest;
    TV_INSERTSTRUCT tvinsert;
    HTREEITEM newParent;

    /* Insert the current item into the treeview */
    tvinsert.hParent=Parent;		// top most level no need handle
    if(Parent == NULL)
    {
        tvinsert.hInsertAfter=TVI_ROOT; // work as root level
    }
    else
    {
        tvinsert.hInsertAfter=TVI_SORT;
    }
    tvinsert.item.pszText=root->name;
    tvinsert.item.cchTextMax = (int)strlen(root->name)+1;
    if(root->folder == TRUE)
    {
        tvinsert.item.mask=TVIF_TEXT|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_PARAM;
	    tvinsert.item.iImage=0;
	    tvinsert.item.iSelectedImage=1;
    }
    else
    {
        tvinsert.item.mask=TVIF_TEXT|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_PARAM;
        tvinsert.item.iImage=2;
        tvinsert.item.iSelectedImage=2;
    }
    tvinsert.item.lParam = (LPARAM)((unsigned int*)root); //Ptr to actual item
    newParent=(HTREEITEM)SendDlgItemMessage(hWnd,tvID,TVM_INSERTITEM,0,(LPARAM)&tvinsert);
    root->hTreeItem = newParent;
 
    /* Try to insert at the next level down */
    if((dirTest = root->nextLevelDown) != NULL)
    {
        insertTreeviewItem(dirTest,newParent,hWnd,tvID);
    }

    /* Try to insert at the next level across */
    if((dirTest = root->currentlevelNextItem) != NULL)
    {
        insertTreeviewItem(dirTest,Parent,hWnd,tvID);
    }

    return 0;
}




/*****************************************************************************/
/*Function: propagateCheckState                                              */
/*Purpose:  When the user checks/unchecks a folder, this function will       */
/*          automatically perform the same operation to all files within     */
/*          that virtual folder.                                             */
/*Input:  HWND to the treeview, pointer to the folder item in question,      */
/*        state the checkboxes will be switched to.                          */
/*Return: 0 on success.                                                      */
/*****************************************************************************/
int propagateCheckState(HWND hwndTV, fsItem* root,int newCheckstate)
{
    fsItem* nextItem = NULL;

    /* Set the state */
    TreeView_SetCheckState(hwndTV, root->hTreeItem, newCheckstate);
    root->checked = newCheckstate;

    /* Try to insert at the next level down */
    if((nextItem = root->nextLevelDown) != NULL)
    {
        propagateCheckState(hwndTV,nextItem,newCheckstate);
    }

    /* Try to insert at the next level across */
    if((nextItem = root->currentlevelNextItem) != NULL)
    {
        propagateCheckState(hwndTV,nextItem,newCheckstate);
    }

    return 0;
}




/*****************************************************************************/
/*Function: removeFs                                                         */
/*Purpose:  Removes a filesystem from the root.                              */
/*Input:  Pointer to the beginning of the filesystem.                        */
/*Return: Void.                                                              */
/*****************************************************************************/
void removeFs(fsItem* root)
{
    fsItem* nextItem = NULL;

    /* Bad Ptr */
    if(root == NULL)
        return;

    /* Try to insert at the next level down */
    if((nextItem = root->nextLevelDown) != NULL)
    {
        removeFs(nextItem);
    }

    /* Try to insert at the next level across */
    if((nextItem = root->currentlevelNextItem) != NULL)
    {
        removeFs(nextItem);
    }
    root->nextLevelDown = NULL;
    root->currentlevelNextItem = NULL;
    free(root);
    root = NULL;

    return;
}
