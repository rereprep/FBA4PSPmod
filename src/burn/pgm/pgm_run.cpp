
#include "pgm.h"
#include "arm7_intf.h"
#include "UniCache.h"

unsigned char PgmJoy1[8] = {0,0,0,0,0,0,0,0};
unsigned char PgmJoy2[8] = {0,0,0,0,0,0,0,0};
unsigned char PgmJoy3[8] = {0,0,0,0,0,0,0,0};
unsigned char PgmJoy4[8] = {0,0,0,0,0,0,0,0};
unsigned char PgmBtn1[8] = {0,0,0,0,0,0,0,0};
unsigned char PgmBtn2[8] = {0,0,0,0,0,0,0,0};
unsigned char PgmInput[8] = {0,0,0,0,0,0,0,0};
unsigned char PgmReset = 0;

int nPGM68KROMLen = 0;
int nPGMTileROMLen = 0;
int nPGMSPRColROMLen = 0;
int nPGMSPRMaskROMLen = 0;
int nPGMSNDROMLen = 0;

unsigned int *RamBg, *RamTx, *RamCurPal;
unsigned short *RamRs, *RamPal, *RamVReg, *RamSpr;
static unsigned char *RamZ80;
unsigned char *Ram68K;

static unsigned char *Mem = NULL, *MemEnd = NULL;
static unsigned char *RamStart, *RamEnd;

unsigned char *USER0, *USER1, *USER2;
unsigned char *PGM68KBIOS, *PGM68KROM, *PGMTileROM, *PGMTileROMExp, *PGMSPRColROM = NULL, *PGMSPRMaskROM = NULL, *PGMARMROM;
unsigned char *PGMARMRAM0, *PGMARMRAM1, *PGMARMRAM2, *PGMARMShareRAM;

unsigned char nPgmPalRecalc = 0;
unsigned char nPgmZ80Work = 0;

static bool bGamePuzlstar = false;
static bool bGameDrgw2 = false;

void (*pPgmResetCallback)() = NULL;
void (*pPgmInitCallback)() = NULL;
int (*pPgmScanCallback)(int, int*) = NULL;

static int kov2 = 0;
static int nEnableArm7 = 0;

#define M68K_CYCS_PER_FRAME	(20000000 / 60)
#define Z80_CYCS_PER_FRAME	( 8468000 / 60)
#define ARM7_CYCS_PER_FRAME	(20000000 / 60)

#define	PGM_INTER_LEAVE	2

#define M68K_CYCS_PER_INTER	(M68K_CYCS_PER_FRAME / PGM_INTER_LEAVE)
#define Z80_CYCS_PER_INTER	(Z80_CYCS_PER_FRAME  / PGM_INTER_LEAVE)
#define ARM7_CYCS_PER_INTER	(ARM7_CYCS_PER_FRAME / PGM_INTER_LEAVE)

static int nCyclesDone[3];

bool bPgmUseCache = false;
static bool bUseArm=false;
static bool bPgmCreateCache = false;
unsigned long nPGMTileROMOffset;
unsigned long nPGMSPRColROMOffset;
unsigned long nPGMSPRMaskROMOffset;
unsigned long nPGMSNDROMOffset;



static int pgmMemIndex()
{
	unsigned char *Next; Next = Mem;
	PGM68KBIOS	= Next; Next += 0x0020000;		// 68000 BIOS
	PGM68KROM	= Next; Next += nPGM68KROMLen;	// 68000 PRG (max 0x400000)
	USER0		= Next; Next += 0x0200000;
	if (BurnDrvGetHardwareCode() & HARDWARE_IGS_USE_ARM_CPU) {
		PGMARMROM	= Next; Next += 0x0004000;
	}
	
	RamStart	= Next;
	
	Ram68K		= Next; Next += 0x0020000;						// 128K Main RAM
	RamBg		= (unsigned int *) Next; Next += 0x0004000;
	RamTx		= (unsigned int *) Next; Next += 0x0002000;
	RamRs		= (unsigned short *) Next; Next += 0x0000800;	// Row Scroll
	RamPal		= (unsigned short *) Next; Next += 0x0001200;	// Palette R5G5B5
	RamVReg		= (unsigned short *) Next; Next += 0x0010000;	// Video Regs inc. Zoom Table
	RamZ80		= Next; Next += 0x0010000;
	RamSpr		= (unsigned short *) Ram68K;
	//RamSprBuf	= (unsigned short*)Next; Next += 0xa00;
	RamCurPal	= (unsigned int *) Next; Next += 0x001220 * sizeof(unsigned int);

	if (BurnDrvGetHardwareCode() & HARDWARE_IGS_USE_ARM_CPU) {
		PGMARMShareRAM	= Next; Next += 0x0010000;
		PGMARMRAM0	= Next; Next += 0x0001000; // minimum map is 0x1000 - should be 0x400 :S
		PGMARMRAM1	= Next; Next += 0x0010000;
		PGMARMRAM2	= Next; Next += 0x0001000; // minimum map is 0x1000 - should be 0x400 :S
	}	
	RamEnd		= Next;

	spriteCacheArray = (SpriteCacheIndex *) Next; Next += sizeof(SpriteCacheIndex)*SPRITE_CACHE_SIZE;
	MemEnd		= Next;
	return 0;
}

