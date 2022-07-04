/*****************************************************************************/
/* dlQueueManagement.c - Routines involved in the display of queued download */
/*                       items.                                              */
/*****************************************************************************/
#pragma warning(disable : 4996)

/************/
/* Includes */
/************/
#include <windows.h>
#include <stdio.h>
#include <commctrl.h>
#include "fileSystemType.h"
#include "resource.h"
#include "downloadStatus.h"
#include "initialComms.h"


/***********/
/* Defines */
/***********/
#define maxcol 7                            //# columns in the listview


/********************/
/* Listview Globals */
/********************/
static HWND G_hList=NULL;				    //Handle to Listview
LVCOLUMN LvCol;								//Column struct for Listview
LVITEM LvItem;								//Item struct for Listview
static queueItem* G_rootDLQueue = NULL;
static queueItem* G_TailDLQueue = NULL;
static int G_DlQueueSize = 0;
static int G_ListSetup = FALSE;
static int G_QueueStart = 0;
static int G_DlQueueIndex = 0;


/***********************/
/* Function Prototypes */
/***********************/
int createDownloadQueue(fsItem** rootFS, int numServers);
static int addCheckedItems(fsItem* root, char* ipaddress);
void destroyDownloadQueue(queueItem* root);
int Populate_Listview(HWND hWnd);
void updateLVItem(queueItem* selectedQueueItem);
queueItem* getNextQueueItem(void);




/*****************************************************************************/
/*Function: createDownloadQueue                                              */
/*Purpose: Creates a download queue.                                         */
/*Returns: 0 on success, -1 on failure.                                      */
/*****************************************************************************/
int createDownloadQueue(fsItem** rootFS, int numServers)
{
    int x,index;
	char ipaddress[32];

    /* Recursively add checked items */
    for(x = 0; x < numServers; x++)
    {
		strcpy(ipaddress,rootFS[x]->name);
		index = 0;
		while(ipaddress[index] != '_')
			index++;
		ipaddress[index] = '\0';

		if( addCheckedItems(rootFS[x],ipaddress) < 0)
            return -1;
    }

    return 0;
}




/*****************************************************************************/
/*Function: addCheckedItems                                                  */
/*Purpose: Recursively traverses virtual filesystem determining which FS     */
/*         items should be added.                                            */
/*Returns: 0 on success, -1 on failure.                                      */
/*****************************************************************************/
static int addCheckedItems(fsItem* root, char* ipaddress)
{
    fsItem* nextItem = NULL;

    /* Add the item if its checked and not a folder */
    if( (root->checked) && (root->folder == FALSE) )
    {
        /* Root */
        if(G_rootDLQueue == NULL)
        {
            G_rootDLQueue = (queueItem*)malloc(sizeof(queueItem));
            if(G_rootDLQueue == NULL)
            {
                //Error allocating memory
                return -1;
            }
            G_TailDLQueue = G_rootDLQueue;
        }
        else
        {
            G_TailDLQueue->nextItem = (queueItem*)malloc(sizeof(queueItem));
            if(G_TailDLQueue->nextItem == NULL)
            {
                //Error allocating memory
                return -1;
            }
            G_TailDLQueue = G_TailDLQueue->nextItem;   
        }
        G_DlQueueSize++;
		strcpy(G_TailDLQueue->ipaddress,ipaddress);
        G_TailDLQueue->avgSpeed = 0;
        G_TailDLQueue->status = 0;
        G_TailDLQueue->bytesRemain = root->fileSize;
        G_TailDLQueue->instSpeed = 0;
        G_TailDLQueue->percentComplete = 0;
        memcpy(&G_TailDLQueue->dLItem, root, sizeof(fsItem));
        G_TailDLQueue->dLItem.currentlevelNextItem = G_TailDLQueue->dLItem.nextLevelDown = NULL;
        G_TailDLQueue->dLItem.hTreeItem = 0;
        G_TailDLQueue->lvIndex = -1;
        G_TailDLQueue->nextItem = NULL;
    }

    /* Check the next level down */
    if((nextItem = root->nextLevelDown) != NULL)
    {
        addCheckedItems(nextItem, ipaddress);
    }

    /* Check the next level across */
    if((nextItem = root->currentlevelNextItem) != NULL)
    {
        addCheckedItems(nextItem, ipaddress);
    }

    return 0;
}




/*****************************************************************************/
/*Function: destroyDownloadQueue                                             */
/*Purpose: Deletes a download queue.                                         */
/*Returns: Nothing                                                           */
/*****************************************************************************/
void destroyDownloadQueue(queueItem* root)
{
    queueItem* currentItem,*nextItem;

    if(root == NULL)
        return;

    currentItem = root;
    do
    {
        nextItem = currentItem->nextItem;
        free(currentItem);
        currentItem = nextItem;
    }
    while(currentItem != NULL);
    G_DlQueueSize = 0;

    return;
}




