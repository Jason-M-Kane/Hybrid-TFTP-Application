/*****************************************************************************/
/* serverDirListingRoutines.h - Routines involved in the storage and display */
/*                              of file systems on remote servers.           */
/*****************************************************************************/
#ifndef serverDIRListingRoutines_H
#define serverDIRListingRoutines_H


/************/
/* Includes */
/************/
#include <windows.h>
#include <commctrl.h>
#include "fileSystemType.h"


/***********************/
/* Function Prototypes */
/***********************/
int readTreeIntoBuffer(char* file, fsItem* root);
int propagateCheckState(HWND hwndTV, fsItem* root,int newCheckstate);
int insertTreeviewItem(fsItem* root, HTREEITEM Parent, HWND hWnd, int tvID);
fsItem* insertFSNode(fsItem* root, fsItem* newFSItem, int searchId);
void removeFs(fsItem* root);



#endif