static void loadAndWriteRomToCache(int i,unsigned int romLength)
{
	static int j;
	BurnLoadRom(uniCacheHead, i, 1);
	for(j=0;j<5;j++)
	{
		sceIoLseek( cacheFile, cacheFileSize, SEEK_SET );
		if( romLength == sceIoWrite(cacheFile, uniCacheHead, romLength ) )
			break;
	}
}

static int pgmGetRoms(bool bLoad)
{
	char* pRomName;
	struct BurnRomInfo ri;
	struct BurnRomInfo pi;

	unsigned char *PGM68KROMLoad = PGM68KROM;
	unsigned char *PGMUSER0Load = USER0;
	unsigned char *PGMTileROMLoad = PGMTileROM + 0x400000;
	unsigned char *PGMSPRColROMLoad = PGMSPRColROM;
	unsigned char *PGMSPRMaskROMLoad = PGMSPRMaskROM;
	unsigned int biosRomRegionLength=0x400000;
	cacheFileSize = 0;
	nPGMTileROMOffset = 0xffffffff;
	nPGMSPRColROMOffset = 0xffffffff;
	nPGMSPRMaskROMOffset = 0xffffffff;
	nPGMSNDROMOffset = 0xffffffff;
	if(strstr(BurnDrvGetTextA(DRV_NAME), "kov2"))
		biosRomRegionLength=0x800000;
	
	for (int i = 0; !BurnDrvGetRomName(&pRomName, i, 0); i++) {

		BurnDrvGetRomInfo(&ri, i);

		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 1)
		{
			if (bLoad) {
				BurnDrvGetRomInfo(&pi, i+1);

				if (ri.nLen == 0x80000 && pi.nLen == 0x80000)
				{
					BurnLoadRom(PGM68KROMLoad + 0, i + 0, 2);
					BurnLoadRom(PGM68KROMLoad + 1, i + 1, 2);
					PGM68KROMLoad += pi.nLen;
					i += 1;
				}
				else
				{
					BurnLoadRom(PGM68KROMLoad, i, 1);
				}
				PGM68KROMLoad += ri.nLen;				
			} else {
				nPGM68KROMLen += ri.nLen;
			}
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 2)
		{
			if (bLoad) {
				if (!bPgmUseCache) {
					BurnLoadRom(PGMTileROMLoad, i, 1);
					PGMTileROMLoad += ri.nLen;
				}else 
				{
					if(nPGMTileROMOffset == 0xffffffff)
					{
						
						nPGMTileROMOffset = cacheFileSize;
						if ( bPgmCreateCache ) {
							loadAndWriteRomToCache(0x00081,0x400000);							
						}
						cacheFileSize = cacheFileSize+0x400000;						
					}
					if(bPgmCreateCache)
					{
						loadAndWriteRomToCache(i,ri.nLen);
					}
					cacheFileSize = cacheFileSize + ri.nLen;
				}				
			} else {
				nPGMTileROMLen += ri.nLen;
			}
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 3)
		{
			if (bLoad) {
				if (!bPgmUseCache) {
					BurnLoadRom(PGMSPRColROMLoad, i, 1);
					PGMSPRColROMLoad += ri.nLen;
				} else 
				{
					if(nPGMSPRColROMOffset == 0xffffffff)
					{
						
						nPGMSPRColROMOffset = cacheFileSize;
					}
					if ( bPgmCreateCache ) {						
						loadAndWriteRomToCache(i,ri.nLen);
					}
					cacheFileSize = cacheFileSize + ri.nLen;
				}				
			} else {
				nPGMSPRColROMLen += ri.nLen;
			}
			continue;
		}

		if ((ri.nType & BRF_GRA) && (ri.nType & 0x0f) == 4)
		{
			if (bLoad) {
				if (!bPgmUseCache) {
					BurnLoadRom(PGMSPRMaskROMLoad, i, 1);
					PGMSPRMaskROMLoad += ri.nLen;
				} else 
				{
					if(nPGMSPRMaskROMOffset == 0xffffffff)
					{
						
						nPGMSPRMaskROMOffset = cacheFileSize;
					}
					if ( bPgmCreateCache ) {
						loadAndWriteRomToCache(i,ri.nLen);
					}
					cacheFileSize = cacheFileSize + ri.nLen;
				}				
			} else {
				nPGMSPRMaskROMLen += ri.nLen;
			}
			continue;
		}

		if ((ri.nType & BRF_SND) && (ri.nType & 0x0f) == 5)
		{
			if (bLoad) {
				if (!bPgmUseCache) {
					//BurnLoadRom(PGMSNDROMLoad, i, 1);
					//PGMSNDROMLoad += ri.nLen;
				}else 
				{
					if(nPGMSNDROMOffset == 0xffffffff)
					{
						
						nPGMSNDROMOffset = cacheFileSize;
						if ( bPgmCreateCache ) {
							loadAndWriteRomToCache(0x00082,biosRomRegionLength);							
						}
						cacheFileSize = cacheFileSize+biosRomRegionLength;						
					}
					if(bPgmCreateCache)
					{
						loadAndWriteRomToCache(i,ri.nLen);
					}
					cacheFileSize = cacheFileSize + ri.nLen;
				}
			} else {
				nPGMSNDROMLen += ri.nLen;
			}
			continue;
		}
		
		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 8)
		{
			if (bLoad) {
				BurnDrvGetRomInfo(&pi, i+1);

				if (ri.nLen == 0x80000 && pi.nLen == 0x80000)
				{
					BurnLoadRom(PGMUSER0Load + 0, i + 0, 2);
					BurnLoadRom(PGMUSER0Load + 1, i + 1, 2);
					PGMUSER0Load += pi.nLen;
					i += 1;
				}
				else
				{
					BurnLoadRom(PGMUSER0Load, i, 1);
				}
				PGMUSER0Load += ri.nLen;				
			}
			continue;
		}
		
		if ((ri.nType & BRF_PRG) && (ri.nType & 0x0f) == 7)
		{
			if (bLoad) {
				BurnLoadRom(PGMARMROM, i, 1);			
			}
			continue;
		}
	}

	if (!bLoad) nPGMTileROMLen += 0x400000;

	if (!bLoad) nPGMSNDROMLen += biosRomRegionLength;

	return 0;
}

