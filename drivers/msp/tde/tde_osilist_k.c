/*****************************************************************************
*             Copyright 2006 - 2050, Hisilicon Tech. Co., Ltd.
*                           ALL RIGHTS RESERVED
* FileName: tde_osilist.h
* Description:TDE osi list interface define
*
* History:
* Version   Date          Author        DefectNum       Description
*
*****************************************************************************/
#ifdef __cplusplus
 #if __cplusplus
extern "C" {
 #endif /* __cplusplus */
#endif  /* __cplusplus */

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include "tde_define.h"
#include "tde_handle.h"
#include "tde_buffer.h"
#include "tde_osilist.h"
#include "tde_osr.h"
#include "tde_hal.h"
#include "wmalloc.h"


/* JOB LIST head node definition */
typedef struct hiTDE_SWJOBLIST_S
{
    struct list_head stList; //0
    HI_S32 s32HandleLast;                   /* job handle wait for last submit */
    HI_S32 s32HandleFinished;               /* job handle last completed */
    HI_U32 u32JobNum; //16                      /* job number in queue */
	#ifndef TDE_BOOT
	spinlock_t lock;
	#endif
    TDE_SWJOB_S *pstJobCommitted;           /* last submited job node pointer, which is the first job */
    TDE_SWJOB_S *pstJobToCommit;            /* job node pointer wait for submit,which is the first job */
    HI_VOID *pstJobFinished; //32
    TDE_SWJOB_S *pstJobLast; //36               /* last job in the list */
    //40
} TDE_SWJOBLIST_S;


typedef HI_VOID (* TDE_WQ_CB) (HI_U32);

typedef struct
{
    HI_U32 Count;
    TDE_WQ_CB pWQCB;
    struct work_struct work;
}TDEFREEWQ_S;


typedef struct hiTDE_WAITALLDONE_S
{
    wait_queue_head_t stTdeWaitAllWq;
    HI_BOOL bBegWait;
    HI_BOOL bNodeComplete;
}TDE_WAITALLDONE_S;

/****************************************************************************/
/*                   TDE osi list inner variable definition                 */
/****************************************************************************/
STATIC TDE_SWJOBLIST_S *s_pstTDEOsiJobList; //[TDE_LIST_BUTT] = {NULL}; /* global job list queue */
//STATIC TDE_SWNODELIST_S *s_pstTDEOsiCmdList[TDE_LIST_BUTT] = {NULL}; /* global CMD list queue */
STATIC wait_queue_head_t s_TdeBlockJobWq;     /* wait queue used to block */
STATIC TDE_WAITALLDONE_S s_stTdeWaitAll;
//STATIC TDE_SWNODE_S *s_pstParentSwNode = HI_NULL; /* save parent node address */
STATIC wait_queue_head_t s_TdeBeginJobWq;   /* block queue when task number achieve the limit */


STATIC HI_DECLARE_MUTEX(g_TDEJobSubLock);  /* ensure command submit is mutex lock in order */
STATIC HI_DECLARE_MUTEX(g_TDEHdMgrLock);
STATIC HI_DECLARE_MUTEX(g_TDEWaitAllLock);


/****************************************************************************/
/*               TDE osi list inner interface definition                    */
/****************************************************************************/
STATIC HI_VOID     TdeOsiListDoFreePhyBuff(HI_U32 u32BuffNum);
STATIC INLINE HI_VOID TdeOsiListSafeDestroyJob(TDE_SWJOB_S *pstJob);
STATIC HI_VOID     TdeOsiListReleaseHandle(HI_HANDLE_MGR *pstJobHeader);
//STATIC HI_VOID     TDEOsiListSubCmd(TDE_LIST_TYPE_E enListType);
STATIC INLINE HI_VOID TdeOsiListAddJob(TDE_SWJOB_S *pstJob/*, TDE_LIST_TYPE_E enListType*/);
STATIC HI_VOID     TdeOsiListDestroyJob(TDE_SWJOB_S *pstJob);
//STATIC HI_VOID     TDEOsiListSubCmd(TDE_LIST_TYPE_E enListType);
STATIC INLINE HI_VOID list_join(struct list_head *list, struct list_head *head);


/*****************************************************************************
* Function:      TdeOsiListSafeDestroyJob
* Description:   release node from FstCmd to LastCmd
* Input:         pstJob:delete job list
* Output:        none
* Return:        none
* Others:
*****************************************************************************/
STATIC INLINE HI_VOID TdeOsiListSafeDestroyJob(TDE_SWJOB_S *pstJob)
{
    /* if user query this job, release job in query function */
    if(pstJob->bInQuery)
    {
        TDE_TRACE(TDE_KERN_DEBUG, "query handle %d complete!\n", pstJob->s32Handle); //96
        pstJob->enNotiType = TDE_JOB_NOTIFY_BUTT;
        wake_up_interruptible(&pstJob->stQuery);
    }
    else
    {
        TdeOsiListDestroyJob(pstJob);
    }
}

/*****************************************************************************
* Function:      TdeOsiListReleaseHandle
* Description:   release handle manage info
* Input:         pstJobHeader:handle manage struct
* Output:        none
* Return:        none
* Others:
*****************************************************************************/
STATIC HI_VOID  TdeOsiListReleaseHandle(HI_HANDLE_MGR *pstJobHeader)
{
    TDE_ASSERT(NULL != pstJobHeader); //117
    if (pstJobHeader != NULL)
    {
        if (release_handle(pstJobHeader->handle))
        {
            if(get_handlenum() != TDE_MAX_JOB_NUM)//!TDE_IS_JOB_FULL())
            {
                wake_up_interruptible(&s_TdeBeginJobWq);
            }

            TDE_FREE(pstJobHeader);
        }
    }
}

/*****************************************************************************
* Function:      TdeOsiListDoFreePhyBuff
* Description:    free temporary buffer 
* Input:         u32BuffNum: the number of temporary buffer
* Output:        none
* Return:        none
* Others:
*****************************************************************************/
STATIC HI_VOID TdeOsiListDoFreePhyBuff(HI_U32 u32BuffNum)
{
    HI_U32 i;

    for(i = 0; i < u32BuffNum; i++)
    {
        TDE_FreePhysicBuff();
    }
}

/*****************************************************************************
 Prototype    : TdeOsiListInit
 Description  : initialize list manage  module
 Input        : HI_VOID
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_S32 TdeOsiListInit(HI_VOID)
{
#ifndef TDE_BOOT
    init_waitqueue_head(&s_TdeBlockJobWq);
#endif
#if 0
    init_waitqueue_head(&s_stTdeWaitAll.stTdeWaitAllWq);
    s_stTdeWaitAll.bBegWait = HI_FALSE;
    s_stTdeWaitAll.bNodeComplete = HI_FALSE;
#endif
    init_waitqueue_head(&s_TdeBeginJobWq);
    
    if (!initial_handle())
    {
        return HI_FAILURE;
    }

    s_pstTDEOsiJobList/*[TDE_LIST_AQ]*/ = (TDE_SWJOBLIST_S *) TDE_MALLOC(sizeof(TDE_SWJOBLIST_S)/* * TDE_LIST_BUTT*/);
    if (NULL == s_pstTDEOsiJobList/*[TDE_LIST_AQ]*/)
    {        
        destroy_handle();
        return HI_FAILURE;
    }

    //memset(s_pstTDEOsiJobList/*[TDE_LIST_AQ]*/, 0, sizeof(TDE_SWJOBLIST_S) * TDE_LIST_BUTT);
#if HI_TDE_SQ_SUPPORT
    s_pstTDEOsiJobList[TDE_LIST_SQ] = s_pstTDEOsiJobList/*[TDE_LIST_AQ]*/ + 1;
#endif
    
    INIT_LIST_HEAD(&s_pstTDEOsiJobList/*[TDE_LIST_AQ]*/->stList);
#if HI_TDE_SQ_SUPPORT
    INIT_LIST_HEAD(&s_pstTDEOsiJobList[TDE_LIST_SQ]->stList);
#endif

#ifndef TDE_BOOT
    spin_lock_init(&s_pstTDEOsiJobList->lock);
    spin_lock_init(&s_TDEBuffLock);
#endif
    return HI_SUCCESS;
}

