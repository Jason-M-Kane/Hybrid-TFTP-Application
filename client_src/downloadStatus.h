/*****************************************************************************/
/* downloadStatus.h - Definitions for download status.                       */
/*****************************************************************************/
#ifndef DOWNLOAD_STATUS_H
#define DOWNLOAD_STATUS_H


/* Download Status Definitions */
#define QUEUED            0
#define DL_COMPLETE       1
#define DOWNLOADING       2
#define DL_FAILED         3
#define RETRYING          4
#define SERVER_REFUSED    5
#define HASH_FAILED       6
#define CHECKING_MD5_HASH 7
#define FAIL_PARTIAL_DL   8
#define RESUMING          9
#define WAIT_WRITES       10


#endif