/* Calendar Emulation */

static unsigned char CalVal, CalMask, CalCom=0, CalCnt=0;

static unsigned char bcd(unsigned char data)
{
	return ((data / 10) << 4) | (data % 10);
}

static unsigned char pgm_calendar_r()
{
	unsigned char calr;
	calr = (CalVal & CalMask) ? 1 : 0;
	CalMask <<= 1;
	return calr;
}

static void pgm_calendar_w(unsigned short data)
{
	time_t nLocalTime = time(NULL);
	tm* tmLocalTime = localtime(&nLocalTime);

	CalCom <<= 1;
	CalCom |= data & 1;
	++CalCnt;
	if(CalCnt==4)
	{
		CalMask = 1;
		CalVal = 1;
		CalCnt = 0;

		switch(CalCom & 0xf)
		{
			case 0x1: case 0x3: case 0x5: case 0x7: case 0x9: case 0xb: case 0xd:
				CalVal++;
				break;

			case 0x0: // Day
				CalVal=bcd(tmLocalTime->tm_wday);
				break;

			case 0x2:  // Hours
				CalVal=bcd(tmLocalTime->tm_hour);
				break;

			case 0x4:  // Seconds
				CalVal=bcd(tmLocalTime->tm_sec);
				break;

			case 0x6:  // Month
				CalVal=bcd(tmLocalTime->tm_mon + 1); // not bcd in MVS
				break;

			case 0x8: // Milliseconds?
				CalVal=0;
				break;

			case 0xa: // Day
				CalVal=bcd(tmLocalTime->tm_mday);
				break;

			case 0xc: // Minute
				CalVal=bcd(tmLocalTime->tm_min);
				break;

			case 0xe: // Year
				CalVal=bcd(tmLocalTime->tm_year % 100);
				break;

			case 0xf: // Load Date
				tmLocalTime = localtime(&nLocalTime);
				break;
		}
	}
}