/*****************************************************************************
 Prototype    : TdeOsiListFreeNode
 Description  : release node 
 Input        : pNode: node pointer
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
STATIC INLINE HI_VOID TdeOsiListFreeNode(TDE_SWNODE_S *pNode)
{
    TDE_ASSERT(NULL != pNode); //209
    list_del_init(&pNode->stList);
    if (NULL != pNode->stNodeBuf.pBuf)
    {
        TDE_FREE(pNode->stNodeBuf.pBuf);
    }

    TdeOsiListPutPhyBuff(pNode->u32PhyBuffNum);

    TDE_FREE(pNode);
}

/*****************************************************************************
* Function:      TdeOsiListFreeSerialCmd
* Description:   release from FstCmd to LastCmd
* Input:         pstFstCmd: first node
*                pstLastCmd:last node
* Output:        none
* Return:        none
* Others:
*****************************************************************************/
HI_VOID TdeOsiListFreeSerialCmd(TDE_SWNODE_S *pstFstCmd, TDE_SWNODE_S *pstLastCmd)
{
    TDE_SWNODE_S *pstNextCmd;
    TDE_SWNODE_S *pstCurCmd;
    
    if((NULL == pstFstCmd) || (NULL == pstLastCmd))
    {
        return ;
    }

    pstCurCmd = pstNextCmd = pstFstCmd;
    while (pstNextCmd != pstLastCmd)
    {
        pstNextCmd = list_entry(pstCurCmd->stList.next, TDE_SWNODE_S, stList);
        TdeOsiListFreeNode(pstCurCmd);
        pstCurCmd = pstNextCmd;
    }

    TdeOsiListFreeNode(pstLastCmd);
}

/*****************************************************************************
 Prototype    : TdeOsiListTerm
 Description  : deinitialization of list manager module
 Input        : HI_VOID
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_VOID TdeOsiListTerm(HI_VOID)
{
    //HI_S32 i;
    TDE_SWJOB_S *pstJob;

    //TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
    //tde_osr_disableirq();
    
    //for (i = 0; i < TDE_LIST_BUTT; i++)
    {
        
        while (!list_empty(&s_pstTDEOsiJobList/*[i]*/->stList))
        {
            pstJob = list_entry(s_pstTDEOsiJobList/*[i]*/->stList.next, TDE_SWJOB_S, stList);
            list_del_init(&pstJob->stList);
            TdeOsiListDestroyJob(pstJob);
        }
    }
    
    TDE_FREE(s_pstTDEOsiJobList/*[TDE_LIST_AQ]*/);
    s_pstTDEOsiJobList/*[TDE_LIST_AQ]*/ = NULL;
#if HI_TDE_SQ_SUPPORT
    s_pstTDEOsiJobList[TDE_LIST_SQ] = NULL;
#endif    
   
    destroy_handle();
    //tde_osr_enableirq();
    //TDE_UP(&g_TDEHdMgrLock);
    
    return;
}

#ifndef TDE_BOOT

#define GFX_MMZ_BUFFER_S     MMZ_BUFFER_S
static inline HI_VOID HI_GFX_Flush(GFX_MMZ_BUFFER_S *stFlushBuf)
{
    HI_DRV_MMZ_Flush(stFlushBuf);
}

HI_VOID TdeOsiListFlushNode(TDE_SWNODE_S *pNode)
{
	TDE_NODE_BUF_S *pNodeBufInfo = NULL;
	HI_U32 CurNodePhy = 0; /*当前节点对应的物理地址*/
	GFX_MMZ_BUFFER_S stFlushBuf = {0};
	TDE_ASSERT(NULL != pNode); //293

	pNodeBufInfo = &pNode->stNodeBuf;

	if (NULL != pNodeBufInfo->pBuf)
	{
	    CurNodePhy = wgetphy(pNodeBufInfo->pBuf);
		stFlushBuf.u32Size = pNodeBufInfo->u32NodeSz+TDE_NODE_HEAD_BYTE+TDE_NODE_TAIL_BYTE;
		stFlushBuf.u32StartPhyAddr = CurNodePhy;
		stFlushBuf.u32StartVirAddr = (HI_U32)pNodeBufInfo->pBuf;
		HI_GFX_Flush(&stFlushBuf);
	}
	return;
}


HI_VOID TdeOsiListFlushJob(TDE_SWJOB_S *pstJob)
{
    TDE_SWNODE_S *pstNextCmd = NULL;
    TDE_SWNODE_S *pstCurCmd = NULL;
    TDE_SWNODE_S *pstFstCmd = NULL;
    TDE_SWNODE_S *pstLastCmd = NULL;
    pstFstCmd = pstJob->pstFirstCmd;
    pstLastCmd =  pstJob->pstTailNode;

    TDE_ASSERT(NULL != pstJob); //315

    if((NULL == pstFstCmd) || (NULL == pstLastCmd))
    {
        return ;
    }

    pstCurCmd = pstNextCmd = pstFstCmd;
    while (pstNextCmd != pstLastCmd)
    {
        pstNextCmd = list_entry(pstCurCmd->stList.next, TDE_SWNODE_S, stList);
        TdeOsiListFlushNode(pstCurCmd);
        pstCurCmd = pstNextCmd;
    }

    TdeOsiListFlushNode(pstLastCmd);
    return;
}
#endif

/*****************************************************************************
* Function:      TdeOsiListAddJob
* Description:   add task info to task list
* Input:         pstJob: job struct
*                enListType: submit type:Sq/Aq
* Output:        none
* Return:        none
* Others:
*****************************************************************************/
STATIC INLINE HI_VOID TdeOsiListAddJob(TDE_SWJOB_S *pstJob/*, TDE_LIST_TYPE_E enListType*/)
{
    list_add_tail(&pstJob->stList, &s_pstTDEOsiJobList/*[enListType]*/->stList);
    s_pstTDEOsiJobList/*[enListType]*/->u32JobNum++;
    s_pstTDEOsiJobList/*[enListType]*/->s32HandleLast = pstJob->s32Handle;
    s_pstTDEOsiJobList/*[enListType]*/->pstJobLast = pstJob;
}

