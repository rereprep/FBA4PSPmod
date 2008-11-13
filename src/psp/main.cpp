
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspaudio.h>
#include <psprtc.h>
#include <pspiofilemgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "burnint.h"
#include "font.h"
#include "psp.h"
#include "UniCache.h"
#include "exception.h"

PSP_MODULE_INFO(PBPNAME, PSP_MODULE_USER, VERSION_MAJOR, VERSION_MINOR);
PSP_HEAP_SIZE_MAX();
PSP_MAIN_THREAD_ATTR( THREAD_ATTR_USER );

extern int mixbufidDiff;
extern bool enableJoyStick;
//#define MAX_PATH		1024
short skipFrame=0;
int nGameStage = 0;
int bGameRunning = 0;
char currentPath[MAX_PATH];
void loadDefaultInput();

void returnToMenu()
{
	scePowerSetClockFrequency(222, 222, 111);
	setGameStage(1);
	sound_pause();
	draw_ui_main();
}
int exit_callback(int arg1, int arg2, void *common) {
	bGameRunning = 0;
	return 0;
}

//int sleep_flag = 0;

int power_callback(int unknown, int powerInfo, void *arg) {

	if(powerInfo & (PSP_POWER_CB_SUSPENDING | PSP_POWER_CB_POWER_SWITCH | PSP_POWER_CB_STANDBY)) {
		
		returnToMenu();
		sceKernelDelayThread(100000);
		sceIoClose(cacheFile);
		cacheFile = -1;
		//sleep_flag = 1;
	}
	/*
	else if(powerInfo & PSP_POWER_CB_RESUME_COMPLETE) {
		sleep_flag = 0;
	}*/

	return 0;
}

int CallbackThread(SceSize args, void *argp) {
	int cbid;

	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);

	cbid = sceKernelCreateCallback("Power Callback", power_callback, NULL);
	scePowerRegisterCallback(0, cbid);

	sceKernelSleepThreadCB();
	return 0;
}

inline void *video_frame_addr(void *frame, int x, int y)
{
	return (void *)(((unsigned int)frame|0x44000000) + ((x + (y << 9)) << 1));
}

static unsigned int HighCol16(int r, int g, int b, int  /* i */)
{
	unsigned int t;
	t  = (b << 8) & 0xF800;
	t |= (g << 3) & 0x07E0;
	t |= (r >> 3) & 0x001F;
	return t;
}

int DrvInit(int nDrvNum, bool bRestore);
int DrvExit();
int InpInit();
int InpExit();
int InpMake(unsigned int);
void InpDIP();

char szAppCachePath[256];

void chech_and_mk_dir(const char * dir)
{
	SceUID fd = sceIoDopen(dir);
	if (fd >= 0) sceIoDclose(fd);
	else sceIoMkdir(dir, 0777);
}

int main(int argc, char** argv) {
	initExceptionHandler();
	SceCtrlData pad;
	unsigned int autoFireButtons=0;

	loadDefaultInput();
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	getcwd(currentPath, MAX_PATH - 1);
	strcat(currentPath, "/");
	
	strcpy(szAppRomPath, currentPath);
	strcat(szAppRomPath, "ROMS");
	chech_and_mk_dir( szAppRomPath );
	strcat(szAppRomPath, "/");
	
	strcpy(szAppCachePath, currentPath);
	strcat(szAppCachePath, "CACHE");
	chech_and_mk_dir( szAppCachePath );
	strcat(szAppCachePath, "/");
	
	int thid = sceKernelCreateThread(PBPNAME, CallbackThread, 0x11, 0xFA0, 0, 0);
	if(thid >= 0) sceKernelStartThread(thid, 0, 0);
	
	setGameStage (1);
	init_gui();

	BurnLibInit();
	nBurnDrvSelect = ~0U;
	bBurnUseASMCPUEmulation = false;
	
	sound_start();
	
	nBurnBpp = 2;
	nBurnPitch  = 512 * nBurnBpp;
	BurnHighCol = HighCol16;
	
	pBurnDraw = (unsigned char *) video_frame_addr(tex_frame, 0, 0);
	
	draw_ui_main();
	bGameRunning = 1;
	nCurrentFrame = 0;
/*
	u64 ctk, ptk;
	int nframes = 0,nTicksCountInSec=0;
	char fps[32] = {0, };
	sceRtcGetCurrentTick( &ctk );
	ptk = ctk;
*/
	while( bGameRunning ) {
		sceCtrlPeekBufferPositive(&pad, 1); 
		
	/*
		sceRtcGetCurrentTick( &ctk );
		nTicksCountInSec=ctk - ptk;
		if ( nTicksCountInSec>= 1000000 ) {
			ptk += 1000000;
			sprintf( fps, "%2d FPS, mixbufidDiff: %u",  nframes,mixbufidDiff);
			nframes = 0;
			nTicksCountInSec=0;
		}
		nframes ++;
*/	
		if ( nGameStage ) {

			do_ui_key( pad.Buttons );	
			update_gui();
			sceDisplayWaitVblankStart();
			show_frame = draw_frame;
			draw_frame = sceGuSwapBuffers();
		} else {
				//Key hook
				if(enableJoyStick)
				{
					if(pad.Lx>192)
						pad.Buttons|=PSP_CTRL_RIGHT;
					else if(pad.Lx<64)
						pad.Buttons|=PSP_CTRL_LEFT;
					if(pad.Ly>192)
						pad.Buttons|=PSP_CTRL_DOWN;
					else if(pad.Ly<64)
						pad.Buttons|=PSP_CTRL_UP;
				}
				if(pad.Buttons&PSP_CTRL_LTRIGGER)
					pad.Buttons|=hotButtons;
				//key hook end
				
			if((nCurrentFrame&0x3)<2)
			{
				autoFireButtons=autoFireButtons&pad.Buttons;
				
				if ( pad.Buttons & PSP_CTRL_RTRIGGER ) 
				{
					
					if( pad.Buttons&PSP_CTRL_SELECT )
					{
						returnToMenu();								
						continue;
					}else
					{
						autoFireButtons=autoFireButtons|(pad.Buttons&(~PSP_CTRL_RTRIGGER));
					}
				}
			}else
				pad.Buttons=pad.Buttons&(~autoFireButtons);
				
			InpMake(pad.Buttons);
			
			nCurrentFrame++;
			
			if(mixbufidDiff<4&&skipFrame<gameSpeedCtrl)
			{
				skipFrame++;
			}else
			{
				skipFrame=0;
				
				while(mixbufidDiff>6&&bGameRunning)
				{
					sceKernelDelayThread(1000);
				}
				
				pBurnDraw = (unsigned char *) video_frame_addr(tex_frame, 0, 0);
			}
/*			
			drawString(fps, (unsigned short*)((unsigned int)GU_FRAME_ADDR(tex_frame)|0x40000000), 11, 11, R8G8B8_to_B5G6R5(0x404040));
			drawString(fps, (unsigned short*)((unsigned int)GU_FRAME_ADDR(tex_frame)|0x40000000), 10, 10, R8G8B8_to_B5G6R5(0xffffff));
*/			
			if(pBurnDraw)
				show_frame = draw_frame;
				draw_frame = sceGuSwapBuffers();
				update_gui();
			BurnDrvFrame();
			pBurnDraw = NULL;
			
						
					
			//sceDisplayWaitVblankStart();
			sound_next();
		}
		
	}

	scePowerSetClockFrequency(222, 222, 111);
	
	sound_stop();
	exit_gui();
	
	DrvExit();
	BurnLibExit();
	InpExit();
	
	sceAudioSRCChRelease();
	
	sceKernelExitGame();
}


