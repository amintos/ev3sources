#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lmstypes.h>
#include <lms2012.h>
#include <bytecodes.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>

#define VOLUME 10

#define STEP_SIZE_TABLE_ENTRIES 89
#define INDEX_TABLE_ENTRIES     16

enum
{
  SOUND_STOPPED,
  SOUND_SETUP_FILE,
  SOUND_FILE_PLAYING,
  SOUND_FILE_LOOPING,
  SOUND_TONE_PLAYING,
  SOUND_TONE_LOOPING
}SOUND_STATES;

#define SND_LEVEL_1   13  // 13% (12.5)
#define SND_LEVEL_2   25  // 25%
#define SND_LEVEL_3   38  // 38% (37.5)
#define SND_LEVEL_4   50  // 50%
#define SND_LEVEL_5   63  // 63% (62.5)
#define SND_LEVEL_6   75  // 75%
#define SND_LEVEL_7   88  // 88% (87.5)

#define TONE_LEVEL_1    8  //  8%
#define TONE_LEVEL_2   16  // 16%
#define TONE_LEVEL_3   24  // 24%
#define TONE_LEVEL_4   32  // 32%
#define TONE_LEVEL_5   40  // 40%
#define TONE_LEVEL_6   48  // 48%
#define TONE_LEVEL_7   56  // 56%
#define TONE_LEVEL_8   64  // 64%
#define TONE_LEVEL_9   72  // 72%
#define TONE_LEVEL_10  80  // 80%
#define TONE_LEVEL_11  88  // 88%
#define TONE_LEVEL_12  96  // 96%

const SWORD StepSizeTable[STEP_SIZE_TABLE_ENTRIES] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
        19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    };

const SWORD IndexTable[INDEX_TABLE_ENTRIES] = {
     -1, -1, -1, -1, 2, 4, 6, 8,
     -1, -1, -1, -1, 2, 4, 6, 8
   };

#define FILEFORMAT_RAW_SOUND      0x0100
#define FILEFORMAT_ADPCM_SOUND    0x0101
#define SOUND_MODE_ONCE           0x00
#define SOUND_LOOP                0x01
#define SOUND_ADPCM_INIT_VALPREV  0x7F
#define SOUND_ADPCM_INIT_INDEX    20

typedef struct
{
  //*****************************************************************************
  // Sound Global variables
  //*****************************************************************************

  int     SoundDriverDescriptor;
  int     hSoundFile;

  DATA8   SoundOwner;
  DATA8   cSoundState;
  SOUND   Sound;
  SOUND	  *pSound;
  UWORD	  BytesLeft;
  UWORD	  SoundFileFormat;
  UWORD	  SoundDataLength;
  UWORD	  SoundSampleRate;
  UWORD	  SoundPlayMode;
  UWORD   SoundFileLength;
  SWORD   ValPrev;
  SWORD   Index;
  SWORD   Step;
  UBYTE   BytesToWrite;
  char    PathBuffer[MAX_FILENAME_SIZE];
  struct  stat FileStatus;
  UBYTE   SoundData[SOUND_FILE_BUFFER_SIZE + 1]; // Add up for CMD
}
SOUND_GLOBALS;

SOUND_GLOBALS SoundInstance;

RESULT    cSoundInit(void)
{
  RESULT  Result = FAIL;
  SOUND  *pSoundTmp;
  int	  SndFile;

  SoundInstance.SoundDriverDescriptor = -1;
  SoundInstance.hSoundFile            = -1;
  SoundInstance.pSound  =  &SoundInstance.Sound;

  // Create a Shared Memory entry for signaling the driver state BUSY or NOT BUSY

  SndFile = open(SOUND_DEVICE_NAME,O_RDWR | O_SYNC);

  if(SndFile >= 0)
  {
    pSoundTmp  =  (SOUND*)mmap(0, sizeof(UWORD), PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, SndFile, 0);

    if(pSoundTmp == MAP_FAILED)
    {
    }
    else
    {
      SoundInstance.pSound = pSoundTmp;
      Result  =  OK;
    }

    close(SndFile);
  }

  return (Result);
}

void cSoundInitAdPcm(void)
{
  // Reset ADPCM values to a known and initial value
  SoundInstance.ValPrev = SOUND_ADPCM_INIT_VALPREV;
  SoundInstance.Index = SOUND_ADPCM_INIT_INDEX;
  SoundInstance.Step = StepSizeTable[SoundInstance.Index];

}