/*****************************************************************************
 Prototype    : TdeOsiListBeginJob
 Description  : create a job
 Input        : NONE
 Output       : pHandle: created job handle
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_S32 TdeOsiListBeginJob(TDE_HANDLE *pHandle)
{
    HI_HANDLE_MGR *pHandleMgr;
    TDE_SWJOB_S *pstJob;
    HI_S32 s32Ret = HI_SUCCESS;
    
    //tde_osr_disableirq();
    pHandleMgr = (HI_HANDLE_MGR *) TDE_MALLOC(sizeof(HI_HANDLE_MGR) + sizeof(TDE_SWJOB_S));
    if (NULL == pHandleMgr)
    {
        //tde_osr_enableirq();
        TDE_TRACE(TDE_KERN_INFO, "TDE BegJob Malloc Fail!\n"); //376
        return HI_ERR_TDE_NO_MEM;
    }

    //memset(pHandleMgr, 0, sizeof(HI_HANDLE_MGR) + sizeof(TDE_SWJOB_S));

    TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
    
    while (get_handle(pHandleMgr, pHandle) < 0)
    {
    	HI_S32 s_s32HandleNum;

        TDE_TRACE(TDE_KERN_INFO, "Too Many TDE Jobs!\n"); //384
        TDE_UP(&g_TDEHdMgrLock);
        //tde_osr_enableirq();
        
        s_s32HandleNum = get_handlenum();

		s32Ret = wait_event_interruptible(s_TdeBeginJobWq, s_s32HandleNum < TDE_MAX_JOB_NUM);
		if ((-ERESTARTSYS) == s32Ret)
		{
			return HI_ERR_TDE_INTERRUPT;
		}

		TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
		//tde_osr_disableirq();
    }

    TDE_UP(&g_TDEHdMgrLock);
    //tde_osr_enableirq();
    pstJob = (TDE_SWJOB_S *)((HI_U8*)pHandleMgr + sizeof(HI_HANDLE_MGR));
    pHandleMgr->res = (HI_VOID*)pstJob;
	#ifndef TDE_BOOT
    INIT_LIST_HEAD(&pstJob->stList);
    init_waitqueue_head(&pstJob->stQuery);
    #endif
    pstJob->s32Handle = *pHandle;
    //pstJob->pid = current->tgid;
    return HI_SUCCESS;
}

/*****************************************************************************
 Prototype    : TdeOsiListDestroyJob
 Description  : destroy a job
 Input        : s32Handle: job handle
 Output       : NONE
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
STATIC HI_VOID TdeOsiListDestroyJob(TDE_SWJOB_S *pstJob)
{
    HI_HANDLE_MGR *pHandleMgr;
    if (!query_handle(pstJob->s32Handle, &pHandleMgr))
    {
        TDE_TRACE(TDE_KERN_DEBUG, "handle %d does not exist!\n", pstJob->s32Handle); //431
        return;
    }
    TdeOsiListFreeSerialCmd(pstJob->pstFirstCmd, pstJob->pstTailNode);
    TdeOsiListReleaseHandle(pHandleMgr);

    return;
}

/*****************************************************************************
* Function:      TdeOsiListCancelJob
* Description:   cancel task
* Input:         s32Handle:task handle
* Output:        none
* Return:        =0,success <0,error
* Others:
*****************************************************************************/
#ifndef TDE_BOOT
HI_S32 TdeOsiListCancelJob(TDE_HANDLE s32Handle)
{
    HI_HANDLE_MGR *pHandleMgr;
    TDE_SWJOB_S *pstJob;
	#ifndef TDE_BOOT
	//HI_SIZE_T lockflags;
	#endif
	//TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    if (!query_handle(s32Handle, &pHandleMgr))
    {
        //TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
        return HI_SUCCESS;
    }
    pstJob = (TDE_SWJOB_S *)pHandleMgr->res;

    if(pstJob->bSubmitted)
    {
        //TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
        TDE_TRACE(TDE_KERN_INFO, "Handle %d has been submitted!\n", s32Handle); //462
        return HI_FAILURE;
    }

    TdeOsiListFreeSerialCmd(pstJob->pstFirstCmd, pstJob->pstTailNode);
    TdeOsiListReleaseHandle(pHandleMgr);
    //TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
    return 0;
}
#else
/*****************************************************************************
* Function:      TdeOsiListWaitTdeIdle
* Description:   wait for completion of the task
* Input:         ?T
* Output:        ?T
* Return:        True: Idle/False: Busy
* Others:        ?T
*****************************************************************************/
HI_VOID  static TdeOsiListWaitTdeIdle(HI_VOID)
{

    while(1)
    {
        /* query the status interval of 10ms*/
        udelay(10*1000);

        if (TdeHalCtlIsIdleSafely())
        {
            return;
        }
    }
}
#endif

