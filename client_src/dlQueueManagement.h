/*****************************************************************************/
/* dlQueueManagement.h - Routines involved in the display of queued download */
/*                       items.                                              */
/*****************************************************************************/
#ifndef DL_QUEUE_MANAGEMENT_H
#define DL_QUEUE_MANAGEMENT_H


/************/
/* Includes */
/************/
#include <windows.h>
#include <commctrl.h>
#include "fileSystemType.h"


/***********************/
/* Function Prototypes */
/***********************/
int createDownloadQueue(fsItem** rootFS, int numServers);
void destroyDownloadQueue(queueItem* root);
int Populate_Listview(HWND hWnd);
void updateLVItem(queueItem* selectedQueueItem);
queueItem* getNextQueueItem(void);


#endif