/*****************************************************************************/
/*Function: Populate_Listview                                                */
/*Purpose: Loads download queue information into a Listview control.         */
/*Returns: 0 on success, -1 on failure                                       */
/*****************************************************************************/
int Populate_Listview(HWND hWnd)
{
    int item, subitem, index;
    char tmp[100];
	char *col_text[] = {"Name", "Size", "% Done", 
	                    "Avg Speed", "Inst Speed", "Bytes Remaining",
                        "Status"};
	int col_length[maxcol] = {100,80,80,80,80,100,200};

    queueItem* currentQueueItem = G_rootDLQueue;


	//Get the Handle to the List Control, add checkboxes and grids
	G_hList=GetDlgItem(hWnd,IDC_DLQUEUE);

    if(G_ListSetup == FALSE)
    {
        G_ListSetup = TRUE;
	    ListView_SetExtendedListViewStyle(G_hList, 
            LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT );
			
	    //Add Column Header data
        memset(&LvCol,0,sizeof(LvCol));
	    LvCol.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM; // Type of mask

	    //Set up columns in the ListView
	    for (index = 0; index < maxcol; index++)
        {
		    LvCol.cx=col_length[index];       // width between each column
		    LvCol.pszText = col_text[index];  // The text for the column.
		    SendMessage(G_hList,LVM_INSERTCOLUMN,index,(LPARAM)&LvCol);
	    }
    }
	else
	{
		ListView_DeleteAllItems(G_hList);
	}

	//Setting properties Of Items:
    memset(&LvItem,0,sizeof(LvItem)); // Reset Item Struct
	LvItem.mask=LVIF_TEXT;   // Text Style
	LvItem.cchTextMax = 400; // Max size of text
                
	//Add the items to the listview
	for(item = 0; item < G_DlQueueSize; item++)
    {
        if(currentQueueItem == NULL)
            return -1;

		LvItem.iItem=G_QueueStart+item;          // Select the item row
		LvItem.iSubItem=0;
        LvItem.pszText = currentQueueItem->dLItem.name;
		currentQueueItem->lvIndex = (int)SendMessage(G_hList,LVM_INSERTITEM,0,(LPARAM)&LvItem);
						
		//Select the column text for a given row
		for(subitem = 1; subitem < maxcol; subitem++)
        {
			LvItem.iSubItem=subitem;
			switch(subitem)
            {
				case 1:
                    sprintf(tmp,"%I64d",currentQueueItem->dLItem.fileSize);
                    LvItem.pszText = tmp;
                    break;
				case 2:
                    sprintf(tmp,"%d",currentQueueItem->percentComplete);
					LvItem.pszText = tmp;
                    break;
				case 3:
                    sprintf(tmp,"%.2f",currentQueueItem->avgSpeed);
					LvItem.pszText = tmp;
                    break;
				case 4:
                    sprintf(tmp,"%.2f",currentQueueItem->instSpeed);
					LvItem.pszText = tmp;
                    break;
				case 5:
                    sprintf(tmp,"%I64d",currentQueueItem->bytesRemain);
					LvItem.pszText = tmp;
                    break;
                case 6:
                    switch(currentQueueItem->status)
                    {
                        case QUEUED:
                            sprintf(tmp,"Queued");
                            break;
                        case DL_COMPLETE:
                            sprintf(tmp,"Download Complete");
                            break;
                        case DOWNLOADING:
                            sprintf(tmp,"Downloading");
                            break;
                        case DL_FAILED:
                            sprintf(tmp,"Download Failed");
                            break;
                        case RETRYING:
                            sprintf(tmp,"Retrying...");
                            break;
                        case SERVER_REFUSED:
                            sprintf(tmp,"Server Refused");
                            break;
                        case HASH_FAILED:
                            sprintf(tmp,"Hash Failed");
                            break;
					    case CHECKING_MD5_HASH:
						    sprintf(tmp,"Checking MD5 HASH");
						    break;
                        case FAIL_PARTIAL_DL:
                            sprintf(tmp,"Fail - Partial Download");
                            break;
                        case RESUMING:
                            sprintf(tmp,"Resuming Partial D/L");
                            break;
                        case WAIT_WRITES:
                            sprintf(tmp,"Waiting for Write Thread");
                            break;
                        default:
                            sprintf(tmp,"Unknown Status");
                            break;
                    }
					LvItem.pszText = tmp;
                    break;
                default:
                    LvItem.pszText = "Cant get here";
                    break;
			}
			SendMessage(G_hList,LVM_SETITEM,0,(LPARAM)&LvItem); // Send to the Listview
		}
        currentQueueItem = currentQueueItem->nextItem;
	}

	return 0;
}