#if 0
/*****************************************************************************
 Prototype    : TdeOsiListAddNode
 Description  : add a command to assigned job
 Input        : s32Handle: job handle
                pSwNode: node resource
                enSubmType: node type
 Output       : NONE
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_S32 TdeOsiListAddNode(TDE_HANDLE s32Handle, TDE_NODE_BUF_S * pSwNode,
                         TDE_NODE_SUBM_TYPE_E enSubmType, HI_U32 u32PhyBuffNumInNode)
{
    HI_HANDLE_MGR *pHandleMgr;
    TDE_SWJOB_S *pstJob;
    TDE_SWNODE_S *pstCmd;
    HI_BOOL bValid;

    TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
    tde_osr_disableirq();
    bValid = query_handle(s32Handle, &pHandleMgr);
    tde_osr_enableirq();

    if (!bValid)
    {
        TDE_TRACE(TDE_KERN_INFO, "invalid handle %d!\n", s32Handle);
        TDE_UP(&g_TDEHdMgrLock);
        return HI_ERR_TDE_INVALID_HANDLE;
    }
    
    pstJob = (TDE_SWJOB_S*)pHandleMgr->res;
    if(pstJob->bSubmitted)
    {
        TDE_TRACE(TDE_KERN_INFO, "job %d already submitted!\n", s32Handle);
        TDE_UP(&g_TDEHdMgrLock);
        return HI_ERR_TDE_INVALID_HANDLE;
    }

    tde_osr_disableirq();
    pstCmd = (TDE_SWNODE_S *) TDE_MALLOC(sizeof(TDE_SWNODE_S));
    tde_osr_enableirq();
    if (NULL == pstCmd)
    {
        TDE_TRACE(TDE_KERN_INFO, "malloc failed!\n");
        TDE_UP(&g_TDEHdMgrLock);
        return HI_ERR_TDE_NO_MEM;
    }

    if (enSubmType != TDE_NODE_SUBM_CHILD)
    {
        pstJob->u32CmdNum++;
        
        if (1 == pstJob->u32CmdNum)
        {
            pstJob->pstFirstCmd = pstCmd;
           
            INIT_LIST_HEAD(&pstCmd->stList);
        }

        pstJob->pstLastCmd = pstCmd;        
    }

    switch(enSubmType)
    {
        case TDE_NODE_SUBM_PARENT:
            pstJob->pstParentCmd = pstCmd;
            pstCmd->pstParentNodeInCmd = HI_NULL;
            break;
        case TDE_NODE_SUBM_CHILD:
            pstCmd->pstParentNodeInCmd = pstJob->pstParentCmd;
            break;
        case TDE_NODE_SUBM_ALONE:
            pstCmd->pstParentNodeInCmd = HI_NULL;
            break;
    }

    if (HI_NULL != pstJob->pstTailNode)
    {
       HI_U32 *pNextNodeAddr = (HI_U32 *)pstJob->pstTailNode->stNodeBuf.pBuf + (TDE_NODE_HEAD_BYTE >> 2) + ((pstJob->pstTailNode->stNodeBuf.u32NodeSz) >> 2);
       HI_U32 *pNextNodeUpdate = pNextNodeAddr + 1;

       *pNextNodeAddr = pSwNode->u32PhyAddr;

#ifdef TDE_VERSION_MPW
       *pNextNodeUpdate = pSwNode->u64Update & 0xffffffff;
#else
       *pNextNodeUpdate++ = pSwNode->u64Update & 0xffffffff;
       *pNextNodeUpdate = (pSwNode->u64Update >> 32) & 0xffffffff;
#endif
    }
   
    memcpy(&pstCmd->stNodeBuf, pSwNode, sizeof(*pSwNode));
    pstCmd->enNotiType = TDE_JOB_NONE_NOTIFY;
    pstCmd->s32Handle = pstJob->s32Handle;
    pstCmd->s32Index   = pstJob->u32CmdNum;
    pstCmd->enSubmType = enSubmType;
    pstCmd->u32PhyBuffNum = u32PhyBuffNumInNode;
   
    //*(HI_U32 *)pstCmd->stNodeBuf.pBuf = (HI_U32)pstCmd;
    *(((HI_U32 *)pstCmd->stNodeBuf.pBuf) + 1) = s32Handle;
    
    
    list_add_tail(&pstCmd->stList, &pstJob->pstFirstCmd->stList);
    pstJob->pstTailNode = pstCmd;
    pstJob->u32NodeNum++;

    if (pstCmd->u32PhyBuffNum != 0)
    {
        pstJob->bAqUseBuff = HI_TRUE;
    }
    
    TDE_UP(&g_TDEHdMgrLock);

    return HI_SUCCESS;
}
#endif

/*****************************************************************************
 Prototype    : list_join
 Description  : add list to the tail of head, list has no head, head has head
 Input        : list: wait for add to list head
                head: target list head
 Output       : NONE
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
STATIC INLINE HI_VOID list_join(struct list_head *list, struct list_head *head)
{
    if(NULL != list)
    {
        struct list_head *last = list->prev;
        struct list_head *at = head->prev;

        list->prev = at;
        at->next = list;

        last->next = head;
        head->prev = last;
    }
}

/*****************************************************************************
 Prototype    : TdeOsiListSubmitJob
 Description  : when submit job handle by user, at first add job list to global list, and then handle with according by different situation
                1.when TDE is free and no command canbe added,evaluate waited node pointer,start to software list node to hardware
                2.when TDE is not free and no command canbe added, evaluate waited node pointer
                3.when TDE is not free but command canbe added,no handle
 Input        : s32Handle: job handle
                pSwNode: node resource
                enSubmType: node type
 Output       : NONE
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_S32 TdeOsiListSubmitJob(TDE_HANDLE s32Handle/*, TDE_LIST_TYPE_E enListType*/,
                           HI_U32 u32TimeOut, TDE_FUNC_CB pFuncComplCB, HI_VOID *pFuncPara,
                           TDE_NOTIFY_MODE_E enNotiType)
{
    TDE_SWJOB_S * pstJob;
    HI_HANDLE_MGR *pHandleMgr;
    HI_BOOL bValid;
    TDE_SWNODE_S *pstTailNode = NULL;
    HI_S32 s32Ret;
    HI_U8 *pBuf;
	#ifndef TDE_BOOT
	HI_SIZE_T lockflags;
	#endif
	HI_BOOL asynflag = 0;
    TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
//    tde_osr_disableirq();
    bValid = query_handle(s32Handle, &pHandleMgr);
//    tde_osr_enableirq();
    
    if (!bValid)
    {
        TDE_TRACE(TDE_KERN_INFO, "invalid handle %d!\n", s32Handle); //533
        TDE_UP(&g_TDEHdMgrLock);
        return HI_ERR_TDE_INVALID_HANDLE;
    }

    pstJob = (TDE_SWJOB_S *)pHandleMgr->res;
    if(pstJob->bSubmitted)
    {
        TDE_TRACE(TDE_KERN_INFO, "job %d already submitted!\n", s32Handle); //539
        TDE_UP(&g_TDEHdMgrLock);
        return HI_ERR_TDE_INVALID_HANDLE;
    }
    
    if(NULL == pstJob->pstFirstCmd)
    {
        TDE_TRACE(TDE_KERN_INFO, "no cmd !\n"); //545
       
        //tde_osr_disableirq();
        TdeOsiListReleaseHandle(pHandleMgr);
        //tde_osr_enableirq();
        TDE_UP(&g_TDEHdMgrLock);
        return HI_SUCCESS;
    }

    pstTailNode = pstJob->pstTailNode;
    pstTailNode->enNotiType = enNotiType;

    pBuf = (HI_U8 *)pstTailNode->stNodeBuf.pBuf + TDE_NODE_HEAD_BYTE;
    
    TdeHalNodeEnableCompleteInt((HI_VOID *)pBuf/*, enListType*/);

    pstJob->bSubmitted = HI_TRUE;
    pstJob->enNotiType   = enNotiType;
    pstJob->pFuncComplCB = pFuncComplCB;
    pstJob->pFuncPara = pFuncPara;
    TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    /*If the job to commit is not null,join the current job to the tail node of the last job.*/
    if (HI_NULL != s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit)
    {
        TDE_SWNODE_S *pstTailNodeInJobList = s_pstTDEOsiJobList/*[enListType]*/->pstJobLast->pstTailNode;
        HI_U32 *pNextNodeAddr = (HI_U32 *)pstTailNodeInJobList->stNodeBuf.pBuf + (TDE_NODE_HEAD_BYTE >> 2) + ((pstTailNodeInJobList->stNodeBuf.u32NodeSz) >> 2);
        //HI_U32 *pNextNodeUpdate = pNextNodeAddr + 1;
        HI_U64 *pNextNodeUpdate = (HI_U64 *)(pNextNodeAddr + 1);

        *pNextNodeAddr = pstJob->pstFirstCmd->stNodeBuf.u32PhyAddr;
#if 0
#ifdef TDE_VERSION_MPW
        *pNextNodeUpdate = (HI_U32)(pstJob->pstFirstCmd->stNodeBuf.u64Update & 0xffffffff);
#else
        *pNextNodeUpdate = (HI_U32)(pstJob->pstFirstCmd->stNodeBuf.u64Update & 0xffffffff);
        *(pNextNodeUpdate + 1) = (HI_U32)((pstJob->pstFirstCmd->stNodeBuf.u64Update >> 32) & 0xffffffff);        
#endif
#else
        *pNextNodeUpdate = pstJob->pstFirstCmd->stNodeBuf.u64Update;
#endif

        if (pstJob->bAqUseBuff)
        {
            s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit->bAqUseBuff = HI_TRUE;
        }
		#ifdef TDE_CACH_STRATEGY
		/*将job中的对应的所有 hw 节点flush到内存，保证硬件能正确访问*/
        TdeOsiListFlushJob(s_pstTDEOsiJobList/*[enListType]*/->pstJobLast);
		#endif
    }
    else
    {
        s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit = pstJob;
    }

	#ifdef TDE_CACH_STRATEGY
	/*将job中的对应的所有 hw 节点flush到内存，保证硬件能正确访问*/
    TdeOsiListFlushJob(pstJob);
	#endif
    TdeOsiListAddJob(pstJob/*, enListType*/);

    if(TDE_JOB_WAKE_NOTIFY != enNotiType)
    {
        if(wgetfreenum() < 5)
        {
            pstJob->enNotiType = TDE_JOB_WAKE_NOTIFY;
            enNotiType = TDE_JOB_WAKE_NOTIFY;
            u32TimeOut = 1000;
            asynflag = 1;/*由非阻塞方式转为阻塞方式标志位*/

        }
    }

    s32Ret = TdeHalNodeExecute(/*enListType,*/ s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit->pstFirstCmd->stNodeBuf.u32PhyAddr,
    s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit->pstFirstCmd->stNodeBuf.u64Update, \
    s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit->bAqUseBuff);
    if (s32Ret == HI_SUCCESS)
    {
        s_pstTDEOsiJobList/*[enListType]*/->pstJobCommitted = s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit;
        s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit = HI_NULL;
        s_pstTDEOsiJobList->pstJobFinished = HI_NULL;
    }
    
    //tde_osr_enableirq();
    TDE_UP(&g_TDEHdMgrLock);

    pstJob->u8WaitForDoneCount++;
    TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);

	#ifndef TDE_BOOT
    if (TDE_JOB_WAKE_NOTIFY == enNotiType)
    {        
        s32Ret = wait_event_interruptible_timeout(s_TdeBlockJobWq, (TDE_JOB_NOTIFY_BUTT == pstJob->enNotiType), u32TimeOut);

        TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
        //tde_osr_disableirq();

        TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
        pstJob->u8WaitForDoneCount--;
        TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);

        if (TDE_JOB_NOTIFY_BUTT == pstJob->enNotiType) 
        {
            TDE_TRACE(TDE_KERN_DEBUG, "handle:%d complete!\n", pstJob->s32Handle); //620

            if (pstJob->u8WaitForDoneCount == 0)
            {
                TdeOsiListDestroyJob(pstJob);
            }
//            TdeOsiListSafeDestroyJob(pstJob);
            //tde_osr_enableirq();
            TDE_UP(&g_TDEHdMgrLock);
            return HI_SUCCESS;
        }
        else
        {
            pstJob->enNotiType = TDE_JOB_COMPL_NOTIFY;
            //tde_osr_enableirq();
            TDE_UP(&g_TDEHdMgrLock);

            if ((-ERESTARTSYS) == s32Ret)
            {
                TDE_TRACE(TDE_KERN_INFO, "handle:%d interrupt!\n", pstJob->s32Handle); //633
                return HI_ERR_TDE_INTERRUPT;
            }
            if(1==asynflag)
            {
            	/*如果由非阻塞方式转为阻塞方式则无超时信息*/
                return HI_SUCCESS;
            }
            TDE_TRACE(TDE_KERN_INFO, "handle:%d timeout!\n", pstJob->s32Handle); //641
            return HI_ERR_TDE_JOB_TIMEOUT;
        }
    }
	#else
	 /* to do:等待TDE任务完成 */
	TdeOsiListWaitTdeIdle();

	/* 从链表中删除该job节点,防止再次遍历到该节点 */
	list_del_init(&pstJob->stList);

	/* 释放job */
	TdeOsiListSafeDestroyJob(pstJob);

	TDE_TRACE(TDE_KERN_DEBUG, "handle:%d complete!\n", pstJob->s32Handle);
	#endif
    return HI_SUCCESS;
}

