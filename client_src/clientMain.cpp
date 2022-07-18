/****************************************************************************/
/* clientMain.c - A GUI Hybrid TFTP Client for Windows                      */
/*                                                                          */
/* Jason Kane 8123  1/16/10                                                 */
/*                                                                          */
/* 1.10 - Initial Release.  GigE has a theoretical max of 119.2 MB/sec      */
/* 1.11 - Fixed Bug with final ack.  Added ability to modify write params.  */
/*        Fixed multiple server bug.                                        */
/* 1.12 - Fixed a bug in the refresh button code.                           */
/* 1.13 - Added hash checking disable/enable button, defaulted write size to*/
/*        4MB                                                               */
/****************************************************************************/
#pragma warning(disable : 4996)


/************/
/* Includes */
/************/
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <commctrl.h>
#include "shlobj.h"             //For Folder Browsing
#include "resource.h"
#include "fileSystemType.h"
#include "serverDirListingRoutines.h"
#include "dlQueueManagement.h"
#include "opcode_definitions.h"
#include "sockfctns.h"
#include "clientFctns.h"
#include "initialComms.h"
#include "serverDatabase.h"
#include "cmn_util.h"
#include "downloadStatus.h"


/***********/
/* Defines */
/***********/
#define WIN32_LEAN_AND_MEAN      //Dont include everything
#define MAX_SIMULT_DOWNLOADS    1 //Max # seperate simultaneous d/ls allowed (fix @ 1)

#define MAX_SIMULT_CONNECTIONS  2 //Max requested connections per download
#define DESIRED_CONNECT_PER_DL	2 //Desired seperate connections per download


/***********/
/* Globals */
/***********/
HINSTANCE hInst;                                    // main function inst handle
char G_outputDir[MAX_PATH+1]="C:\\mydownloads\\";   //Output directory
static char outFolderName[MAX_PATH + 1];
HWND hTree;                             // Handle to the Treeview used to display server directory listings
int G_DisableCheckStateCode = FALSE;    // Disables checking the state of treeview checkboxes
static char errorString[200];
SOCKET querySocket;
HWND G_hWnd;                            //Global handle to the main dialog
HANDLE G_h_KillProgram;                 //Global handle to kill the program
int G_DisableHashChecking = FALSE;

/* Virtual filesystem related variables */
fsItem** G_rootFSPtr = NULL;
int G_totalTreeRootItems = 0;
int G_lastnumservers = 0;

/* Global Connection oriented variables */
int G_TotalAllowedConnections = MAX_SIMULT_CONNECTIONS;
int G_TotalUsedConnections = 0;

extern int G_TEMP_BUFFER_BACKLOG;             /* # of backlog buffers per thread */
extern int G_WRITE_BUFFER_SIZE;      /* Size of the client's write buffer in memory */
                                            /* Must be less than 2GB or overlap of incomming packets may occur */
extern int G_WRITE_SIZE;


/***********************/
/* Function Prototypes */
/***********************/
BOOL CALLBACK MainDialog(HWND hWnd, UINT message, WPARAM wParam,LPARAM lParam);
DWORD WINAPI manageHybridDownloads(LPVOID lpParam);




