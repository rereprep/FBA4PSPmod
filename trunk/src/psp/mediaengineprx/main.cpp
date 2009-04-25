#include <pspsdk.h>
#include <pspkernel.h>
#include <string.h>
#include "../me.h"


#define VERS 1
#define REVS 0

PSP_MODULE_INFO("MediaEngine", 0x1006, VERS, REVS);
PSP_MAIN_THREAD_ATTR(0);
extern "C" {
	
	extern void me_stub(void);
	extern void me_stub_end(void);
	void	sceSysregMeResetEnable371();
	void	sceSysregMeBusClockEnable371();
	void	sceSysregMeResetDisable371();
	int module_start(SceSize args, void *argp);
	int module_stop();
}

volatile struct me_struct* mei;
#define bool int
#define true 1
#define false 0

int InitME(volatile struct me_struct *mei_)
{
	unsigned int k1;

	k1 = pspSdkSetK1(0);

	mei=mei_;
	if (mei == 0)
	{
   		pspSdkSetK1(k1);
   		return -1;
	}

	// initialize the MediaEngine Instance
	mei->start = 0;
	mei->done = 1;
	//mei->func = 0;
	mei->result = 111;
	mei->signals = 0;
	mei->init = 1;
	
	// start the MediaEngine
	memcpy((void *)0xbfc00040,(void *)me_stub, (int)me_stub_end - (int)me_stub);
	_sw((unsigned int)mei->func,  0xbfc00600);	// k0
	_sw((unsigned int)mei, 0xbfc00604);			// a0
	//_sw((unsigned int)(mei->mem)+0x100000, 0xbfc00608);	 //sp
	sceKernelDcacheWritebackAll();
	
	sceSysregMeResetEnable371();
	sceSysregMeBusClockEnable371();
	sceSysregMeResetDisable371();

	pspSdkSetK1(k1);

	return 0;
}


void KillME(volatile struct me_struct *mei)
{
	unsigned int k1;

	k1 = pspSdkSetK1(0);

	if (mei == 0)
	{
		pspSdkSetK1(k1);
		return;
	}

	while (!mei->done);
	mei->init = 0;
	
	pspSdkSetK1(k1);
}


int CallME(volatile struct me_struct *mei, int func, int param)
{
	int result;
	unsigned int k1;

	k1 = pspSdkSetK1(0);

	if (!mei->done)
	{
		pspSdkSetK1(k1);
		return -1;
	}

	mei->done = 0;
	//mei->func = (int (*)(int))func;
	mei->result = 0;
	mei->signals = 0;
	mei->start = 1;

	while (!mei->done);
	result = mei->result;

	pspSdkSetK1(k1);

	return result;
}


int WaitME(volatile struct me_struct *mei)
{
	int result;
	unsigned int k1;

	k1 = pspSdkSetK1(0);

	while (!mei->done);
	result = mei->result;

	pspSdkSetK1(k1);

	return result;
}


int module_start(SceSize args, void *argp)
{
	return 0;
}

int module_stop()
{
	return 0;
}