inline static unsigned int CalcCol(unsigned short nColour)
{
#ifndef BUILD_PSP
	int r, g, b;

	r = (nColour & 0x001F) << 3;	// Red
	r |= r >> 5;
	g = (nColour & 0x03E0) >> 2;  // Green
	g |= g >> 5;
	b = (nColour & 0x7C00) >> 7;	// Blue
	b |= b >> 5;

	return BurnHighCol(b, g, r, 0);
#else
	return ((nColour & 0x001f) << 11) | 
	       ((nColour & 0x03e0) <<  1) | 
	       ((nColour & 0x7c00) >> 10);
#endif
}

unsigned char __fastcall PgmReadByte(unsigned int sekAddress)
{
	switch (sekAddress)
	{
		case 0xC00007:
			return pgm_calendar_r();

//		default:
//			bprintf(PRINT_NORMAL, _T("Attempt to read byte value of location %x\n"), sekAddress);

	}
	return 0;
}

unsigned short __fastcall PgmReadWord(unsigned int sekAddress)
{
	switch (sekAddress) {
		case 0xC00004:
			return ics2115_soundlatch_r(1);

		case 0xC08000:	// p1+p2 controls
			return ~(PgmInput[0] | (PgmInput[1] << 8));

		case 0xC08002:  // p3+p4 controls
			return ~(PgmInput[2] | (PgmInput[3] << 8));

		case 0xC08004:  // extra controls
			return ~(PgmInput[4] | (PgmInput[5] << 8));

		case 0xC08006: // dipswitches
			return ~(PgmInput[6]) | 0xffe0;

//		default:
//			bprintf(PRINT_NORMAL, _T("Attempt to read word value of location %x\n"), sekAddress);
	}
	return 0;
}

void __fastcall PgmWriteByte(unsigned int sekAddress, unsigned char /*byteValue*/)
{
//	switch (sekAddress) {
//		default:
//			bprintf(PRINT_NORMAL, _T("Attempt to write byte value %x to location %x\n"), byteValue, sekAddress);
//	}
}

void __fastcall PgmWriteWord(unsigned int sekAddress, unsigned short wordValue)
{
	switch (sekAddress) {

		case 0x700006:	// Watchdog?
			break;

		case 0xC00002:
			ics2115_soundlatch_w(0, wordValue);
			if(nPgmZ80Work) ZetNmi();
			break;

		case 0xC00004:
			ics2115_soundlatch_w(1, wordValue);
			break;

		case 0xC00006:
			pgm_calendar_w(wordValue);
			break;

		case 0xC00008:
			if (wordValue == 0x5050) {
				ics2115_reset();
				nPgmZ80Work = 1;

				ZetReset();
			} else {
				nPgmZ80Work = 0;
			}
			break;

		case 0xC0000A:	// z80_ctrl_w
			break;

		case 0xC0000C:
			ics2115_soundlatch_w(2, wordValue);
			break;

//		default:
//			bprintf(PRINT_NORMAL, _T("Attempt to write word value %x to location %x\n"), wordValue, sekAddress);
	}
}

unsigned char __fastcall PgmZ80ReadByte(unsigned int sekAddress)
{
//	switch (sekAddress) {
//		default:
//			bprintf(PRINT_NORMAL, _T("Attempt to read byte value of location %x\n"), sekAddress);
//	}
	return 0;
}

unsigned short __fastcall PgmZ80ReadWord(unsigned int sekAddress)
{
	sekAddress -= 0xC10000;
	return (RamZ80[sekAddress] << 8) | RamZ80[sekAddress+1];
}

void __fastcall PgmZ80WriteWord(unsigned int sekAddress, unsigned short wordValue)
{
	sekAddress -= 0xC10000;
	RamZ80[sekAddress] = wordValue >> 8;
	RamZ80[sekAddress+1] = wordValue & 0xFF;
}