/*****************************************************************************/
/*Function: WinMain                                                          */
/*Purpose: Main entry point to the program.                                  */
/*Returns: LRESULT                                                           */
/*****************************************************************************/
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPTSTR lpCmdLine, int nCmdShow)
{
    HANDLE hThread;
    DWORD dwThreadId;
    int counter = 0;
    INITCOMMONCONTROLSEX InitCtrlEx;
	InitCtrlEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
	InitCtrlEx.dwICC  = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
	InitCommonControlsEx(&InitCtrlEx);

    /* Init Winsock */
    if(initializeWinsock() < 0)
    {
        return -1;
    }
    initLogging();
    G_h_KillProgram = CreateSemaphore(NULL,0,1,NULL);

    /********************************/
    /* Bind Local Client to Port 69 */
    /********************************/
    querySocket = INVALID_SOCKET;
    while(querySocket == INVALID_SOCKET)
    {
        querySocket = udpOpenBindSocket(5000);

        /* Determine when to give up */
        counter++;
        if( (counter > 50) && (querySocket == INVALID_SOCKET) )
        {
            MessageBox(G_hWnd,"Could not bind to UDP port 5000","Socket Error",MB_OK);
            return -1;
        }
    }

	//Assign the handle to the current program's instance to a global
	hInst = hInstance;

    /* Spawn download management thread */
    hThread = CreateThread( 
		NULL,              // default security attributes
		0,                 // use default stack size  
		manageHybridDownloads,    // thread function 
		(void*)NULL,       // argument to thread function 
		0,                 // use default creation flags 
		&dwThreadId);	   // returns the thread identifier 
	if (hThread == NULL)
    {
		MessageBox(NULL, "Error in CreateThread, Download Manager Creation FAILED.", "OS Error", MB_OK);
        return -1;
    }

	//Create the main dialog box
	DialogBox(hInst, (LPCTSTR)IDD_CLIENT, NULL, (DLGPROC)MainDialog);

	return 0;
}