/*****************************************************************************
 Prototype    : TdeOsiListReset
 Description  : Reset all state, free list space
 Input        : NONE
 Output       : NONE
 Return Value : HI_SUCCESS/HI_FAILURE
 Calls        :
 Called By    :
*****************************************************************************/
HI_VOID TdeOsiListReset(HI_VOID)
{
    TDE_SWJOB_S *pstDelJob;
#if 0
    HI_U32 i = 0;
#if HI_TDE_SQ_SUPPORT
    TDE_LIST_TYPE_E enListType[3] = {TDE_LIST_AQ, TDE_LIST_SQ, TDE_LIST_BUTT};
#else
    TDE_LIST_TYPE_E enListType[2] = {TDE_LIST_AQ, TDE_LIST_BUTT};
#endif
    TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
    tde_osr_disableirq();
#endif
    HI_SIZE_T lockflags;

    while(1)
    {
    	if (TdeHalCtlIsIdleSafely())
    	{
    		TdeHalResumeInit();
    		break;
    	}
    }
    TDE_TRACE(TDE_KERN_DEBUG, "TDE reset successfully!\n"); //684
    TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    //for (i = 0; i < TDE_LIST_BUTT; i++)
    {
        while(!list_empty(&s_pstTDEOsiJobList/*[enListType[i]]*/->stList))
        {
            pstDelJob = list_entry(s_pstTDEOsiJobList/*[enListType[i]]*/->stList.next, TDE_SWJOB_S, stList);
            s_pstTDEOsiJobList/*[enListType[i]]*/->u32JobNum--;

            
            list_del_init(&pstDelJob->stList);

            TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);

            if(TDE_JOB_WAKE_NOTIFY == pstDelJob->enNotiType)
            {
                TDE_TRACE(TDE_KERN_DEBUG, "reset free handle:%d!\n", pstDelJob->s32Handle); //696
                
                pstDelJob->enNotiType = TDE_JOB_NOTIFY_BUTT;
                wake_up_interruptible(&s_TdeBlockJobWq);
            }
            else if (TDE_JOB_COMPL_NOTIFY == pstDelJob->enNotiType)
            {
                TDE_TRACE(TDE_KERN_DEBUG, "reset free handle:%d!\n", pstDelJob->s32Handle); //703

                TdeOsiListSafeDestroyJob(pstDelJob);
            }
#if 0
            if (s_stTdeWaitAll.bBegWait)
            {
                s_stTdeWaitAll.bNodeComplete = HI_TRUE;
                wake_up_interruptible(&s_stTdeWaitAll.stTdeWaitAllWq);
            }
#endif
            TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);

            s_pstTDEOsiJobList->s32HandleFinished = -1;
            s_pstTDEOsiJobList->s32HandleLast = -1;

        	s_pstTDEOsiJobList->pstJobCommitted = HI_NULL;
        	s_pstTDEOsiJobList->pstJobToCommit = HI_NULL;
        	s_pstTDEOsiJobList->pstJobLast = HI_NULL;
        }
