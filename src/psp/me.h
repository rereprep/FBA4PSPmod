#ifndef __ME_H__
#define __ME_H__
#ifdef BUILD_PSP
#include <pspkernel.h>
#endif
#define MAX_ORDERS 256
enum CMD
{
	ARM7_RESET=1,
	ARM7_EXECUTE,
	ARM7_SET_IRQ_LINE,
	ARM7_SYNC_CONTEXT,
	ARM7MAPAREA,
	ARM7SETREADHANDLER,
	ARM7SETWRITEHANDLER,
	ARM7RUNEND,
/*	
	SEKREADWORD,
	SEKINIT,
	SEKEXIT,
*/
	SEKNEWFRAME,
	SEKCLOSE,
	SEKOPEN,
	SEKSETIRQLINE,
/*
	SEKRESET,
*/
	SEKRUN,
/*
	SEKMAPMEMORY,
	SEKMAPHANDLER,
	SEKSETREADBYTEHANDLER,
	SEKSETWRITEBYTEHANDLER,
	SEKSETREADWORDHANDLER,
	SEKSETWRITEWORDHANDLER,
	SEKSETREADLONGHANDLER,
	SEKSETWRITELONGHANDLER,
	SEKGETPC,
*/
	SEKSETCYCLESSCANLINE,
	SEKIDLE,
	
	ZETNEWFRAME,
	ZETCLOSE,
	ZETOPEN,
	ZETRUN,
	ZETSETIRQLINE,
	ZETNMI,
	ZETIDLE
};

struct MeOrders
{
	unsigned int command;
	int param[4];
};
struct me_struct
{
	unsigned int count;
	int start;
	int done;
	void (*func)(volatile me_struct *mei);	// function ptr - func takes an int argument and returns int
				// function argument
	int result;			// function return value
	unsigned int signals;
	int init;
	unsigned int meBinSize;
	unsigned char* meBin;	
	unsigned int debugValue;
	void (*arm7RunEndCall)();
	void *mem;
	struct MeOrders meOrders[MAX_ORDERS];
	unsigned short meOrderHead;
	unsigned short meOrderEnd;
	
};
#ifdef __cplusplus
extern "C" {
#endif
int InitME(volatile struct me_struct *mei);
void KillME(volatile struct me_struct *mei);
int CallME(volatile struct me_struct *mei, int func, int param);
int WaitME(volatile struct me_struct *mei);
int BeginME(volatile struct me_struct *mei, int func, int param);
int CheckME(volatile struct me_struct *mei);
unsigned int SignalME(volatile struct me_struct *mei, unsigned int sigmask, unsigned int sigset);
void me_loop(volatile me_struct *mei);

extern volatile struct me_struct *mei;
extern int disableMe;

inline static void dcache_wbinv_all() 
{ 
   int i; 
   for(i = 0; i < 8192; i += 64) 
   { 
      __builtin_allegrex_cache(0x14, i); 
      __builtin_allegrex_cache(0x14, i); 
   } 
}

inline static int isOnMe()
{
	int result;
	__asm__ __volatile__(
	"srl %0, $sp, 28"
	: "=r" (result)
	);
	return result>0||disableMe>0;
}
inline static void meAddCmd(unsigned int command)//, unsigned int param0=0, unsigned int param1=0, unsigned int param2=0, unsigned int param3=0)
{
	unsigned short currentOrder=mei->meOrderEnd;
	/*
	volatile MeOrders *pMeOrder=&(mei->meOrders[currentOrder]);
 	pMeOrder->param[0]=param0;
 	pMeOrder->param[1]=param1;
 	pMeOrder->param[2]=param2;
 	pMeOrder->param[3]=param3;
  pMeOrder->command=command;
  */

  mei->meOrders[currentOrder].command=command;
  currentOrder++;
  if(currentOrder>=MAX_ORDERS)
  	currentOrder=0;
  //sceKernelDcacheWritebackInvalidateAll();
  int iCount;
  for(iCount=0;iCount<4000;iCount++)
  {
  	if(currentOrder!=mei->meOrderHead)
  	{
  		mei->meOrderEnd=currentOrder;
  		return;
  	}
  	sceKernelDelayThread(1000);
  }
  disableMe=1;
 
  
}
inline static void waitMeEnd ()
{
	if(isOnMe())
		return;
	unsigned short currentOrder=mei->meOrderEnd;
	dcache_wbinv_all();

	int iCount;
	for(iCount=0;iCount<0x50000000;iCount++)
	{
  	if(currentOrder==mei->meOrderHead)
  		return;
  	//sceKernelDelayThread(1000);
  }
  disableMe=1;
}

#ifdef __cplusplus
}
#endif
#endif