unsigned char __fastcall PgmZ80PortRead(unsigned short port)
{
	switch (port >> 8)
	{
		case 0x80:
			return ics2115read(port & 0xff);

		case 0x81:
			return ics2115_soundlatch_r(2) & 0xff;

		case 0x82:
			return ics2115_soundlatch_r(0) & 0xff;

		case 0x84:
			return ics2115_soundlatch_r(1) & 0xff;

//		default:
//			bprintf(PRINT_NORMAL, _T("Z80 Attempt to read port %04x\n"), port);
	}
	return 0;
}

void __fastcall PgmZ80PortWrite(unsigned short port, unsigned char data)
{
	switch (port >> 8) {
		case 0x80:
			ics2115write(port & 0xff, data);
			break;

		case 0x81:
			ics2115_soundlatch_w(2, data);
			break;

		case 0x82:
			ics2115_soundlatch_w(0, data);
			break;

		case 0x84:
			ics2115_soundlatch_w(1, data);
			break;

//		default:
//			bprintf(PRINT_NORMAL, _T("Z80 Attempt to write %02x to port %04x\n"), data, port);
	}
}

void pgm_cpu_sync()
{
	// I'd rather use Arm7TotalCycles(), but nCyclesDone[2] works...
	int nCycles = SekTotalCycles() - nCyclesDone[2];

	if (nCycles > 0) {
		nCyclesDone[2] += Arm7Run(nCycles);
	}
}

int PgmDoReset()
{
	SekOpen(0);
	SekSetIRQLine(0, SEK_IRQSTATUS_NONE);
	SekReset();
	SekClose();
if (nEnableArm7) {
		Arm7Open(0);
		Arm7Reset();
		Arm7Close();
	}

	nPgmZ80Work = 0;
	ZetReset();
	ics2115_reset();

	if (pPgmResetCallback) {
		pPgmResetCallback();
	}
	return 0;
}

#ifndef PGM_LOW_MEMORY
static void expand_gfx_2()
{
	unsigned char *src = PGMTileROM;
	unsigned char *dst = PGMTileROMExp;

	for (int i = nPGMTileROMLen/5-1; i >= 0 ; i --) {
		dst[0+8*i] = ((src[0+5*i] >> 0) & 0x1f);
		dst[1+8*i] = ((src[0+5*i] >> 5) & 0x07) | ((src[1+5*i] << 3) & 0x18);
		dst[2+8*i] = ((src[1+5*i] >> 2) & 0x1f );
		dst[3+8*i] = ((src[1+5*i] >> 7) & 0x01) | ((src[2+5*i] << 1) & 0x1e);
		dst[4+8*i] = ((src[2+5*i] >> 4) & 0x0f) | ((src[3+5*i] << 4) & 0x10);
		dst[5+8*i] = ((src[3+5*i] >> 1) & 0x1f );
		dst[6+8*i] = ((src[3+5*i] >> 6) & 0x03) | ((src[4+5*i] << 2) & 0x1c);
		dst[7+8*i] = ((src[4+5*i] >> 3) & 0x1f );
	}
}
#endif