#if 0
        //s_pstTDEOsiJobList/*[enListType[i]]*/->s32HandleToCommit = -1;
        s_pstTDEOsiJobList/*[enListType[i]]*/->s32HandleFinished = -1;
        s_pstTDEOsiJobList/*[enListType[i]]*/->s32HandleLast = -1;
        //s_pstTDEOsiJobList/*[enListType[i]]*/->s32HandleCommitted = -1;

        s_pstTDEOsiJobList/*[enListType[i]]*/->pstJobCommitted = HI_NULL;
        s_pstTDEOsiJobList/*[enListType[i]]*/->pstJobToCommit = HI_NULL;
        s_pstTDEOsiJobList/*[enListType[i]]*/->pstJobLast = HI_NULL;
#endif
    }
#if 0
    if (TdeHalCtlIsIdleSafely())
    {
        TDE_TRACE(TDE_KERN_DEBUG, "reset tde is IDLE\n");
        TdeHalResetStatus();
    }
    else
    {
        TDE_TRACE(TDE_KERN_DEBUG, "reset tde is BUSY\n");
    }

    tde_osr_enableirq();
    TDE_UP(&g_TDEHdMgrLock);
#endif
    TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
    
    return;
}

/*****************************************************************************
 Prototype    : TdeOsiListWaitAllDone
 Description  : wait for all TDE operate is completed
 Input        : none
 Output       : NONE
 Return Value : HI_SUCCESS,TDE operate completed
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5

*****************************************************************************/
HI_S32 TdeOsiListWaitAllDone(/*TDE_LIST_TYPE_E enListType*/HI_VOID)
{
    TDE_HANDLE s32WaitHandle;
    HI_SIZE_T lockflags;
    TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    s32WaitHandle = s_pstTDEOsiJobList->s32HandleLast;
    TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
    if (-1 == s32WaitHandle)
    {
        return HI_SUCCESS;
    }
    return TdeOsiListWaitForDone(s32WaitHandle,0);
}

#if 0
/*****************************************************************************
 Prototype    : TdeOsiListIsCompleted
 Description  : query if one command is completed in job
 Input        : s32Handle: job handle
                s32Id: command index
 Output       : NONE
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_BOOL TdeOsiListIsCompleted(TDE_HANDLE s32Handle, HI_U32 u32Id)
{
    HI_BOOL bValid;
    HI_HANDLE_MGR *pHandleMgr;
    
    tde_osr_disableirq();
    bValid = query_handle(s32Handle, &pHandleMgr);
    tde_osr_enableirq();

    return (HI_BOOL) !bValid;
}
#endif

/*****************************************************************************
* Function:      TdeOsiListWaitForDone
* Description:   block to wait for job done
* Input:         s32Handle: job handle
*                u32TimeOut: timeout value
* Output:        none
* Return:        =0,successfully completed <0,error
* Others:
*****************************************************************************/
HI_S32 TdeOsiListWaitForDone(TDE_HANDLE s32Handle, HI_U32 u32TimeOut)
{
    TDE_SWJOB_S * pstJob;
    HI_HANDLE_MGR *pHandleMgr;
    HI_S32 s32Ret;
    HI_BOOL bValid;
    HI_SIZE_T lockflags;
    //TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    //TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
    //tde_osr_disableirq();
    bValid = query_handle(s32Handle, &pHandleMgr);

    if(!bValid)
    {
        //tde_osr_enableirq();
        //TDE_UP(&g_TDEHdMgrLock);
        return HI_SUCCESS;
    }
    /* AI7D02634 */
    pstJob = (TDE_SWJOB_S *)pHandleMgr->res;
    //init_waitqueue_head(&pstJob->stQuery);
    TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    pstJob->u8WaitForDoneCount++;
    pstJob->bInQuery = HI_TRUE;
    //tde_osr_enableirq();
    //TDE_UP(&g_TDEHdMgrLock);
    TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);

    if(u32TimeOut)
    {
        s32Ret = wait_event_interruptible_timeout(pstJob->stQuery, (TDE_JOB_NOTIFY_BUTT == pstJob->enNotiType), u32TimeOut);

        TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
        //TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
        //tde_osr_disableirq();
        pstJob->u8WaitForDoneCount--;
        
        if(TDE_JOB_NOTIFY_BUTT != pstJob->enNotiType)
        {
            pstJob->bInQuery = HI_FALSE;
            //tde_osr_enableirq();
            //TDE_UP(&g_TDEHdMgrLock);
            TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
            if ((-ERESTARTSYS) == s32Ret)
            {
                TDE_TRACE(TDE_KERN_INFO, "query handle (%d) interrupt!\n", pstJob->s32Handle); //788
                return HI_ERR_TDE_INTERRUPT;
            }
            else
            {
                TDE_TRACE(TDE_KERN_INFO, "query handle (%d) time out!\n", pstJob->s32Handle); //793
            }

            return HI_ERR_TDE_QUERY_TIMEOUT;
        }

        TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);

        /** complete */
        if (pstJob->u8WaitForDoneCount == 0) 
        {                  
            TdeOsiListDestroyJob(pstJob);
        }
        
        //tde_osr_enableirq();
        //TDE_UP(&g_TDEHdMgrLock);
        return HI_SUCCESS;
    }

    s32Ret = wait_event_interruptible(pstJob->stQuery, (TDE_JOB_NOTIFY_BUTT == pstJob->enNotiType));
    if ((-ERESTARTSYS) == s32Ret)
    {
        TDE_TRACE(TDE_KERN_INFO, "query handle (%d) interrupt!\n", pstJob->s32Handle); //810
        return HI_ERR_TDE_INTERRUPT;
    }

    TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    pstJob->u8WaitForDoneCount--;
    TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
    //TDE_DOWN_INTERRUPTIBLE(&g_TDEHdMgrLock);
    //tde_osr_disableirq();
    if (pstJob->u8WaitForDoneCount == 0) 
    {
        TdeOsiListDestroyJob(pstJob);
    } 
    //tde_osr_enableirq();
    //TDE_UP(&g_TDEHdMgrLock);

    return HI_SUCCESS;
}

