

#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>
#include <pspkernel.h>
#include <string.h>

#include "pspadhoc.h"

#define PACKET_LENGTH 24


extern unsigned int nCurrentFrame;
extern SceUID sendThreadSem,recvThreadSem;
extern unsigned char forceDelay;

static int pdpId = 0;
static unsigned char g_mac[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static SceUID recvThreadId=0, sendThreadId=0;
//static int pdpStatLength=20;
unsigned int inputKeys[3][3]={{0,},};
static unsigned int recvBuffer[PACKET_LENGTH/4];
static char adhocInited=0;
void resetGame();

int wifiRecv()
{
	unsigned short port;
	unsigned char mac[8];
	
	unsigned int length,recvMaxFrame=0;
	unsigned char currentInput=nCurrentFrame&1U;
	int err;
	//int recvExpiredFrameCount=0; 
	//pdpStatStruct pspStat;
	
	//sceKernelDelayThread(5000); //to be improved here
	
/*
	if(inputKeys[2][2]!=0&&inputKeys[2][2]<=nCurrentFrame)
	{
		if(inputKeys[2][2]==nCurrentFrame)
		{
			inputKeys[currentInput][0]=inputKeys[currentInput][0]|inputKeys[2][0];
			inputKeys[currentInput][1]=inputKeys[currentInput][1]|inputKeys[2][1];
		}
		inputKeys[2][0]=0;
		inputKeys[2][1]=0;
		inputKeys[2][2]=0;
	}
*/
	
	//for(i=0;i<16;i++)
	while(1)
	{
		//if(err||length!=PACKET_LENGTH)
			//sceKernelDelayThread(5000);
		//sceKernelWaitSema(recvThreadSem, 1, 0);
		//sceNetAdhocGetPdpStat(&pdpStatLength, &pspStat);

		//if (pspStat.rcvdData <=0 ) break;
		length=PACKET_LENGTH;
		err = sceNetAdhocPdpRecv(pdpId,
					mac,
					&port,
					(unsigned char*)recvBuffer,
					&length,
					0,	// 0 in lumines
					1);	// 1 in lumines
		if(err==0&&length==PACKET_LENGTH)
		{
			/*
			if(recvBuffer[3]!=recvBuffer[0]+recvBuffer[1]+recvBuffer[2])
			{
				continue;
			}
			*/
			if(recvBuffer[2]==0)
			{
				switch(recvBuffer[1])
				{
					case WIFI_CMD_RESET:
						resetGame();
						return 0;
					default:
						inputKeys[currentInput][0]=inputKeys[currentInput][0]|recvBuffer[0];
						inputKeys[currentInput][1]=inputKeys[currentInput][1]|recvBuffer[1];
				}
			}else if(recvBuffer[2]==nCurrentFrame)
			{
				inputKeys[currentInput][0]=inputKeys[currentInput][0]|recvBuffer[0];
				inputKeys[currentInput][1]=inputKeys[currentInput][1]|recvBuffer[1];
			}else if(recvBuffer[2]>nCurrentFrame)
			{
				if(recvBuffer[5]==nCurrentFrame)
				{
					inputKeys[currentInput][0]=inputKeys[currentInput][0]|recvBuffer[3];
					inputKeys[currentInput][1]=inputKeys[currentInput][1]|recvBuffer[4];
				}
				//else require re-sync
			}
			/*
			else //recvBuffer[2]<nCurrentFrame
			{
				if(recvExpiredFrameCount>0)
					return -1;
				recvExpiredFrameCount++;
			}*/
			if(recvBuffer[2]>recvMaxFrame)
			{
				recvMaxFrame=recvBuffer[2];
			}
			
		}else
		{
			break;
		}
			
	}
	if(recvMaxFrame<nCurrentFrame)
	{
		return -1;
	}
	return 0;
}

int wifiSend(unsigned int wifiCMD)
{
	unsigned char currentInput;
	
	if(wifiCMD)
	{
		recvBuffer[1]=wifiCMD;
		recvBuffer[2]=0;

		sceNetAdhocPdpSend(pdpId,
			&g_mac[0],
			0x309,
			&recvBuffer,
			PACKET_LENGTH,
			0,	// 0 in lumines
			1);	// 1 in lumines
		sceKernelDelayThread(5000);
		sceNetAdhocPdpSend(pdpId,
			&g_mac[0],
			0x309,
			&recvBuffer,
			PACKET_LENGTH,
			0,	// 0 in lumines
			1);	// 1 in lumines
		sceKernelDelayThread(5000);
		sceNetAdhocPdpSend(pdpId,
			&g_mac[0],
			0x309,
			&recvBuffer,
			PACKET_LENGTH,
			0,	// 0 in lumines
			1);	// 1 in lumines
		sceKernelDelayThread(5000);
	}else{
		//sceKernelWaitSema(sendThreadSem, 1, 0);
		currentInput=nCurrentFrame&1U;
		
		//if(inputKeys[currentInput][0]!=0||inputKeys[currentInput][1]!=0)
		{
			//inputKeys[currentInput][2]=nCurrentFrame;
			//inputKeys[currentInput][3]=inputKeys[currentInput][0]+inputKeys[currentInput][1]+inputKeys[currentInput][2];
			if(currentInput==1)
			{
				inputKeys[2][0]=inputKeys[0][0];
				inputKeys[2][1]=inputKeys[0][1];
				inputKeys[2][2]=inputKeys[0][2];
			}
			sceNetAdhocPdpSend(pdpId,
			&g_mac[0],
			0x309,
			&inputKeys[currentInput],
			PACKET_LENGTH,
			0,	// 0 in lumines
			0);	// 1 in lumines
			//sceKernelDelayThread(3000);
		}
	}
	
	return 0;
}


int adhocLoadDrivers()
{

	sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);         // AHMAN
	sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);          // AHMAN

	return 0;
}