int pgmInit()
{
	spriteCacheArrayFreeP=0;
	Mem = NULL;
	bGamePuzlstar = strcmp(BurnDrvGetTextA(DRV_NAME), "puzlstar") == 0;
	bGameDrgw2 = strcmp(BurnDrvGetTextA(DRV_NAME), "drgw2") == 0 || strcmp(BurnDrvGetTextA(DRV_NAME), "drgw2c") == 0 || strcmp(BurnDrvGetTextA(DRV_NAME), "drgw2j") == 0;
	if (strncmp(BurnDrvGetTextA(DRV_NAME), "kov2", 4) == 0)
	{
		kov2 = 1;
	}
	
	pgmGetRoms(false);

	pgmMemIndex();
	int nLen = MemEnd - (unsigned char *)0;
	if ((Mem = (unsigned char *)malloc(nLen)) == NULL) return 1;
	memset(Mem, 0, nLen);
	pgmMemIndex();

	bPgmUseCache = true;
	
	if (bPgmUseCache) {

		
		extern char szAppCachePath[];
		
		strcpy(filePathName, szAppCachePath);
		strcat(filePathName, BurnDrvGetTextA(DRV_NAME));
		strcat(filePathName, "_LB");
		bPgmCreateCache = false;
		cacheFile = sceIoOpen( filePathName, PSP_O_RDONLY, 0777);
		if (cacheFile<0)
		{
			bPgmCreateCache = true;
			cacheFile = sceIoOpen( filePathName, PSP_O_WRONLY|PSP_O_CREAT, 0777 );
		}else if(sceIoLseek(cacheFile,0,SEEK_END)!=(nPGMTileROMLen+nPGMSPRColROMLen+nPGMSPRMaskROMLen+nPGMSNDROMLen))
		{
			bPgmCreateCache = true;
			sceIoClose(cacheFile);
			cacheFile = sceIoOpen( filePathName, PSP_O_WRONLY|PSP_O_TRUNC, 0777 );
		}
		if(bPgmCreateCache)
		{
			if ((uniCacheHead = (unsigned char *)malloc(0x0A00000)) == NULL) return 1;
			memset(uniCacheHead, 0, 0x0A00000);
		}
	} else {
			
#ifndef PGM_LOW_MEMORY
	PGMTileROMExp   = (unsigned char*)malloc((nPGMTileROMLen / 5) * 8);	// Expanded 8x8 Text Tiles and 32x32 BG Tiles
#endif		
		PGMTileROM      = (unsigned char*)malloc(nPGMTileROMLen);			// 8x8 Text Tiles + 32x32 BG Tiles	
		PGMSPRColROM	= (unsigned char*)malloc(nPGMSPRColROMLen);
		PGMSPRMaskROM	= (unsigned char*)malloc(nPGMSPRMaskROMLen);
		memset(PGMTileROM, 0, nPGMTileROMLen);
		memset(PGMSPRColROM, 0, nPGMSPRColROMLen);
		memset(PGMSPRMaskROM, 0, nPGMSPRMaskROMLen);
	}


	pgmGetRoms(true);
	
	if (bPgmUseCache) {
		if ( bPgmCreateCache ) {
			free(uniCacheHead);
			uniCacheHead=NULL;
			sceIoClose( cacheFile );
			cacheFile = sceIoOpen( filePathName,PSP_O_RDONLY, 0777);
		}
	}
	
	// load bios roms
	BurnLoadRom(PGM68KBIOS,		0x00080, 1);	// 68k bios
	//BurnLoadRom(PGMTileROM,		0x00081, 1);	// Bios Text and Tiles

		
	if (bPgmUseCache) {
		//Init cacheIndex
		initCacheStructure(0.95);

	}
#ifndef PGM_LOW_MEMORY
	// expand gfx1 into gfx2
	expand_gfx_2();
#endif

//	printf("Main %08x  Tile %08x  Col %08x  Mask %08x\n", nLen, nPGMTileROMLen, nPGMSPRColROMLen, nPGMSPRMaskROMLen );


	if (pPgmInitCallback) {
		pPgmInitCallback();
	}

	{
		SekInit(0, 0x68000);										// Allocate 68000
	    SekOpen(0);

			SekMapMemory(PGM68KBIOS,		0x000000, 0x01ffff, SM_ROM);				// 68000 BIOS
		SekMapMemory(PGM68KROM,			0x100000, (nPGM68KROMLen-1)+0x100000, SM_ROM);				// 68000 ROM

		SekMapMemory(Ram68K,			0x800000, 0x81ffff, SM_RAM);				// Main Ram
		SekMapMemory(Ram68K,			0x820000, 0x83ffff, SM_RAM);				// Mirrors...
		SekMapMemory(Ram68K,			0x840000, 0x85ffff, SM_RAM);
		SekMapMemory(Ram68K,			0x860000, 0x87ffff, SM_RAM);
		SekMapMemory(Ram68K,			0x880000, 0x89ffff, SM_RAM);
		SekMapMemory(Ram68K,			0x8a0000, 0x8bffff, SM_RAM);
		SekMapMemory(Ram68K,			0x8c0000, 0x8dffff, SM_RAM);
		SekMapMemory(Ram68K,			0x8e0000, 0x8fffff, SM_RAM);

		SekMapMemory((unsigned char *)RamBg,	0x900000, 0x903fff, SM_RAM);
		SekMapMemory((unsigned char *)RamTx,	0x904000, 0x905fff, SM_RAM);
		SekMapMemory((unsigned char *)RamRs,	0x907000, 0x9077ff, SM_RAM);
		SekMapMemory((unsigned char *)RamPal,	0xa00000, 0xa011ff, SM_RAM);
		SekMapMemory((unsigned char *)RamVReg,	0xb00000, 0xb0ffff, SM_RAM);

		SekMapHandler(2,			0xc10000, 0xc1ffff, SM_READ | SM_WRITE);

		SekSetReadWordHandler(0, PgmReadWord);
		SekSetReadByteHandler(0, PgmReadByte);
		SekSetWriteWordHandler(0, PgmWriteWord);
		SekSetWriteByteHandler(0, PgmWriteByte);

		SekSetReadWordHandler(2, PgmZ80ReadWord);
		SekSetWriteWordHandler(2, PgmZ80WriteWord);

		SekClose();
	}

	{
		ZetInit(1);
		ZetOpen(0);
		ZetMapArea(0x0000, 0xffff, 0, RamZ80);
		ZetMapArea(0x0000, 0xffff, 1, RamZ80);
		ZetMapArea(0x0000, 0xffff, 2, RamZ80);
		ZetSetOutHandler(PgmZ80PortWrite);
		ZetSetInHandler(PgmZ80PortRead);
		ZetMemEnd();
		ZetClose();
	}

	if (BurnDrvGetHardwareCode() & HARDWARE_IGS_USE_ARM_CPU)
	{
		nEnableArm7 = 1;

		install_asic27A_protection();
	}

	ics2115_init();

	PgmDoReset();
	
	return 0;
}