/*****************************************************************************/
/*Function: updateLVItem                                                     */
/*Purpose: Updates a listview item in the queue list.                        */
/*Returns: Void                                                              */
/*****************************************************************************/
void updateLVItem(queueItem* selectedQueueItem)
{
    int index, subitem;
    char tmp[100];

    if(G_hList == NULL)
        return;
    index = selectedQueueItem->lvIndex;

    //Select the column text for a given row
	for(subitem = 0; subitem < maxcol; subitem++)
    {
		switch(subitem)
        {
            case 0:
                ListView_SetItemText(G_hList,index,subitem,selectedQueueItem->dLItem.name);
                continue;
            break;
			case 1:
                sprintf(tmp,"%I64d",selectedQueueItem->dLItem.fileSize);
            break;
		    case 2:
                sprintf(tmp,"%d",selectedQueueItem->percentComplete);
                break;
			case 3:
                sprintf(tmp,"%.2f",selectedQueueItem->avgSpeed);
                break;
			case 4:
                sprintf(tmp,"%.2f",selectedQueueItem->instSpeed);
                break;
			case 5:
                sprintf(tmp,"%I64d",selectedQueueItem->bytesRemain);
                break;
            case 6:
                switch(selectedQueueItem->status)
                {
                    case QUEUED:
                        sprintf(tmp,"Queued");
                        break;
                    case DL_COMPLETE:
                        sprintf(tmp,"Download Complete");
                        break;
                    case DOWNLOADING:
                        sprintf(tmp,"Downloading");
                        break;
                    case DL_FAILED:
                        sprintf(tmp,"Download Failed");
                        break;
                    case RETRYING:
                        sprintf(tmp,"Retrying...");
                        break;
                    case SERVER_REFUSED:
                        sprintf(tmp,"Server Refused");
                        break;
                    case HASH_FAILED:
                        sprintf(tmp,"Hash Failed");
                        break;
					case CHECKING_MD5_HASH:
						sprintf(tmp,"Checking MD5 HASH");
						break;
                    case FAIL_PARTIAL_DL:
                        sprintf(tmp,"Fail - Partial Download");
                        break;
                    case RESUMING:
                        sprintf(tmp,"Resuming Partial D/L");
                        break;
                    case WAIT_WRITES:
                        sprintf(tmp,"Waiting for Write Thread");
                        break;
                    default:
                        sprintf(tmp,"Unknown Status");
                        break;
                }
                break;
            default:
                strcpy(tmp,"Cant get here");
                break;
        }
        ListView_SetItemText(G_hList,index,subitem,tmp);
	}

    return;
}




/*****************************************************************************/
/*Function: getNextQueueItem                                                 */
/*Purpose: Returns the next item in the download queue.                      */
/*Returns: Ptr to a queue item                                               */
/*****************************************************************************/
queueItem* getNextQueueItem(void)
{
    int x;
    int found = FALSE;
    queueItem* rtnval = G_rootDLQueue;

    /* No items on queue */
    if(G_DlQueueSize == 0)
        return NULL;

	/* Fast-forward to the index location */
	for(x=0; x < G_DlQueueIndex; x++)
	{
		if(rtnval != NULL)
			rtnval = rtnval->nextItem;
	}

    /********************************************************/
    /* Navigate through the linked list to the current item */
    /* If its already downloaded or downloading, keep going */
    /********************************************************/
    for(x=G_DlQueueIndex; x < G_DlQueueSize; x++)
    {
        /* For some reason the item doesnt exist */
        if(rtnval == NULL)
            break;
		
		/* Check to see if the current item is downloadable */
		if( (rtnval->status == DL_FAILED) || 
           	(rtnval->status == QUEUED) ||
			(rtnval->status == SERVER_REFUSED) ||
            (rtnval->status == FAIL_PARTIAL_DL) )
        {
            /* If Failed Or Server Refused, see if server is still there */
            /* (Server Failure Detection) */
            if(rtnval->status != QUEUED)
            {
                if(serverFailDetect(rtnval->ipaddress) < 0)
                {
                    rtnval->status = SERVER_REFUSED;
                    updateLVItem(rtnval);
                    G_DlQueueIndex++;
            		rtnval = rtnval->nextItem;
                    continue;
                }
            }
            found = TRUE;
			break;
        }
		G_DlQueueIndex++;
		rtnval = rtnval->nextItem;
    }


    /******************************************************/
    /* If the queue was exhausted set the new index to 0  */
    /* So that downloads will restart from the beginning  */
    /* On the next call to this function.                 */
    /******************************************************/
    if(!found)
    {
        G_DlQueueIndex = 0;
        rtnval = NULL;
    }
    else
    {
        G_DlQueueIndex++;
    }

    /* Reset the DL Queue Index if it exceeds the size of the queue */
    if(G_DlQueueIndex > (G_DlQueueSize-1))
    {
        G_DlQueueIndex = 0;
    }

    return rtnval;
}