/******************************************************************************/
/*Function: DialogProc                                                        */
/*Purpose: Processes messages for the main dialog.                            */
/*Returns: LRESULT                                                            */
/******************************************************************************/
BOOL CALLBACK MainDialog(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{ 	 

        /* Init the dialog */
		case WM_INITDIALOG: 
		{
            G_hWnd = hWnd;
            HIMAGELIST hImageList;      // Image list array hadle
            HBITMAP hBitMap;            // bitmap handler

			SetDlgItemText(hWnd, IDC_OUTPUTDIR, G_outputDir);
			//InitCommonControls(); //Required to get the treeview to work
			hTree=GetDlgItem(hWnd,IDC_FILE_TREE);

            SetDlgItemInt(hWnd,IDC_wbedit,G_WRITE_BUFFER_SIZE,0);
            SetDlgItemInt(hWnd,IDC_wsedit,G_WRITE_SIZE,0);
            SetDlgItemInt(hWnd,IDC_bufedit,G_TEMP_BUFFER_BACKLOG,0);

            //Uncheck the disable hash checking button
            CheckDlgButton(hWnd,IDC_DISABLE_HASH,0);

            /**********************************************************/
			/* create the image list and put it into the tree control */
            /**********************************************************/
			hImageList=ImageList_Create(16,16,ILC_COLOR16,2,10);					  // Macro: 16x16:16bit with 2 pics [array]
			hBitMap=LoadBitmap(hInst,MAKEINTRESOURCE(IDB_TREE));					  // Load the Folder image from the resource
			ImageList_Add(hImageList,hBitMap,NULL);								      // Macro: Attach the image to the image list
			DeleteObject(hBitMap);
            hBitMap=LoadBitmap(hInst,MAKEINTRESOURCE(IDB_FILE));					  // Load the picture image from the resource
   			ImageList_Add(hImageList,hBitMap,NULL);								      // Macro: Attach the image to the image list
			DeleteObject(hBitMap);
		    SendDlgItemMessage(hWnd,IDC_FILE_TREE,TVM_SETIMAGELIST,0,(LPARAM)hImageList); // Add the image list to the tree control

            /* Insert virtual file system for each server */
            Populate_Listview(hWnd);
		}
		break;


        /* This code activates whenever the user checks or unchecks an item in the treeview */
        #define UM_CHECKSTATECHANGE (WM_USER + 100)
        case WM_NOTIFY:
        {
            LPNMHDR lpnmh = (LPNMHDR) lParam;
            TVHITTESTINFO ht = {0};
                
            if((lpnmh->code  == NM_CLICK) && (lpnmh->idFrom == IDC_FILE_TREE))
            {
                DWORD dwpos = GetMessagePos();

                // include <windowsx.h> and <windows.h> header files
                ht.pt.x = GET_X_LPARAM(dwpos);
                ht.pt.y = GET_Y_LPARAM(dwpos);
                MapWindowPoints(HWND_DESKTOP, lpnmh->hwndFrom, &ht.pt, 1);

                TreeView_HitTest(lpnmh->hwndFrom, &ht);
                     
                if(TVHT_ONITEMSTATEICON & ht.flags)
                {
                    PostMessage(hWnd, UM_CHECKSTATECHANGE, 0, (LPARAM)ht.hItem);
                }
            }
        }
        break;

        /* This code activates whenever the user checks or unchecks an item in the treeview */
        case UM_CHECKSTATECHANGE:
        {
            fsItem* ptrFSItem;
            TVITEM test;
            char buffer[400];
            HTREEITEM   hItemChanged = (HTREEITEM)lParam;

            //If disabled skip this
            if(G_DisableCheckStateCode)
                break;

            memset(buffer,0,400);
            ZeroMemory(&test, sizeof(TVITEM));
            test.hItem = hItemChanged;
            test.mask = TVIF_TEXT | TVIF_STATE | TVIF_HANDLE| TVIF_PARAM;
            test.cchTextMax = 399;
            test.pszText = buffer;
            TreeView_GetItem((HWND)hTree,&test);

            /* Figure out if we checked or unchecked the item. */
            /* Set the corresponding data in the structure.    */
            ptrFSItem = (fsItem*)test.lParam;
            if(test.state & 0x2000)
            {
                ptrFSItem->checked = TRUE;
            }
            else
            {
                ptrFSItem->checked = FALSE;
            }
            
            /* If its a folder check or uncheck everything underneath it */
            if(ptrFSItem->folder)
            {
                G_DisableCheckStateCode = TRUE;
                propagateCheckState(hTree,ptrFSItem,ptrFSItem->checked);
                G_DisableCheckStateCode = FALSE;
            }
        }
        break;


		case WM_COMMAND: // Controling the Buttons
		{
			switch (LOWORD(wParam)) // what we pressed on?
			{

                case IDC_DISABLE_HASH:
                {
                    if(IsDlgButtonChecked(hWnd,IDC_DISABLE_HASH) == BST_CHECKED)
                    {
                        //Disable Hash Checking
                        G_DisableHashChecking = TRUE;
                    }
					else
                    {
                        //Enable Hash Checking
                        G_DisableHashChecking = FALSE;
                    }
                    break;
                }

                case IDC_DOWNLOAD:
                {
                    /* Add to the download queue and display it */
                    createDownloadQueue(G_rootFSPtr, G_totalTreeRootItems);
                    Populate_Listview(hWnd);
                }
                break;

                /* Set write related variables */
                case IDC_SET1:
                {
                    /* Ensure buffer size is divisible by 65464 */
                    int div;
                    G_WRITE_BUFFER_SIZE = GetDlgItemInt(hWnd,IDC_wbedit,NULL,0);
                    div = G_WRITE_BUFFER_SIZE / 65464;
                    G_WRITE_BUFFER_SIZE = 65464 * div;
                    if(G_WRITE_BUFFER_SIZE <= 0)
                        G_WRITE_BUFFER_SIZE = 65464;
                    SetDlgItemInt(hWnd,IDC_wbedit,G_WRITE_BUFFER_SIZE,0);
                }
                break;
                case IDC_SET2:
                {
                    int tmp;
                    char sharepathroot[32];
                    DWORD secPerCluster,bytesPerSector,numFreeClusters,totalNumClusters;
                    memset(sharepathroot,0,32);
                    strncpy(sharepathroot,G_outputDir,3);
                    GetDiskFreeSpace(sharepathroot,&secPerCluster,&bytesPerSector,&numFreeClusters,&totalNumClusters);
                    G_WRITE_SIZE = GetDlgItemInt(hWnd,IDC_wsedit,NULL,0);

                    tmp = G_WRITE_SIZE / bytesPerSector;
                    G_WRITE_SIZE = tmp * bytesPerSector;
                    if(G_WRITE_SIZE <= 0)
                        G_WRITE_SIZE = bytesPerSector;
                    SetDlgItemInt(hWnd,IDC_wsedit,G_WRITE_SIZE,0);
                }
                break;
                case IDC_SET3:
                {
                    G_TEMP_BUFFER_BACKLOG = GetDlgItemInt(hWnd,IDC_bufedit,NULL,0);
                    if(G_TEMP_BUFFER_BACKLOG <= 0)
                        G_TEMP_BUFFER_BACKLOG = 1;
                    SetDlgItemInt(hWnd,IDC_bufedit,G_TEMP_BUFFER_BACKLOG,0);
                }
                break;

                /* Browse for local download folder */
                case IDC_BROWSE:
                {
                    IMalloc* imalloc = NULL;
                    BROWSEINFO browse;   //For Browsing for the Output Folder
                    LPITEMIDLIST pidl;

                    //Set up the GUI folder selection
					ZeroMemory(&browse, sizeof(browse));
                    browse.hwndOwner = hWnd;
                    browse.lpfn = NULL;
                    browse.lpszTitle = "Select An Output Directory";
                    browse.pidlRoot = NULL; /*Root Defaults to Desktop*/
                    browse.pszDisplayName = outFolderName; /*Selected Folder Name*/
                    browse.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
                    pidl = SHBrowseForFolder(&browse);

                    if(pidl != NULL)
                    {
                        //Get the Path
                        if(!SHGetPathFromIDList(pidl, G_outputDir))
                        {
                            strcpy(G_outputDir,"C:\\mydownloads\\");
                            MessageBox(hWnd,"Invalid Directory Chosen","Error",MB_OK);
                        }

                        ////////////////////////////////////////////////////
                        // Now Free The PIDL                              //
                        //Get a Pointer to the Shell's imalloc interface  //
                        ////////////////////////////////////////////////////
                        if(SHGetMalloc(&imalloc) != E_FAIL)
                        {
                            imalloc->Free(pidl);
                            imalloc->Release();
                        }
                    }
                    
					SetDlgItemText(hWnd, IDC_OUTPUTDIR, G_outputDir);
                    break;
                }




                /* Query for available servers, get directory listings of all that reply */
                case IDC_QUERY:
                {
                    int rtnval,x;
                    char* ipaddrString=NULL,*ptrIpaddrString;
                    int numServers;

                    /***********************************************/
                    /* Wipe out existing treeview and file systems */
                    /***********************************************/
                    TreeView_DeleteAllItems(hTree);

                    /* Delete all filesystem listings */
                    for(x=0; x < G_totalTreeRootItems; x++)
                    {
                        removeFs(G_rootFSPtr[x]);
                    }
                    /* Free the Pointer to all remote filesystems */
                    if(G_rootFSPtr != NULL)
                        free(G_rootFSPtr);
                    G_rootFSPtr = NULL;
                    G_totalTreeRootItems = 0;

                    /* Discover Servers, 2 second timeout */
                    rtnval = sendServerDiscoveryRqst(querySocket, 2);
                    if(rtnval == 0)
                    {
                        MessageBox(hWnd,"No servers replied","Server Discovery Error",MB_OK);
                    }
                    else if(rtnval < 0)
                    {
                        MessageBox(hWnd,"No servers replied","Server Discovery Error",MB_OK);
                    }
                    else
                    {
                        /* Get directory listing from all discovered servers */
                        sprintf(errorString,"%d servers replied, getting server listings",rtnval);
                        MessageBox(hWnd,errorString,"Server Discovery Success",MB_OK);

                        G_lastnumservers = numServers = getServerIpAddresses(&ipaddrString);
                        if(numServers <= 0)
                        {
                            MessageBox(hWnd,"Error parsing through server ip addresses","Server Database Error",MB_OK);
                        }
                        else
                        {

                            /* Servers replied.  Get and display their directory listings */
                            ptrIpaddrString = ipaddrString;
                            G_totalTreeRootItems = 0;
                            G_rootFSPtr = (fsItem**)malloc(numServers*sizeof(fsItem*));
                            if(G_rootFSPtr == NULL)
                            {
                                MessageBox(hWnd,"Error allocating memory for file system display","Memory Alloc Error",MB_OK);
                                break;
                            }

                            for(x=0; x < numServers; x++)
                            {
                                G_rootFSPtr[x] = NULL;

                                if(getServerDirectoryListing(ptrIpaddrString) < 0)
                                {
                                    sprintf(errorString,"Error receiving server directory listing from %s",ptrIpaddrString);
                                    MessageBox(hWnd,errorString,"Server Directory Listing Error",MB_OK);
                                    G_totalTreeRootItems--;
                                }
                                else
                                {
                                    char tmp[50];
                                    /* Add to the virtual file system and user treeview GUI */
                                    sprintf(tmp,"%s_root",ptrIpaddrString);
                                    G_rootFSPtr[G_totalTreeRootItems] = (fsItem*)malloc(sizeof(fsItem));
                                    if(G_rootFSPtr[G_totalTreeRootItems] == NULL)
                                    {
                                        //Error mallocing
                                        continue;
                                    }
                                    G_rootFSPtr[G_totalTreeRootItems]->folder = TRUE;                      //Is the current item a folder?  True or false
                                    G_rootFSPtr[G_totalTreeRootItems]->ID = 0;                             //ID - A unique number to identify a folder
                                    G_rootFSPtr[G_totalTreeRootItems]->parentID = -1;                      //ID of the parent directory of the item
                                    G_rootFSPtr[G_totalTreeRootItems]->level = 0;                          //# of levels down from the root directory
                                    G_rootFSPtr[G_totalTreeRootItems]->fileSize = 0;                       //If the item is a file, the filesize in bytes
                                    G_rootFSPtr[G_totalTreeRootItems]->hashType = -1;                      //Type of file hash
                                    strcpy(G_rootFSPtr[G_totalTreeRootItems]->hashValue,"NONE");           //Value of the hash function (Hex String)
                                    strcpy(G_rootFSPtr[G_totalTreeRootItems]->name,tmp);                    //Name of the file or directory
                                    strcpy(G_rootFSPtr[G_totalTreeRootItems]->fullpathAndName,"root");     //Full path and name of the file or directory
                                    G_rootFSPtr[G_totalTreeRootItems]->currentlevelNextItem = NULL;        //Next Item assoc. with the current FS level
                                    G_rootFSPtr[G_totalTreeRootItems]->nextLevelDown = NULL;               //Pointer to the next item

                                    sprintf(tmp,".\\ServerCache\\%s_cached.dat",ptrIpaddrString);
                                    readTreeIntoBuffer(tmp, G_rootFSPtr[G_totalTreeRootItems]);
                                    insertTreeviewItem(G_rootFSPtr[G_totalTreeRootItems],NULL,hWnd,IDC_FILE_TREE);
                                    G_totalTreeRootItems++;
                                }
                                while(*ptrIpaddrString != '\0')
                                    *ptrIpaddrString++;
                                *ptrIpaddrString++;
                            }
                            
                            if(ipaddrString != NULL)
                                free(ipaddrString);
                        }
                    }
                    break;
                }

            }
			break;

			case WM_CLOSE: //Close the dialog
			{
                long prev;
                ReleaseSemaphore(G_h_KillProgram,1,&prev);
				EndDialog(hWnd,0); 
			}
			break;
		}
		break;
	}
	return 0;
}