int pgmExit()
{
	SekExit();
	ZetExit();
	
	if (nEnableArm7) {
		Arm7Exit();
	}
	
	free(Mem);
	Mem = NULL;

	ics2115_exit();

	free (PGMTileROM);
#ifndef PGM_LOW_MEMORY
	free (PGMTileROMExp);
#endif

	if (!bPgmUseCache) {
		free (PGMSPRColROM);
		free (PGMSPRMaskROM);
	}

	PGM68KBIOS = NULL;
	PGM68KROM = NULL;
	PGMTileROM = NULL;
	PGMTileROMExp = NULL;
	PGMSPRColROM = NULL;
	PGMSPRMaskROM = NULL;

	// ics2115_exit can free and nil it
	ICSSNDROM = NULL;

	nPGM68KROMLen = 0;
	nPGMTileROMLen = 0;
	nPGMSPRColROMLen = 0;
	nPGMSPRMaskROMLen = 0;
	nPGMSNDROMLen = 0;

	pPgmInitCallback = NULL;
	pPgmScanCallback = NULL;
	kov2 = 0;
	nEnableArm7 = 0;
	destroyUniCache();

	return 0;
}

inline static void PgmClearOpposites(unsigned char* nJoystickInputs)
{
	if ((*nJoystickInputs & 0x06) == 0x06) {
		*nJoystickInputs &= ~0x06;
	}
	if ((*nJoystickInputs & 0x18) == 0x18) {
		*nJoystickInputs &= ~0x18;
	}
}

