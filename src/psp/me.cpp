#include "me.h"
#include "sek.h"
#include "zet.h"
static struct me_struct __attribute__((aligned(64))) me_struct_static;
volatile struct me_struct *mei=( struct me_struct *)(((int) &me_struct_static) | 0x40000000);
int disableMe=0;

int BeginME(volatile struct me_struct *mei, int func, int param)
{
	if (!mei->done)
		return -1;

	mei->done = 0;
	mei->result = 0;
	mei->signals = 0;
	mei->start = 1;

	return 0;
}


int CheckME(volatile struct me_struct *mei)
{
	return mei->done;
}


unsigned int SignalME(volatile struct me_struct *mei, unsigned int sigmask, unsigned int sigset)
{
	unsigned int signals;

	signals = mei->signals;
	mei->signals = (mei->signals & ~sigmask) | (sigset & sigmask);

	return signals;
}

void me_loop(volatile me_struct *mei)
{
	volatile MeOrders *pMeOrder;
	mei->result=999;
	unsigned short currentOrder=mei->meOrderHead;
	while(1)
	{
		
		if(currentOrder==mei->meOrderEnd)
		{
			dcache_wbinv_all();
			while(currentOrder==mei->meOrderEnd)
			{
				;//mei->count++;
			}
			
		}
		
		
		pMeOrder=&(mei->meOrders[currentOrder]);
		switch(pMeOrder->command)
		{
/*			
			case ARM7_RESET:

				arm7_reset();
				testMode=true;
				
				break;

			case ARM7_EXECUTE:

				mei->result=arm7_execute(pMeOrder->param[0]);
				break;
	
			case ARM7_SET_IRQ_LINE:

				arm7_set_irq_line(pMeOrder->param[0], pMeOrder->param[1]);
				break;
				
			case ARM7_SYNC_CONTEXT:

				
				break;
			
			case ARM7MAPAREA:

				arm7MapArea(pMeOrder->param[0],pMeOrder->param[1],pMeOrder->param[2],(unsigned char *)(pMeOrder->param[3]));
				break;
			
					
			case ARM7SETREADHANDLER:

				
				arm7SetReadHandler((unsigned int (*)(unsigned int))pMeOrder->param[0]);
				
				break;
			
			case ARM7SETWRITEHANDLER:

				
				arm7SetWriteHandler((void (*)(unsigned int, unsigned int))pMeOrder->param[0]);
				
				break;
			
			case ARM7RUNEND:

				arm7RunEnd();
				break;
			case SEKREADWORD:

				mei->result=meSekReadWord(pMeOrder->param[0]);
				break;
			case SEKINIT:

				mei->result=meSekInit(pMeOrder->param[0],pMeOrder->param[1]);	
				break;
			case SEKEXIT:

				mei->result=meSekExit();
				break;
*/
			case SEKNEWFRAME:

				meSekNewFrame();		
				break;
			case SEKCLOSE:

				meSekClose();		
				break;
			case SEKOPEN:

				meSekOpen(pMeOrder->param[0]);		
				break;
			case SEKSETIRQLINE:

				meSekSetIRQLine(pMeOrder->param[0],pMeOrder->param[1]);		
				break;
/*			case SEKRESET:

				meSekReset();		
				break;
*/
			case SEKRUN:

				meSekRun(pMeOrder->param[0]);		
				break;
/*			case SEKMAPMEMORY:

				mei->result=meSekMapMemory((unsigned char*)pMeOrder->param[0],pMeOrder->param[1],pMeOrder->param[2],pMeOrder->param[3]);		
				break;
			case SEKMAPHANDLER:

				mei->result=meSekMapHandler(pMeOrder->param[0],pMeOrder->param[1],pMeOrder->param[2],pMeOrder->param[3]);		
				break;
			case SEKSETREADBYTEHANDLER:

				mei->result=meSekSetReadByteHandler(pMeOrder->param[0],(pmeSekReadByteHandler)pMeOrder->param[1]);		
				break;
			case SEKSETWRITEBYTEHANDLER:

				mei->result=meSekSetWriteByteHandler(pMeOrder->param[0],(pmeSekWriteByteHandler)pMeOrder->param[1]);		
				break;
			case SEKSETREADWORDHANDLER:

				mei->result=meSekSetReadWordHandler(pMeOrder->param[0],(pmeSekReadWordHandler)pMeOrder->param[1]);		
				break;
			case SEKSETWRITEWORDHANDLER:

				mei->result=meSekSetWriteWordHandler(pMeOrder->param[0],(pmeSekWriteWordHandler)pMeOrder->param[1]);
				break;
			case SEKSETREADLONGHANDLER:

				mei->result=meSekSetReadLongHandler(pMeOrder->param[0],(pmeSekReadLongHandler)pMeOrder->param[1]);
				break;
			case SEKSETWRITELONGHANDLER:

				mei->result=meSekSetWriteLongHandler(pMeOrder->param[0],(pmeSekWriteLongHandler)pMeOrder->param[1]);
				break;
			case SEKGETPC:

				mei->result=meSekGetPC(pMeOrder->param[0]);
				break;
*/
			case SEKSETCYCLESSCANLINE:

				meSekSetCyclesScanline(pMeOrder->param[0]);
				break;
			case SEKIDLE:

				meSekIdle(pMeOrder->param[0]);
				break;
				
			case ZETNEWFRAME:
				
				meSekNewFrame();
				break;
			case ZETCLOSE:
				
				meZetClose();
				break;
			case ZETOPEN:

				meZetOpen(pMeOrder->param[0]);
				break;
			case ZETRUN:
				
				meZetRun(pMeOrder->param[0]);
				break;
			case ZETSETIRQLINE:
				
				meZetSetIRQLine(pMeOrder->param[0], pMeOrder->param[1]);
				break;
			case ZETNMI:
				
				meZetNmi();
				break;
			case ZETIDLE:
				
				meZetIdle(pMeOrder->param[0]);
				break;
			default:

			mei->debugValue=pMeOrder->command;
				break;		
		
		
		}
		currentOrder++;
		if(currentOrder>=MAX_ORDERS)
  		currentOrder=0;
		mei->meOrderHead=currentOrder;
	}
	
}