void      cSoundEntry(int Cmd, UWORD Temp1, DATA8 *pFileName, UWORD Frequency,
UWORD Duration)
{
    UBYTE   Loop = FALSE;

    UWORD	BytesToWrite;
    UWORD	BytesWritten = 0;
    DATA8   SoundData[SOUND_FILE_BUFFER_SIZE + 1]; // Add up for CMD

    char    PathName[MAX_FILENAME_SIZE];
    UBYTE   Tmp1;
    UBYTE   Tmp2;

    SoundData[0] = Cmd;	// General for all commands :-)
    BytesToWrite = 0;

    switch(Cmd)
    {

      case TONE:      (*SoundInstance.pSound).Status = BUSY;

                      // Scale the volume from 1-100% into 13 level steps
											// Could be linear but prepared for speaker and -box adjustments... :-)
											if(Temp1 > 0)
											{
											  if(Temp1 > TONE_LEVEL_6)           // >  48%
											  {
											    if(Temp1 > TONE_LEVEL_9)         // >  72%
											    {
											      if(Temp1 > TONE_LEVEL_11)      // >  88%
											      {
											        if(Temp1 > TONE_LEVEL_12)    // >  96%
											        {
											          SoundData[1] = 13;            // => 100%
											        }
											        else
											        {
											          SoundData[1] = 12;            // => 100%
											        }
											      }
											      else
											      {
											        if(Temp1 > TONE_LEVEL_10)    // >  96%
											        {
											          SoundData[1] = 11;            // => 100%
											        }
											        else
											        {
											          SoundData[1] = 10;            // => 100%
											        }
											      }
											    }
											    else
											    {
											      if(Temp1 > TONE_LEVEL_8)       // >  62.5%
											      {
											        SoundData[1] = 9;           // => 75%
											      }
											      else
											      {
											        if(Temp1 > TONE_LEVEL_7)
											        {
											          SoundData[1] = 8;           // => 62.5%
											        }
											        else
											        {
											          SoundData[1] = 7;           // => 62.5%
											        }
											      }
											    }
											  }
											  else
											  {
											    if(Temp1 > TONE_LEVEL_3)         // >  25%
											    {
											      if(Temp1 > TONE_LEVEL_5)       // >  37.5%
											      {
											        SoundData[1] = 6;           // => 50%
											      }
											      else
											      {
											        if(Temp1 > TONE_LEVEL_4)       // >  37.5%
											        {
											          SoundData[1] = 5;           // => 37.5%
											        }
											        else
											        {
											          SoundData[1] = 4;
											        }
											      }
											    }
											    else
											    {
											      if(Temp1 > TONE_LEVEL_2)       // >  12.5%
											      {
											        SoundData[1] = 3;
											      }
											      else
											      {
											        if(Temp1 > TONE_LEVEL_1)
											        {
											          SoundData[1] = 2;           // => 25%
											        }
											        else
											        {
											          SoundData[1] = 1;           // => 25%
											        }
											      }
											    }											                           											                        }
											  }
											  else
											    SoundData[1] = 0;

                      SoundData[2] = (UBYTE)(Frequency);
                      SoundData[3] = (UBYTE)(Frequency >> 8);
                      SoundData[4] = (UBYTE)(Duration);
                      SoundData[5] = (UBYTE)(Duration >> 8);
                      BytesToWrite = 6;
                      SoundInstance.cSoundState = SOUND_TONE_PLAYING;
                      break;

      case BREAK:     //SoundData[0] = Cmd;
                      BytesToWrite = 1;
                      SoundInstance.cSoundState = SOUND_STOPPED;

                      if (SoundInstance.hSoundFile >= 0)
                      {
                        close(SoundInstance.hSoundFile);
                        SoundInstance.hSoundFile  = -1;
                      }

                      break;

      case REPEAT: 	  Loop = TRUE;
                      SoundData[0] = PLAY;  // Yes, but looping :-)
                      // Fall through

      case PLAY:	    // If SoundFile is Flood filled, we must politely
                      // close the active handle - else we acts as a "BUG"
                      // eating all the crops (handles) ;-)

                      SoundInstance.cSoundState = SOUND_STOPPED;  // Yes but only shortly

                      if (SoundInstance.hSoundFile >= 0)  // An active handle?
                      {
                        close(SoundInstance.hSoundFile);  // No more use
                        SoundInstance.hSoundFile  = -1;   // Signal it
                      }

                      (*SoundInstance.pSound).Status = BUSY;

    	  	  	  	  	// Scale the volume from 1-100% into 1 - 8 level steps
    	  	  	  	  	// Could be linear but prepared for speaker and -box adjustments... :-)
                      if(Temp1 > 0)
                      {
                        if(Temp1 > SND_LEVEL_4)           // >  50%
                        {
                          if(Temp1 > SND_LEVEL_6)         // >  75%
                          {
                            if(Temp1 > SND_LEVEL_7)       // >  87.5%
                            {
                              SoundData[1] = 8;           // => 100%
                            }
                            else
                            {
                              SoundData[1] = 7;           // => 87.5%
                            }
                          }
                          else
                          {
                            if(Temp1 > SND_LEVEL_5)       // >  62.5%
                            {
                              SoundData[1] = 6;           // => 75%
                            }
                            else
                            {
                              SoundData[1] = 5;           // => 62.5%
                            }
                          }
                        }
                        else
                        {
                          if(Temp1 > SND_LEVEL_2)         // >  25%
                          {
                            if(Temp1 > SND_LEVEL_3)       // >  37.5%
                            {
                              SoundData[1] = 4;           // => 50%
                            }
                            else
                            {
                              SoundData[1] = 3;           // => 37.5%
                            }
                          }
                          else
                          {
                            if(Temp1 > SND_LEVEL_1)       // >  12.5%
                            {
                              SoundData[1] = 2;           // => 25%
                            }
                            else
                            {
                              SoundData[1] = 1;           // => 12.5%
                            }
                          }
                        }
                      }
                      else
                        SoundData[1] = 0;

    	  	  	  	  	BytesToWrite = 2;

    	  	  	  	  	if(pFileName != NULL) // We should have a valid filename
    	  	  	  	    {
    	  	  	  	  	  // Get Path and concatenate

    	  	  	  	  	  PathName[0]  =  0;
    	  	  	  	  	  if (pFileName[0] != '.')
    	  	  	  	  	  {
    	  	  	  	  	  //GetResourcePath(PathName, MAX_FILENAME_SIZE);
                          sprintf(SoundInstance.PathBuffer, "%s%s.rsf", (char*)PathName, (char*)pFileName);
    	  	  	  	  	  }
    	  	  	  	  	  else
    	  	  	  	  	  {
    	  	  	  	  	    sprintf(SoundInstance.PathBuffer, "%s.rsf", (char*)pFileName);
    	  	  	  	  	  }

    	  	  	  	  	  // Open SoundFile

    	  	  	  	  	  SoundInstance.hSoundFile  =  open(SoundInstance.PathBuffer,O_RDONLY,0666);

                        if(SoundInstance.hSoundFile >= 0)
                        {
                          // Get actual FileSize
                          stat(SoundInstance.PathBuffer,&SoundInstance.FileStatus);
                          SoundInstance.SoundFileLength   =  SoundInstance.FileStatus.st_size;

                          // BIG Endianess

                          read(SoundInstance.hSoundFile,&Tmp1,1);
                          read(SoundInstance.hSoundFile,&Tmp2,1);
                          SoundInstance.SoundFileFormat   =  (UWORD)Tmp1 << 8 | (UWORD)Tmp2;

                          read(SoundInstance.hSoundFile,&Tmp1,1);
                          read(SoundInstance.hSoundFile,&Tmp2,1);
                          SoundInstance.SoundDataLength   =  (UWORD)Tmp1 << 8 | (UWORD)Tmp2;

                          read(SoundInstance.hSoundFile,&Tmp1,1);
                          read(SoundInstance.hSoundFile,&Tmp2,1);
                          SoundInstance.SoundSampleRate   =  (UWORD)Tmp1 << 8 | (UWORD)Tmp2;

                          read(SoundInstance.hSoundFile,&Tmp1,1);
                          read(SoundInstance.hSoundFile,&Tmp2,1);
                          SoundInstance.SoundPlayMode     =  (UWORD)Tmp1 << 8 | (UWORD)Tmp2;

                          SoundInstance.cSoundState       =  SOUND_SETUP_FILE;

                          if(SoundInstance.SoundFileFormat == FILEFORMAT_ADPCM_SOUND)
                            cSoundInitAdPcm();
                        }


    	  	  	  	    }
    	  	  	  	    else
    	  	  	  	    {
    	  	  	  	      //Do some ERROR-handling :-)
    	  	  	  	      //NOT a valid name from above :-(
    	  	  	  	    }

    	  	  	  break;

      default:  BytesToWrite = 0; // An non-valid entry
                break;
    }

    if(BytesToWrite > 0)
    {
      SoundInstance.SoundDriverDescriptor = open(SOUND_DEVICE_NAME, O_WRONLY);
      if (SoundInstance.SoundDriverDescriptor >= 0)
      {
        BytesWritten = write(SoundInstance.SoundDriverDescriptor, SoundData, BytesToWrite);
        close(SoundInstance.SoundDriverDescriptor);
        SoundInstance.SoundDriverDescriptor = -1;

        if (SoundInstance.cSoundState == SOUND_SETUP_FILE)  // The one and only situation
        {
          SoundInstance.BytesToWrite = 0;                   // Reset
          if(TRUE == Loop)
            SoundInstance.cSoundState = SOUND_FILE_LOOPING;
          else
            SoundInstance.cSoundState = SOUND_FILE_PLAYING;
        }
      }
      else
        SoundInstance.cSoundState = SOUND_STOPPED;          // Couldn't do the job :-(
    }
    else
    {
      BytesToWrite  =  BytesWritten;
      BytesToWrite  =  0;
    }
}

void msleep(int msec) {
    struct timespec request, remain;
    request.tv_sec = 0;
    request.tv_nsec = msec * 1000000;
    clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remain);
}

int main(int argc, char** argv) {
    
    int i;
    cSoundInit();
    for(i = 0; i < 15; i++) {
        cSoundEntry(TONE, VOLUME, NULL, 720, 100);
        msleep(100);
        cSoundEntry(TONE, VOLUME, NULL, 780, 100);
        msleep(100);
    }

    return 0;

}