int pgmFrame()
{
	if (PgmReset) 
		PgmDoReset();
	
	if (nPgmPalRecalc) {
		for (int i=0;i<(0x1200/2);i++)
			RamCurPal[i] = CalcCol(RamPal[i]);
		nPgmPalRecalc = 0;
	}

	// Compile digital inputs
	PgmInput[0] = 0x0000;
	PgmInput[1] = 0x0000;
	PgmInput[2] = 0x0000;
	PgmInput[3] = 0x0000;
	PgmInput[4] = 0x0000;
	PgmInput[5] = 0x0000;
	for (int i = 0; i < 8; i++) {
		PgmInput[0] |= (PgmJoy1[i] & 1) << i;
		PgmInput[1] |= (PgmJoy2[i] & 1) << i;
		PgmInput[2] |= (PgmJoy3[i] & 1) << i;
		PgmInput[3] |= (PgmJoy4[i] & 1) << i;
		PgmInput[4] |= (PgmBtn1[i] & 1) << i;
		PgmInput[5] |= (PgmBtn2[i] & 1) << i;
	}	

	PgmClearOpposites(&PgmInput[0]);
	PgmClearOpposites(&PgmInput[1]);
	PgmClearOpposites(&PgmInput[2]);
	PgmClearOpposites(&PgmInput[3]);

	int nCyclesNext[3] = {0, 0, 0};
	nCyclesDone[0] = 0;
	nCyclesDone[1] = 0;
	nCyclesDone[2] = 0;

	SekNewFrame();
	ZetNewFrame();
	Arm7NewFrame();
	if (nEnableArm7) {
		Arm7Open(0);

		PGMARMShareRAM[0x138] = PgmInput[7]; // region hack
	}
	SekOpen(0);
	//ZetOpen(0);

	for(int i=0; i<PGM_INTER_LEAVE; i++) {

		nCyclesNext[0] += M68K_CYCS_PER_INTER;
		nCyclesNext[1] += Z80_CYCS_PER_INTER;
		nCyclesNext[2] += ARM7_CYCS_PER_INTER;

		int cycles = nCyclesNext[0] - nCyclesDone[0];

		if (cycles > 0) {
			nCyclesDone[0] += SekRun(cycles);
		}

		cycles = nCyclesNext[2] - nCyclesDone[2];

		if (cycles > 0 && nEnableArm7) {
			nCyclesDone[2] += Arm7Run(cycles);
		}

		if ( nPgmZ80Work ) {
			nCyclesDone[1] += ZetRun( nCyclesNext[1] - nCyclesDone[1] );
		} else
			nCyclesDone[1] += nCyclesNext[1] - nCyclesDone[1];
	}

	if ( bGameDrgw2 ) {
		SekSetIRQLine(6, SEK_IRQSTATUS_AUTO);
		SekRun(nCyclesNext[0] - nCyclesDone[0]);
		SekSetIRQLine(4, SEK_IRQSTATUS_AUTO);
		SekRun(nCyclesNext[0] - nCyclesDone[0]);
	} else {
		SekSetIRQLine(6, SEK_IRQSTATUS_AUTO);
	}

	ics2115_frame();

	if (nEnableArm7) {
		Arm7Close();
	}
	SekClose();
	//ZetClose();

	ics2115_update(nBurnSoundLen);

	if (pBurnDraw) pgmDraw();
	
	return 0;
}

int pgmScan(int nAction,int *pnMin)
{
	struct BurnArea ba;

	if (pnMin) {						// Return minimum compatible version
		*pnMin =  0x029702;
	}

	if (nAction & ACB_MEMORY_ROM) {						// Scan memory rom
		ba.Data		= PGM68KBIOS;
		ba.nLen		= 0x0020000;
		ba.nAddress = 0;
		ba.szName	= "BIOS ROM";
		BurnAcb(&ba);

		ba.Data		= PGM68KROM;
		ba.nLen		= nPGM68KROMLen;
		ba.nAddress = 0x100000;
		ba.szName	= "68K ROM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_MEMORY_RAM) {						// Scan memory, devices & variables
		ba.Data		= RamBg;
		ba.nLen		= 0x0004000;
		ba.nAddress = 0x900000;
		ba.szName	= "Bg RAM";
		BurnAcb(&ba);

		ba.Data		= RamTx;
		ba.nLen		= 0x0002000;
		ba.nAddress = 0x904000;
		ba.szName	= "Tx RAM";
		BurnAcb(&ba);

		ba.Data		= RamRs;
		ba.nLen		= 0x0000800;
		ba.nAddress = 0x907000;
		ba.szName	= "Row Scroll";
		BurnAcb(&ba);

		ba.Data		= RamPal;
		ba.nLen		= 0x0001200;
		ba.nAddress = 0xA00000;
		ba.szName	= "Palette";
		BurnAcb(&ba);

		ba.Data		= RamVReg;
		ba.nLen		= 0x0010000;
		ba.nAddress = 0xB00000;
		ba.szName	= "Video Regs";
		BurnAcb(&ba);

		ba.Data		= RamZ80;
		ba.nLen		= 0x0010000;
		ba.nAddress = 0xC10000;
		ba.szName	= "Z80 RAM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_NVRAM) {								// Scan nvram
		ba.Data		= Ram68K;
		ba.nLen		= 0x020000;
		ba.nAddress	= 0x800000;
		ba.szName	= "68K RAM";
		BurnAcb(&ba);
	}

	if (nAction & ACB_DRIVER_DATA) {

		SekScan(nAction);										// Scan 68000 state
		ZetScan(nAction);									// Scan Z80 state

		SCAN_VAR(PgmInput);

		SCAN_VAR(nPgmZ80Work);
	//	ics2115_scan(nAction, pnMin);

		if (pPgmScanCallback) {
			pPgmScanCallback(nAction, pnMin);
		}
	}

 	return 0;
}