/*****************************************************************************
 Prototype    : TdeOsiListSqUpdateProc
 Description  : sq can update interrupt service     CNcomment:ͬ������ɸ����жϷ������
 Input        : HI_VOID
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
#if 0
HI_VOID TdeOsiListSqUpdateProc(HI_VOID)
{
    HI_VOID *pSuspendNode = HI_NULL;
    TDE_SWNODE_S *pstSuspendNode = HI_NULL;
    HI_S32 s32Ret;
    
    
    TdeHalNodeUpdateSqList();
    
    TDEOsiListSubCmd(TDE_LIST_SQ);
    

    if (s_pstTDEOsiJobList[TDE_LIST_SQ]->pstJobToCommit != HI_NULL)
    {
        TdeHalNodeExecute(TDE_LIST_SQ, s_pstTDEOsiJobList[TDE_LIST_SQ]->pstJobToCommit->pstFirstCmd->stNodeBuf.u32PhyAddr
                        , s_pstTDEOsiJobList[TDE_LIST_SQ]->pstJobToCommit->pstFirstCmd->stNodeBuf.u64Update, s_pstTDEOsiJobList[TDE_LIST_SQ]->bAqUseBuff);
    }
    else
    {
        TdeHalGetSuspendNode(&pSuspendNode);
        if (HI_NULL == pSuspendNode)
        {
            return;
        }
        else
        {
            pstSuspendNode = (TDE_SWNODE_S *)pSuspendNode;
            if(pstSuspendNode->enSubmType == TDE_NODE_SUBM_CHILD)
            {
                pstSuspendNode = pstSuspendNode->pstParentNodeInCmd;

                s32Ret = TdeHalNodeExecute(TDE_LIST_AQ, pstSuspendNode->stNodeBuf.u32PhyAddr, 
                    pstSuspendNode->stNodeBuf.u64Update, HI_FALSE);
                if (s32Ret != HI_SUCCESS)
                {
                    return;
                }
            }
            else
            {
                s32Ret = TdeHalRestoreAloneNode();
                if (s32Ret != HI_SUCCESS)
                {
                    return;
                }

                s32Ret = TdeHalNodeExecute(TDE_LIST_AQ, pstSuspendNode->stNodeBuf.u32PhyAddr
                    , pstSuspendNode->stNodeBuf.u64Update, HI_FALSE);
                if (s32Ret != HI_SUCCESS)
                {
                    return;
                }
            }
        }
        
        
    }
    return;
}
#endif

/*****************************************************************************
 Prototype    : TdeOsiListCompProc
 Description  : list complete interrupt servic, mainly complete switch on hardware lists
 Input        : HI_VOID
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_VOID TdeOsiListCompProc(/*TDE_LIST_TYPE_E enListType*/HI_VOID)
{
    HI_S32 s32Ret;
#if HI_TDE_SQ_SUPPORT
    HI_VOID *pSuspendNode = HI_NULL;
    TDE_SWNODE_S *pstSuspendNode = HI_NULL;
#endif

    HI_SIZE_T lockflags;
    TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);

#if HI_TDE_SQ_SUPPORT
    if (TDE_LIST_SQ == enListType)
    {
        if (s_pstTDEOsiJobList[TDE_LIST_SQ]->pstJobToCommit != HI_NULL)
        {
            TdeHalNodeExecute(TDE_LIST_SQ, s_pstTDEOsiJobList[TDE_LIST_SQ]->pstJobToCommit->pstFirstCmd->stNodeBuf.u32PhyAddr
                            , s_pstTDEOsiJobList[TDE_LIST_SQ]->pstJobToCommit->pstFirstCmd->stNodeBuf.u64Update, \
                            s_pstTDEOsiJobList[TDE_LIST_SQ]->pstJobToCommit->bAqUseBuff);
        }
        else
        {
           
            TdeHalGetSuspendNode(&pSuspendNode);
            if (HI_NULL == pSuspendNode)
            {
                return;
            }
            
                pstSuspendNode = (TDE_SWNODE_S *)pSuspendNode;
                
                if(pstSuspendNode->enSubmType == TDE_NODE_SUBM_CHILD)
                {
                    
                    pstSuspendNode = pstSuspendNode->pstParentNodeInCmd;

                    s32Ret = TdeHalNodeExecute(TDE_LIST_AQ, pstSuspendNode->stNodeBuf.u32PhyAddr, 
                        pstSuspendNode->stNodeBuf.u64Update, HI_TRUE);
                    if (s32Ret != HI_SUCCESS)
                    {
                        return;
                    }
                }
                else
                {
                    s32Ret = TdeHalRestoreAloneNode();
                    if (s32Ret != HI_SUCCESS)
                    {
                        return;
                    }

                    s32Ret = TdeHalNodeExecute(TDE_LIST_AQ, pstSuspendNode->stNodeBuf.u32PhyAddr
                        , pstSuspendNode->stNodeBuf.u64Update, HI_FALSE);
                    if (s32Ret != HI_SUCCESS)
                    {
                        return;
                    }
                }
            
            
        }
    }
#endif

    //tde_osr_disableirq();
    //if (TDE_LIST_AQ == enListType)
    {
        if (s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit != HI_NULL)
        {
            s32Ret = TdeHalNodeExecute(/*enListType,*/ s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit->pstFirstCmd->stNodeBuf.u32PhyAddr,
            s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit->pstFirstCmd->stNodeBuf.u64Update, \
            s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit->bAqUseBuff);
            if (s32Ret == HI_SUCCESS)
            {
                s_pstTDEOsiJobList/*[enListType]*/->pstJobCommitted = s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit;
                s_pstTDEOsiJobList/*[enListType]*/->pstJobToCommit = HI_NULL;
                s_pstTDEOsiJobList->pstJobFinished = HI_NULL;
            }
        }
    }
    //tde_osr_enableirq();
    TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);

    return;    
}

