#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lmstypes.h>
#include <bytecodes.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#define   MOTOR_DEVICE_NAME     "/dev/lms_motor"    //!< TACHO device file name
#define   PWM_DEVICE_NAME       "/dev/lms_pwm"        //!< PWM device file name
#define OUTPUTS 4   // Determined by grepping for vmOUTPUTS

typedef   enum
{
  NOBREAK       = 0x0100,               //!< Dispatcher running (looping)
  STOPBREAK     = 0x0200,               //!< Break because of program stop
  SLEEPBREAK    = 0x0400,               //!< Break because of sleeping
  INSTRBREAK    = 0x0800,               //!< Break because of opcode break
  BUSYBREAK     = 0x1000,               //!< Break because of waiting for completion
  PRGBREAK      = 0x2000,               //!< Break because of program break
  USERBREAK     = 0x4000,               //!< Break because of user decision
  FAILBREAK     = 0x8000                //!< Break because of fail
}
DSPSTAT;

typedef struct
{
  SLONG TachoCounts;
  SBYTE Speed;
  SLONG TachoSensor;
}MOTORDATA;

typedef struct
{
  //*************************
  // Output Global variables
  //*************************

  DATA8       OutputType[OUTPUTS];
  OBJID       Owner[OUTPUTS];

  int         PwmFile;
  int         MotorFile;

  MOTORDATA   MotorData[OUTPUTS];
  MOTORDATA   *pMotor;
}
OUTPUT_GLOBALS;

OUTPUT_GLOBALS OutputInstance;

void      OutputReset(void)
{
  UBYTE   Tmp;
  DATA8   StopArr[3];

  for(Tmp = 0; Tmp < OUTPUTS; Tmp++)
  {
    OutputInstance.Owner[Tmp] = 0;
  }

  StopArr[0] = (DATA8)opOUTPUT_STOP;
  StopArr[1] = 0x0F;
  StopArr[2] = 0x00;
  if (OutputInstance.PwmFile >= 0)
  {
    write(OutputInstance.PwmFile,StopArr,3);
  }
}


RESULT    cOutputOpen(void)
{
  RESULT  Result = FAIL;
  UBYTE   PrgStart =  opPROGRAM_START;
  OutputReset();

  if (OutputInstance.PwmFile >= 0)
  {
    write(OutputInstance.PwmFile,&PrgStart,1);
  }

  Result  =  OK;

  return (Result);
}


int cOutputInit(void) {

  RESULT      Result = FAIL;
  MOTORDATA   *pTmp;

  // To ensure that pMotor is never uninitialised
  OutputInstance.pMotor  =  OutputInstance.MotorData;

  // Open the handle for writing commands
  OutputInstance.PwmFile  =  open(PWM_DEVICE_NAME,O_RDWR);

  if (OutputInstance.PwmFile >= 0)
  {
    // Open the handle for reading motor values - shared memory
    OutputInstance.MotorFile  =  open(MOTOR_DEVICE_NAME,O_RDWR | O_SYNC);
    if (OutputInstance.MotorFile >= 0)
    {
      pTmp  =  (MOTORDATA*)mmap(0, sizeof(OutputInstance.MotorData), PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, OutputInstance.MotorFile, 0);

      if (pTmp == MAP_FAILED)
      {
        close(OutputInstance.MotorFile);
        close(OutputInstance.PwmFile);
      }
      else
      {
        OutputInstance.pMotor  =  (MOTORDATA*)pTmp;
        Result  =  cOutputOpen();
      }
    }
  }

  return (Result);
}

/*! \page cOutput
 *  <hr size="1"/>
 *  <b>     opOUTPUT_POWER (LAYER, NOS, SPEED)  </b>
 *
 *- Set power of the outputs\n
 *- Dispatch status unchanged
 *
 *  \param  (DATA8)   LAYER   - Chain layer number [0..3]
 *  \param  (DATA8)   NOS     - Output bit field [0x00..0x0F]
 *  \param  (DATA8)   POWER   - Power [-100..100%]
 */
void      cOutputPower(DATA8 Layer, DATA8 Nos, DATA8 Power)
{
  DATA8   SetPower[3];
  DATA8   Len;

  Len    =  0;

  SetPower[0] = (DATA8)opOUTPUT_POWER;
  SetPower[1] = Nos;
  SetPower[2] = Power;
  if (OutputInstance.PwmFile >= 0)
  {
    write(OutputInstance.PwmFile,SetPower,sizeof(SetPower));
  }
  
}

/*! \page cOutput
 *  <hr size="1"/>
 *  <b>     opOUTPUT_START (LAYER, NOS)  </b>
 *
 *- Starts the outputs\n
 *- Dispatch status unchanged
 *
 *  \param  (DATA8)   LAYER   - Chain layer number [0..3]
 *  \param  (DATA8)   NOS     - Output bit field [0x00..0x0F]
 */
void      cOutputStart(DATA8 Layer, DATA8 Nos)
{
  DATA8   Tmp;
  DATA8   Len;

  DATA8   StartMotor[2];

  printf("Checkpoint 1\n");
  Len    =  0;

  if (Layer == 0)
  {
    printf("Checkpoint 2\n");
    StartMotor[0] = (DATA8)opOUTPUT_START;
    StartMotor[1] = Nos;
    printf("Checkpoint 3\n");
    if (OutputInstance.PwmFile >= 0)
    {
      printf("Checkpoint 4\n");
      write(OutputInstance.PwmFile,StartMotor,sizeof(StartMotor));

      for (Tmp = 0; Tmp < OUTPUTS; Tmp++)
      {
        if (Nos & (0x01 << Tmp))
        {
        }
      }
      printf("Checkpoint 5\n");
    }
  }
}

/*! \page cOutput
 *  <hr size="1"/>
 *  <b>     opOUTPUT_STOP (LAYER, NOS)  </b>
 *
 *- Stops the outputs\n
 *- Dispatch status unchanged
 *
 *  \param  (DATA8)   LAYER   - Chain layer number [0..3]
 *  \param  (DATA8)   NOS     - Output bit field [0x00..0x0F]
 *  \param  (DATA8)   BRAKE   - Brake [0,1]
 */
void      cOutputStop(DATA8 Layer, DATA8 Nos, DATA8 Brake)
{
  UBYTE   Len;

  DATA8   StopArr[3];

  Len    =  0;

  if (Layer == 0)
  {
    StopArr[0] = (DATA8)opOUTPUT_STOP;
    StopArr[1] = Nos;
    StopArr[2] = Brake;

    if (OutputInstance.PwmFile >= 0)
    {
      write(OutputInstance.PwmFile,StopArr,sizeof(StopArr));
    }
  }
  
}

int main(int argc, char** argv) {

    cOutputInit();

    if(argc < 2) {
        exit(1);
    }

    if(strcmp(argv[1], "start") == 0) {
	printf("Starting the motor!\n");
        cOutputPower(0, 0x0, 100);

        //cOutputStart(0, 0);
        //cOutputStart(0, 0x01);
        //cOutputStart(0, 0x01 << 1);
        //cOutputStart(0, 0x01 << 2);

    } else if(strcmp(argv[1], "stop") == 0) {
	printf("Stopping the motor!\n");
        cOutputStop(0, 0x1,1);
    }

    //OutputReset();

    return 0;

}