int adhocInit(char* netWorkName)
{
	if ( netWorkName==0||adhocInited)
		return;
	adhocInited=1;	
	char mac[6];
	struct productStruct product;

	strcpy(product.product, "ULUS99999");
	product.unknown = 0;

    u32 err;
	////printf2("sceNetInit()\n");
    err = sceNetInit(0x20000, 0x20, 0x1000, 0x20, 0x1000);
    if (err != 0)
        return err;
	//g_NetInit = true;

	//printf2("sceNetAdhocInit()\n");
    err = sceNetAdhocInit();
    if (err != 0)
        return err;
	//g_NetAdhocInit = true;

	//printf2("sceNetAdhocctlInit()\n");
    err = sceNetAdhocctlInit(0x2000, 0x20, &product);
    if (err != 0)
        return err;
	//g_NetAdhocctlInit = true;

    // Connect
    
    //printf2("sceNetAdhocctlConnect()\n");
    err = sceNetAdhocctlConnect(netWorkName);
    if (err != 0)
        return err;
	//g_NetAdhocctlConnect = true;

    int stateLast = -1;
    //printf2("Connecting...\n");
    int i;
    for(i=0;i<100;i++)
    {
        int state;
        err = sceNetAdhocctlGetState(&state);
        if (err != 0)
        {
        	//pspDebugScreenInit();
            //printf("sceNetApctlGetState returns $%x\n", err);
            sceKernelDelayThread(10*1000000); // 10sec to read before exit
			return -1;
        }
        if (state > stateLast)
        {
            //printf2("  connection state %d of 1\n", state);
            stateLast = state;
        }
        if (state == 1)
            break;  // connected

        // wait a little before polling again
        sceKernelDelayThread(50*1000); // 50ms
    }
    if(i>=100) return -1;
    
    //printf2("Connected!\n");
  
    
	sceWlanGetEtherAddr(mac);
	
	
    //printf2("sceNetAdhocPdpCreate\n");
    
    
	pdpId = sceNetAdhocPdpCreate(mac,
		     0x309,		// 0x309 in lumines
		     0x400, 	// 0x400 in lumines
		     0);		// 0 in lumines
	if(pdpId <= 0)
	{
		
		//pspDebugScreenInit();
		//printf("pdpId = %x\n", pdpId);
		return -1;
	}

	return 0;
}


int adhocSend(void *buffer, int length)
{
	int err=0;

	err = sceNetAdhocPdpSend(pdpId,
			&g_mac[0],
			0x309,
			buffer,
			length,
			0,	// 0 in lumines
			1);	// 1 in lumines

	return err;
}

int adhocTerm()
{
    if(adhocInited==0) return;
    
    u32 err;

	{
		////printf2("sceNetAdhocPdpDelete\n");
		err = sceNetAdhocPdpDelete(pdpId,0);
		if(err != 0)
		{
			//pspDebugScreenInit();
			//printf("sceNetAdhocPdpDelete returned %x\n", err);
		}

	}

	sceNetAdhocctlDisconnect();
	
	{
		//printf2("sceNetAdhocctlTerm\n");
		err = sceNetAdhocctlTerm();
		if(err != 0)
		{
			//pspDebugScreenInit();
			//printf("sceNetAdhocctlTerm returned %x\n", err);
		}
		//g_NetAdhocctlInit = false;
	}

	{
		////printf2("sceNetAdhocTerm\n");
		err = sceNetAdhocTerm();
		if(err != 0)
		{
			//pspDebugScreenInit();
			//printf("sceNetAdhocTerm returned %x\n", err);
		}
		//g_NetAdhocInit = false;
	}


	{
		////printf2("sceNetTerm\n");
		err = sceNetTerm();
		if(err != 0)
		{
			//pspDebugScreenInit();
			//printf("sceNetTerm returned %x\n", err);
		}
		//g_NetInit = false;
	}
	adhocInited=0;
    return 0; // assume it worked
}