/*****************************************************************************/
/* manageHybridDownloads - Handles all downloads.                            */
/* Returns:  0 on success, -1 on failure.                                    */
/*****************************************************************************/
DWORD WINAPI manageHybridDownloads(LPVOID lpParam)
{
    HANDLE hThread,outFile;
    DWORD dwThreadId;
    queueItem* nextQueuedItem;
    int x, numConnectionLeft, failedBind, unusedConnections;
    int maxSimultaneousDownloads = MAX_SIMULT_DOWNLOADS;
    int currentNumDownloads = 0;
    int allowedConnects;
    LARGE_INTEGER fileSize;
    unsigned short firstPort = 2000;

    while(1)
    {
		/* Determine when to start downloading the next item on the queue */
        if( (currentNumDownloads < MAX_SIMULT_DOWNLOADS) && 
			((G_TotalAllowedConnections-G_TotalUsedConnections) > 0) )
        {
            nextQueuedItem = getNextQueueItem();
            if(nextQueuedItem != NULL)
            {
				/* Determine how many simultaneous connections to run */
				/* Update global variables accordingly                */
				numConnectionLeft = G_TotalAllowedConnections - G_TotalUsedConnections;
				if(numConnectionLeft >= DESIRED_CONNECT_PER_DL)
					nextQueuedItem->numConnectionsToUse = DESIRED_CONNECT_PER_DL;
				else
					nextQueuedItem->numConnectionsToUse = numConnectionLeft;


                /* Create an array of sockets and initialize */
                nextQueuedItem->sockArray = (SOCKET*)malloc(nextQueuedItem->numConnectionsToUse*sizeof(SOCKET));
                if(nextQueuedItem->sockArray == NULL)
                {
                    MessageBox(NULL, "Error in allocating socket memory.", "OS Error", MB_OK);
                    continue;
                }
                for(x = 0; x < nextQueuedItem->numConnectionsToUse; x++)
                {
                    nextQueuedItem->sockArray[x] = INVALID_SOCKET;
                }


                /* Bind the sockets on sequential ports */
                failedBind = FALSE;
                while(1)
                {
                    for(x = 0; x < nextQueuedItem->numConnectionsToUse; x++)
                    {
                        /*Fix wraparound case */
                        if(firstPort < 2000)
                        {
                            firstPort = 2000;
                            failedBind = TRUE;
                            break;
                        }

                        if(x == 0)
                        {
                            nextQueuedItem->firstPort = firstPort;
                        }

                        nextQueuedItem->sockArray[x] = udpOpenBindSocket(firstPort);
                        if(nextQueuedItem->sockArray[x] == INVALID_SOCKET)
                        {
                            failedBind = TRUE;
							break;
                        }
                        firstPort++;
                    }

                    if(failedBind)
                    {
						firstPort += 10;
                        for(x = 0; x < nextQueuedItem->numConnectionsToUse; x++)
                        {
                            closesocket(nextQueuedItem->sockArray[x]);
                        }
						failedBind = FALSE;
                    }
                    else
                        break;
                }


	            /* Attempt to initiate a "connection" with the remote server */
                if( initiateConnection(nextQueuedItem->sockArray[0], nextQueuedItem->ipaddress,
                    5, nextQueuedItem->numConnectionsToUse, &allowedConnects) < 0)
	            {
                    MessageBox(NULL, "Error initiating connection with server, downloading FAILED.", "Connect Error", MB_OK);
                    for(x = 0; x < nextQueuedItem->numConnectionsToUse; x++)
                    {
                        closesocket(nextQueuedItem->sockArray[x]);
                    }
                    nextQueuedItem->status = SERVER_REFUSED;
                    updateLVItem(nextQueuedItem);
                    continue;
	            }

                /* If the server is going to limit us from having the max# of connections that we want */
                /* then free up the extra connetions so that another download process can use them.    */
                unusedConnections = nextQueuedItem->numConnectionsToUse - allowedConnects;
                if(unusedConnections > 0)
                {
                    for( x = 0; x < unusedConnections; x++)
                    {
                        closesocket(nextQueuedItem->sockArray[nextQueuedItem->numConnectionsToUse-1-x]);
                    } 
                    nextQueuedItem->numConnectionsToUse = allowedConnects;
                }

                /* Update the # of used connections */
                G_TotalUsedConnections += nextQueuedItem->numConnectionsToUse;

                /* Update the status of the queued item */
                nextQueuedItem->fileOffset = 0;
                if(nextQueuedItem->status == QUEUED)
                {
                    nextQueuedItem->status = DOWNLOADING;
                }
                else if( (nextQueuedItem->status == FAIL_PARTIAL_DL) ||
                    (nextQueuedItem->status == SERVER_REFUSED) )
                {
                    /* Get the size of the downloaded file */
                    /* Determine # remaining bytes         */
                    outFile = CreateFile(
		                nextQueuedItem->dLItem.localfullpathAndName,    /* lpFileName      */
                        GENERIC_READ,             /* dwDesiredAccess */
                        FILE_SHARE_READ,          /* dwShareMode */
                        NULL,                     /* lpSecurityAttributes */
                        OPEN_EXISTING,            /* dwCreationDisposition */
                        FILE_FLAG_SEQUENTIAL_SCAN,  /* dwFlagsAndAttributes */
                        NULL                     /* hTemplateFile */
                    );
                    if(outFile == INVALID_HANDLE_VALUE)
                    {
                        /* Redownload, dont resume */
                        nextQueuedItem->bytesRemain = nextQueuedItem->dLItem.fileSize;
                        nextQueuedItem->status = RETRYING;
                    }
                    else
                    {
                        /* Get the filesize, should be a multiple of 65464 */
                        fileSize.QuadPart = 0;
                        GetFileSizeEx(outFile,&fileSize);
                        if( (fileSize.QuadPart > 65464) && ((fileSize.QuadPart % 65464) == 0) )
                        {
                            nextQueuedItem->fileOffset = fileSize.QuadPart;
                            nextQueuedItem->bytesRemain = nextQueuedItem->dLItem.fileSize - nextQueuedItem->fileOffset;
                            nextQueuedItem->status = RESUMING;
                            updateLVItem(nextQueuedItem);
                        }
                        else
                        {
                            /* Redownload, dont resume */
                            nextQueuedItem->bytesRemain = nextQueuedItem->dLItem.fileSize;
                            nextQueuedItem->status = RETRYING;
                        }
                    }    
                    CloseHandle(outFile);
                }
                else
                {
					nextQueuedItem->bytesRemain = nextQueuedItem->dLItem.fileSize;
                    nextQueuedItem->status = RETRYING;
                }

                /* Begin Download */
                hThread = CreateThread( 
		            NULL,              // default security attributes
		            0,                 // use default stack size  
		            beginHybridDownload,    // thread function 
		            (void*)nextQueuedItem,       // argument to thread function 
		            0,                 // use default creation flags 
		            &dwThreadId);	   // returns the thread identifier 
	            if (hThread == NULL)
                {
                    reportError("Error in CreateThread, Downloading FAILED.",0,0);
		            MessageBox(NULL, "Error in CreateThread, Downloading FAILED.", "OS Error", MB_OK);
                    for(x = 0; x < nextQueuedItem->numConnectionsToUse; x++)
                    {
                        closesocket(nextQueuedItem->sockArray[x]);
                    }
                    G_TotalUsedConnections -= nextQueuedItem->numConnectionsToUse;
                    nextQueuedItem->status = DL_FAILED;
                    updateLVItem(nextQueuedItem);
                    continue;
                }
            }
        }

        /* Delay 1 second to allow other threads time */
        Sleep(1000);

        /* Break out if quitting the program */
        if( WaitForSingleObject(G_h_KillProgram,0) == WAIT_OBJECT_0 )
        {
            break;
        }
    }

    ExitThread(0);
    return 0;
}