/*****************************************************************************
 Function:      TdeOsiListNodeComp
 Description:   node complete interrupt service, maily complete deleting node and resume suspending,free node 
 Input:         TDE_LIST_TYPE_E enListType  node complete interrupt type
 Output:        none
 Return:        create job handle
 Others:        none
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
HI_VOID TdeOsiListNodeComp(/*TDE_LIST_TYPE_E enListType*/HI_VOID)
{
    HI_HANDLE_MGR *pHandleMgr;
    TDE_SWJOB_S *pstJob;
    HI_S32 s32FinishedHandle;
    TDE_SWJOB_S *pstDelJob;
    TDE_HANDLE s32Delhandle;
    HI_U32 u32RunningSwNodeAddr;
    //HI_BOOL bSqWork;
    HI_BOOL bWork = HI_TRUE;
    HI_U32 *pu32FinishHandle;
    HI_SIZE_T lockflags;
    TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
#if 0
    TdeHalNodeComplteNd(enListType);
    bSqWork = TdeHalIsSqWork();
#endif
    if (TdeHalCtlIsIdleSafely())
    {
        bWork = HI_FALSE;
    }
#if HI_TDE_SQ_SUPPORT    
    if ((bSqWork && (TDE_LIST_AQ == enListType)) || (!bSqWork && (TDE_LIST_SQ == enListType)))
    {
        bWork = HI_FALSE;
    }
#endif
    
    u32RunningSwNodeAddr = TdeHalCurNode(/*enListType*/);
    if (0 == u32RunningSwNodeAddr)
    {
        TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
        return;
    }

    if(u32RunningSwNodeAddr ==(HI_U32)s_pstTDEOsiJobList->pstJobFinished)
    {
        TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
        return;
    }

    pu32FinishHandle = (HI_U32 *)wgetvrt(u32RunningSwNodeAddr - TDE_NODE_HEAD_BYTE  + 4);
    if (HI_NULL == pu32FinishHandle)
    {
        TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
        return;
    }
    
    s32FinishedHandle = *pu32FinishHandle;
    //tde_osr_disableirq();
    if (!bWork)
    {
        s_pstTDEOsiJobList->pstJobFinished = (HI_VOID *)u32RunningSwNodeAddr;
    }
    else
    {
        
        if(!query_handle(s32FinishedHandle, &pHandleMgr))
        {
            //tde_osr_enableirq();
            TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
            return;
        }
        pstJob = (TDE_SWJOB_S *)pHandleMgr->res;

        
        if (pstJob->stList.prev == &s_pstTDEOsiJobList/*[enListType]*/->stList)
        {
            TDE_TRACE(TDE_KERN_DEBUG, "No pre Job left, finishedhandle:%d\n", s32FinishedHandle); //934
            TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
            //tde_osr_enableirq();
            return;
        }

        pstDelJob = list_entry(pstJob->stList.prev, TDE_SWJOB_S, stList);
        s32FinishedHandle = pstDelJob->s32Handle;
        s_pstTDEOsiJobList->pstJobFinished = HI_NULL;
    }
    TDE_TRACE(TDE_KERN_DEBUG, "finishedhandle:%d\n", s32FinishedHandle); //943
    if (!query_handle(s32FinishedHandle, &pHandleMgr))
    {
        TDE_TRACE(TDE_KERN_DEBUG, "handle %d already delete!\n", s32FinishedHandle); //946
        //tde_osr_enableirq();
        TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
        return;
    }

    s_pstTDEOsiJobList/*[enListType]*/->s32HandleFinished = s32FinishedHandle;

    while(!list_empty(&s_pstTDEOsiJobList/*[enListType]*/->stList))
    {
        pstDelJob = list_entry(s_pstTDEOsiJobList/*[enListType]*/->stList.next, TDE_SWJOB_S, stList);
        s32Delhandle = pstDelJob->s32Handle;
        s_pstTDEOsiJobList/*[enListType]*/->u32JobNum--;

        list_del_init(&pstDelJob->stList);

        TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);

        if(TDE_JOB_WAKE_NOTIFY == pstDelJob->enNotiType)
        {
            TDE_TRACE(TDE_KERN_DEBUG, "handle:%d!\n", pstDelJob->s32Handle); //963
           
            pstDelJob->enNotiType = TDE_JOB_NOTIFY_BUTT;
            if (NULL != pstDelJob->pFuncComplCB)
            {
                TDE_TRACE(TDE_KERN_DEBUG, "handle:%d has callback func!\n", pstDelJob->s32Handle); //968
                //TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
                pstDelJob->pFuncComplCB(pstDelJob->pFuncPara, &(pstDelJob->s32Handle));
                //TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
            }
            wake_up_interruptible(&s_TdeBlockJobWq);
        }
        else if (TDE_JOB_COMPL_NOTIFY == pstDelJob->enNotiType)
        {
            TDE_TRACE(TDE_KERN_DEBUG, "handle:%d!\n", pstDelJob->s32Handle); //976

            if (NULL != pstDelJob->pFuncComplCB)
            {
                TDE_TRACE(TDE_KERN_DEBUG, "handle:%d has callback func!\n", pstDelJob->s32Handle); //980
                //TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
                //tde_osr_enableirq();
                pstDelJob->pFuncComplCB(pstDelJob->pFuncPara, &(pstDelJob->s32Handle));
                //tde_osr_disableirq();
                //TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
            }

            TdeOsiListSafeDestroyJob(pstDelJob);
        }
#if 0
        if (s_stTdeWaitAll.bBegWait)
        {
            s_stTdeWaitAll.bNodeComplete = HI_TRUE;
            wake_up_interruptible(&s_stTdeWaitAll.stTdeWaitAllWq);
        }
#endif
        if(s32Delhandle == s32FinishedHandle)
        {
            //tde_osr_enableirq();
            if (!bWork)
            {
                if (TdeHalCtlIsIdleSafely())
                {
                	/*TdeHalSystemInit();*/
                	TdeHalCtlIntStats();

                	TdeOsiListCompProc(/*TDE_LIST_AQ*/);
                }
            }
            return ;
        }
        TDE_LOCK(&s_pstTDEOsiJobList->lock,lockflags);
    }

    //tde_osr_enableirq();
    TDE_UNLOCK(&s_pstTDEOsiJobList->lock,lockflags);
}

/*****************************************************************************
 Function:      TdeOsiListSuspLineProc
 Description:   suspend for interrupt to handle in current line
 Input:         enListType: list type:Sq/Aq
 Output:        none
 Return:        created job handle
 Calls        :
 Called By    :

  History        :
  1.Date         : 2008/3/5
    Author       : w54130
    Modification : Created function

*****************************************************************************/
#if HI_TDE_SQ_SUPPORT
HI_VOID TdeOsiListSuspLineProc(TDE_LIST_TYPE_E enListType)
{
    if (TDE_LIST_SQ == enListType)
    {
       
        return;
    }
    else if (TDE_LIST_AQ == enListType)
    {
        
        TdeHalNodeSuspend();
    }
}
#endif
/*****************************************************************************
* Function:      TdeOsiListGetPhyBuff
* Description:    get one physical buffer, to deflicker and zoom
* Input:         
* Output:        none
* Return:        created job handle
* Others:        none
*****************************************************************************/
HI_U32 TdeOsiListGetPhyBuff(HI_U32 u32CbCrOffset)
{
#ifndef TDE_BOOT
    if (in_interrupt())
    {
        return 0;
    }
#endif
    return TDE_AllocPhysicBuff(u32CbCrOffset);
}

#ifndef TDE_BOOT
void TdeOsiListFreevmem(struct work_struct *work)
{
    TDEFREEWQ_S *pWQueueInfo = container_of(work, TDEFREEWQ_S, work);

    pWQueueInfo->pWQCB(pWQueueInfo->Count);
    TDE_FREE(pWQueueInfo);
}

void TdeOsiListHsr(void* pstFunc, void* data)
{
    TDEFREEWQ_S *pstWQ = NULL;

    pstWQ = TDE_MALLOC(sizeof(TDEFREEWQ_S));
    if(HI_NULL == pstWQ)
    {
        TDE_TRACE(TDE_KERN_INFO, "Malloc TDEFREEWQ_S failed!\n"); //1040
        return;
    }
    pstWQ->Count = (HI_U32)data;
    pstWQ->pWQCB = (TDE_WQ_CB)pstFunc;
    INIT_WORK(&pstWQ->work, TdeOsiListFreevmem);
    schedule_work(&pstWQ->work);
    return;
}
#endif
/*****************************************************************************
* Function:      TdeOsiListPutPhyBuff
* Description:   put back physical buffer
* Input:         u32BuffNum 
* Output:        none
* Return:        none
* Others:        none
*****************************************************************************/
HI_VOID  TdeOsiListPutPhyBuff(HI_U32 u32BuffNum)
{
    if (0 == u32BuffNum)
    {
        return;
    }
    //tde_osr_hsr((HI_VOID *)TdeOsiListDoFreePhyBuff, (HI_VOID *)u32BuffNum);
	#ifndef TDE_BOOT
	TdeOsiListHsr((HI_VOID *)TdeOsiListDoFreePhyBuff, (HI_VOID *)u32BuffNum);
	#else
	TdeOsiListDoFreePhyBuff(u32BuffNum);
	#endif
}

#ifdef __cplusplus
 #if __cplusplus
}
 #endif
#endif /* End of #ifdef __cplusplus */

