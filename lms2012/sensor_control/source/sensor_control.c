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
#include <sys/statvfs.h>
#include <math.h>

#define   MAX_DEVICE_DATASETS   8                     //!< Max number of data sets in one device
#define   MAX_DEVICE_DATALENGTH 32                    //!< Max device data length

#define   ANALOG_DEVICE         "lms_analog"          //!< ANALOG device name
#define   ANALOG_DEVICE_NAME    "/dev/lms_analog"     //!< ANALOG device file name

#define   UART_DEVICE           "lms_uart"            //!< UART device name
#define   UART_DEVICE_NAME      "/dev/lms_uart"       //!< UART device file name

#define   IIC_DEVICE            "lms_iic"             //!< IIC device name
#define   IIC_DEVICE_NAME       "/dev/lms_iic"        //!< IIC device

#define   INPUT_PORTS                   INPUTS
#define   INPUT_DEVICES                 (INPUT_PORTS * CHAIN_DEPT)

#define   OUTPUT_PORTS                  OUTPUTS
#define   OUTPUT_DEVICES                (OUTPUT_PORTS * CHAIN_DEPT)

#define   DEVICES                       (INPUT_DEVICES + OUTPUT_DEVICES)

#define   INPUT_VALUES                  (INPUT_PORTS * 3)
#define   INPUT_VALUE_SIZE              5
#define   INPUT_BUFFER_SIZE             (INPUT_VALUES * INPUT_VALUE_SIZE)
#define   INPUT_SIZE                    (INPUT_VALUES * 2)


typedef   struct
{
  UWORD   InvalidTime;                        //!< mS from type change to valid data
  UWORD   TimeoutTimer;                       //!< mS allowed to be busy timer
  UWORD   TypeIndex;                          //!< Index to information in "TypeData" table
  DATA8   Connection;                         //!< Connection type (from DCM)
  OBJID   Owner;
  RESULT  DevStatus;
  DATA8   Busy;
  DATAF   Raw[MAX_DEVICE_DATASETS];           //!< Raw value (only updated when "cInputReadDeviceRaw" function is called)
#ifndef DISABLE_BUMBED
  DATAF   OldRaw;
  DATA32  Changes;
  DATA32  Bumps;
#endif
#ifdef Linux_X86
  UWORD   Timer;
  UBYTE   Dir;
#endif
}
DEVICE;

typedef   struct
{
  DATA8   InUse;
  DATAF   Min;
  DATAF   Max;
}
CALIB;

typedef struct
{
  //*****************************************************************************
  // Input Global variables
  //*****************************************************************************

  ANALOG    Analog;
  ANALOG    *pAnalog;

  UART      Uart;
  UART      *pUart;

  IIC       Iic;
  IIC       *pIic;

  int       UartFile;
  int       AdcFile;
  int       DcmFile;
  int       IicFile;

  DEVCON    DevCon;
  UARTCTL   UartCtl;
  IICCTL    IicCtl;
  IICDAT    IicDat;

  DATA32    InputNull;

  DATA8     TmpMode[INPUT_PORTS];

  DATA8     ConfigurationChanged[MAX_PROGRAMS];

  DATA8     DeviceType[DEVICES];              //!< Type of all devices - for easy upload
  DATA8     DeviceMode[DEVICES];              //!< Mode of all devices
  DEVICE    DeviceData[DEVICES];              //!< Data for all devices

  UWORD     NoneIndex;
  UWORD     UnknownIndex;
  DATA8     DCMUpdate;

  DATA8     TypeModes[MAX_DEVICE_TYPE + 1];   //!< No of modes for specific type

  UWORD     MaxDeviceTypes;                   //!< Number of device type/mode entries in tabel
  TYPES     *TypeData;                        //!< Type specific data
  UWORD     IicDeviceTypes;                   //!< Number of IIC device type/mode entries in tabel
  IICSTR    *IicString;
  IICSTR    IicStr;

  DATA16    TypeDataTimer;
  DATA16    TypeDataIndex;

  CALIB     Calib[MAX_DEVICE_TYPE][MAX_DEVICE_MODES];
} INPUT_GLOBALS;

INPUT_GLOBALS InputInstance;	

TYPES     TypeDefault[] =
{
//   Name                    Type                   Connection     Mode  DataSets  Format  Figures  Decimals  Views   RawMin  RawMax  PctMin  PctMax  SiMin   SiMax   Time   IdValue  Pins Symbol
  { "PORT ERROR"           , TYPE_ERROR           , CONN_ERROR   , 0   , 0       , 0     , 4    ,   0     ,   1   ,   0.0,    0.0,    0.0,    0.0,    0.0,    0.0,    0,     0,      'f', ""     },
  { "NONE"                 , TYPE_NONE            , CONN_NONE    , 0   , 0       , 0     , 4    ,   0     ,   1   ,   0.0,    0.0,    0.0,    0.0,    0.0,    0.0,    0,     0,      'f', ""     },
  { "UNKNOWN"              , TYPE_UNKNOWN         , CONN_UNKNOWN , 0   , 1       , 1     , 4    ,   0     ,   1   ,   0.0, 1023.0,    0.0,  100.0,    0.0, 1023.0,    0,     0,      'f', ""     },
  { "\0" }
};

//                DEVICE MAPPING
//
// Device         0   1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31
//
// Layer          0   0   0   0   1   1   1   1   2   2   2   2   3   3   3   3   0   0   0   0   1   1   1   1   2   2   2   2   3   3   3   3
// Port (INPUT)   0   1   2   3   0   1   2   3   0   1   2   3   0   1   2   3   16  17  18  19  16  17  18  19  16  17  18  19  16  17  18  19
// Port (OUTPUT)  -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   -   0   1   2   3   0   1   2   3   0   1   2   3   0   1   2   3
// Output         0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   0   1   1   1   1   1   1   1   1   1   1   1   1   1   1   1   1

#define   SENSOR_RESOLUTION             1023L

/* Remember this is ARM AD converter  - 3,3 VDC as max voltage      */
/* When in color mode background value is substracted => min = 0!!! */
#define   AD_MAX                        2703L
#define   AD_FS                         3300L

#define   COLORSENSORBGMIN              (214/(AD_FS/AD_MAX))
#define   COLORSENSORMIN                (1L/(AD_FS/AD_MAX)) /* 1 inserted else div 0 (1L/(120/AD_MAX)) */
#define   COLORSENSORMAX                ((AD_MAX * AD_FS)/3300)
#define   COLORSENSORPCTDYN             (UBYTE)(((COLORSENSORMAX - COLORSENSORMIN) * 100L)/AD_MAX)
#define   COLORSENSORBGPCTDYN           (UBYTE)(((COLORSENSORMAX - COLORSENSORBGMIN) * 100L)/AD_MAX)

#define   INSTALLED_MEMORY      6000                  //!< Flash allocated to hold user programs/data
#define   RESERVED_MEMORY       100                   //!< Memory reserve for system [KB]
#define   LOW_MEMORY            500                   //!< Low memory warning [KB]

void      cMemoryGetUsage(DATA32 *pTotal,DATA32 *pFree,DATA8 Force)
{

	struct  statvfs Status;
  DATA32  Used;
    if (statvfs(MEMORY_FOLDER,&Status) == 0)
    {
	Used  =  (DATA32)((((Status.f_blocks - Status.f_bavail) * Status.f_bsize) + (KB - 1)) / KB);
  	*pTotal   =  INSTALLED_MEMORY;
  	*pFree    =  INSTALLED_MEMORY - Used;
    } else {
	  *pTotal   =  INSTALLED_MEMORY;
	  *pFree    =  INSTALLED_MEMORY;
	}

}

RESULT    cMemoryRealloc(void *pOldMemory,void **ppMemory,DATA32 Size)
{
  RESULT  Result = FAIL;

  *ppMemory  =  realloc(pOldMemory,(size_t)Size);
  if (*ppMemory != NULL)
  {
    Result  =  OK;
  }

  return (Result);
}

RESULT    cMemoryMalloc(void **ppMemory, DATA32 Size)
{
  RESULT  Result = FAIL;
  DATA32  Total;
  DATA32  Free;

  cMemoryGetUsage(&Total,&Free,0);
  if (((Size + (KB - 1)) / KB) < (Free - RESERVED_MEMORY))
  {
    *ppMemory  =  malloc((size_t)Size);
    if (*ppMemory != NULL)
    {
      Result  =  OK;
    }
  }

  return (Result);
}

RESULT    cInputGetIicString(DATA8 Type,DATA8 Mode,IICSTR *IicStr)
{
  RESULT  Result    = FAIL;  // FAIL=full, OK=new, BUSY=found
  UWORD   Index     = 0;


  (*IicStr).SetupLng      =  0;
  (*IicStr).SetupString   =  0;
  (*IicStr).PollLng       =  0;
  (*IicStr).PollString    =  0;
  (*IicStr).ReadLng       =  0;

  if ((Type >= 0) && (Type < (MAX_DEVICE_TYPE + 1)) && (Mode >= 0) && (Mode < MAX_DEVICE_MODES))
  { // Type and mode valid


    while ((Index < InputInstance.IicDeviceTypes) && (Result != OK))
    { // trying to find device type

      if ((InputInstance.IicString[Index].Type == Type) && (InputInstance.IicString[Index].Mode == Mode))
      { // match on type and mode

        (*IicStr).Type          =  Type;
        (*IicStr).Mode          =  Mode;
        snprintf((char*)IicStr->Manufacturer, sizeof(IicStr->Manufacturer),"%s", (char*)InputInstance.IicString[Index].Manufacturer);
        snprintf((char*)IicStr->SensorType, sizeof(IicStr->SensorType), "%s",(char*)InputInstance.IicString[Index].SensorType);
        (*IicStr).SetupLng      =  InputInstance.IicString[Index].SetupLng;
        (*IicStr).SetupString   =  InputInstance.IicString[Index].SetupString;
        (*IicStr).PollLng       =  InputInstance.IicString[Index].PollLng;
        (*IicStr).PollString    =  InputInstance.IicString[Index].PollString;
        (*IicStr).ReadLng       =  InputInstance.IicString[Index].ReadLng;

        Result          =  OK;
      }
      Index++;
    }
  }

  return (Result);
}

RESULT    cInputFindDumbInputDevice(DATA8 Device,DATA8 Type,DATA8 Mode,UWORD *pTypeIndex)
{
  RESULT  Result = FAIL;
  UWORD   IdValue = 0;
  UWORD   Index = 0;
  UWORD   Tmp;

  // get actual id value
  IdValue  =  CtoV((*InputInstance.pAnalog).InPin1[Device]);

  while ((Index < InputInstance.MaxDeviceTypes) && (Result != OK))
  {
    Tmp  =  InputInstance.TypeData[Index].IdValue;

    if (Tmp >= IN1_ID_HYSTERESIS)
    {
      if ((IdValue >= (Tmp - IN1_ID_HYSTERESIS)) && (IdValue < (Tmp + IN1_ID_HYSTERESIS)))
      { // id value match

        if (Type == TYPE_UNKNOWN)
        { // first search

          // "type data" entry found
          *pTypeIndex  =  Index;

        }
        else
        { // next search

          if (Type == InputInstance.TypeData[Index].Type)
          { //

            *pTypeIndex  =  Index;
          }
        }
        if (Mode == InputInstance.TypeData[Index].Mode)
        { // mode match

          // "type data" entry found
          *pTypeIndex  =  Index;

          // skip looping
          Result  =  OK;
        }
      }
    }
    Index++;
  }

  return (Result);
}

RESULT    cInputFindDevice(DATA8 Type,DATA8 Mode,UWORD *pTypeIndex)
{
  RESULT  Result = FAIL;
  UWORD   Index = 0;

  while ((Index < InputInstance.MaxDeviceTypes) && (Result != OK))
  {
    if (Type == InputInstance.TypeData[Index].Type)
    { // type match

      *pTypeIndex  =  Index;

      if (Mode == InputInstance.TypeData[Index].Mode)
      { // mode match

        // "type data" entry found
        *pTypeIndex  =  Index;

        // skip looping
        Result  =  OK;
      }
    }
    Index++;
  }

  return (Result);
}


void      cInputResetDevice(DATA8 Device,UWORD TypeIndex)
{
  PRGID   TmpPrgId;

  InputInstance.DeviceType[Device]              =  InputInstance.TypeData[TypeIndex].Type;
  InputInstance.DeviceMode[Device]              =  InputInstance.TypeData[TypeIndex].Mode;

  InputInstance.DeviceData[Device].InvalidTime  =  InputInstance.TypeData[TypeIndex].InvalidTime;
  InputInstance.DeviceData[Device].DevStatus    =  BUSY;

  // save new type
  InputInstance.DeviceData[Device].TypeIndex    =  TypeIndex;

  if (InputInstance.DeviceType[Device] != TYPE_UNKNOWN)
  {
    // configuration changed
    for (TmpPrgId = 0;TmpPrgId < MAX_PROGRAMS;TmpPrgId++)
    {
      InputInstance.ConfigurationChanged[TmpPrgId]++;
    }
  }
}

RESULT    cInputExpandDevice(DATA8 Device,DATA8 *pLayer,DATA8 *pPort,DATA8 *pOutput)
{ // pPort: pOutput=0 -> 0..3 , pOutput=1 -> 0..3

  RESULT  Result = FAIL;

  if ((Device >= 0) && (Device < DEVICES))
  {
    if (Device >= INPUT_DEVICES)
    { // OUTPUT

      *pOutput  =  1;
      Device   -=  INPUT_DEVICES;
      *pPort    =  (Device % OUTPUT_PORTS);
      *pPort   +=  INPUT_DEVICES;
    }
    else
    { // INPUT

      *pOutput  =  0;
      *pPort    =  Device % INPUT_PORTS;

    }
    *pLayer     =  Device / CHAIN_DEPT;

    Result      =  OK;
  }

  return (Result);
}

void      cInputSetDeviceType(DATA8 Device,DATA8 Type, DATA8 Mode,int Line)
{
  UWORD   Index;
  char    Buf[INPUTS * 2 + 1];
  UWORD   TypeIndex;
  DATA8   Layer;
  DATA8   Port;
  DATA8   Output;

  if (cInputExpandDevice(Device,&Layer,&Port,&Output) == OK)
  { // Device within range

    if (InputInstance.DeviceData[Device].Connection == CONN_NONE)
    {
      Type       =  TYPE_NONE;
    }
    // default type is unknown!
    TypeIndex  =  InputInstance.UnknownIndex;

    if (Layer == 0)
    { // Local device

      if (Output == 0)
      { // Input device

        // TRY TO FIND DUMB INPUT DEVICE
        if (InputInstance.DeviceData[Device].Connection == CONN_INPUT_DUMB)
        { // search "type data" for matching "dumb" input device

          cInputFindDumbInputDevice(Device,Type,Mode,&TypeIndex);
        }

        // IF NOT FOUND YET - TRY TO FIND TYPE ANYWAY
        if (TypeIndex == InputInstance.UnknownIndex)
        { // device not found or not "dumb" input/output device

          cInputFindDevice(Type,Mode,&TypeIndex);
        }

        if (InputInstance.DeviceData[Device].TypeIndex != TypeIndex)
        { // type or mode has changed

          cInputResetDevice(Device,TypeIndex);

          (*InputInstance.pUart).Status[Device]      &= ~UART_DATA_READY;
          (*InputInstance.pIic).Status[Device]       &= ~IIC_DATA_READY;
          (*InputInstance.pAnalog).Updated[Device]    =  0;

          if (InputInstance.DeviceData[Device].Connection == CONN_NXT_IIC)
          { // Device is an IIC device

            cInputGetIicString(InputInstance.DeviceType[Device],InputInstance.DeviceMode[Device],&InputInstance.IicStr);
            InputInstance.IicStr.Port  =  Device;
            InputInstance.IicStr.Time  =  InputInstance.DeviceData[Device].InvalidTime;

            if ((InputInstance.IicStr.SetupLng) || (InputInstance.IicStr.PollLng))
            {
//              printf("%u %-4u %-3u %01u IIC %u 0x%08X %u 0x%08X %d\r\n",InputInstance.IicStr.Port,InputInstance.IicStr.Time,InputInstance.IicStr.Type,InputInstance.IicStr.Mode,InputInstance.IicStr.SetupLng,InputInstance.IicStr.SetupString,InputInstance.IicStr.PollLng,InputInstance.IicStr.PollString,InputInstance.IicStr.ReadLng);

              if (InputInstance.IicFile >= MIN_HANDLE)
              {
                ioctl(InputInstance.IicFile,IIC_SET,&InputInstance.IicStr);
              }
            }
          }

          // SETUP DRIVERS
          for (Index = 0;Index < INPUTS;Index++)
          { // Initialise pin setup string to do nothing

            Buf[Index]    =  '-';
          }
          Buf[Index]      =  0;

          // insert "pins" in setup string
          Buf[Device]     =  InputInstance.TypeData[InputInstance.DeviceData[Device].TypeIndex].Pins;

          // write setup string to "Device Connection Manager" driver
          if (InputInstance.DcmFile >= MIN_HANDLE)
          {
            write(InputInstance.DcmFile,Buf,INPUTS);
          }

          for (Index = 0;Index < INPUTS;Index++)
          { // build setup string for UART and IIC driver

            InputInstance.DevCon.Connection[Index]  =  InputInstance.DeviceData[Index].Connection;
            InputInstance.DevCon.Type[Index]        =  InputInstance.TypeData[InputInstance.DeviceData[Index].TypeIndex].Type;
            InputInstance.DevCon.Mode[Index]        =  InputInstance.TypeData[InputInstance.DeviceData[Index].TypeIndex].Mode;
          }

          // write setup string to "UART Device Controller" driver
          if (InputInstance.UartFile >= MIN_HANDLE)
          {
            ioctl(InputInstance.UartFile,UART_SET_CONN,&InputInstance.DevCon);
          }
          if (InputInstance.IicFile >= MIN_HANDLE)
          {
            ioctl(InputInstance.IicFile,IIC_SET_CONN,&InputInstance.DevCon);
          }

#ifdef DEBUG_TRACE_MODE_CHANGE
          printf("c_input   cInputSetDeviceType: I   D=%-3d C=%-3d Ti=%-3d N=%s\r\n",Device,InputInstance.DeviceData[Device].Connection,InputInstance.DeviceData[Device].TypeIndex,InputInstance.TypeData[InputInstance.DeviceData[Device].TypeIndex].Name);
#endif
        }
      }
      else
      { // Output device

        // TRY TO FIND DUMB OUTPUT DEVICE
        if (InputInstance.DeviceData[Device].Connection == CONN_OUTPUT_DUMB)
        { // search "type data" for matching "dumb" output device

          cInputFindDumbInputDevice(Device,Type,Mode,&TypeIndex);
        }

        // IF NOT FOUND YET - TRY TO FIND TYPE ANYWAY
        if (TypeIndex == InputInstance.UnknownIndex)
        { // device not found or not "dumb" input/output device

          cInputFindDevice(Type,Mode,&TypeIndex);
        }

        if (InputInstance.DeviceData[Device].TypeIndex != TypeIndex)
        { // type or mode has changed

          cInputResetDevice(Device,TypeIndex);

          for (Index = 0;Index < OUTPUT_PORTS;Index++)
          { // build setup string "type" for output

            Buf[Index]    =  InputInstance.DeviceType[Index + INPUT_DEVICES];
          }
          Buf[Index]      =  0;

#ifdef DEBUG_TRACE_MODE_CHANGE
          printf("c_input   cInputSetDeviceType: O   D=%-3d C=%-3d Ti=%-3d N=%s\r\n",Device,InputInstance.DeviceData[Device].Connection,InputInstance.DeviceData[Device].TypeIndex,InputInstance.TypeData[InputInstance.DeviceData[Device].TypeIndex].Name);
#endif
        }
      }
    }
    else
    { // Not local device

      // IF NOT FOUND YET - TRY TO FIND TYPE ANYWAY
      if (TypeIndex == InputInstance.UnknownIndex)
      { // device not found or not "dumb" input/output device

        cInputFindDevice(Type,Mode,&TypeIndex);
      }

      if (InputInstance.DeviceData[Device].TypeIndex != TypeIndex)
      { // type or mode has changed

        cInputResetDevice(Device,TypeIndex);

#ifdef DEBUG_TRACE_MODE_CHANGE
        printf("c_input   cInputSetDeviceType: D   D=%-3d C=%-3d Ti=%-3d N=%s\r\n",Device,InputInstance.DeviceData[Device].Connection,InputInstance.DeviceData[Device].TypeIndex,InputInstance.TypeData[InputInstance.DeviceData[Device].TypeIndex].Name);
#endif
      }
    }
#ifdef BUFPRINTSIZE
    BufPrint('p',"%-4d - D=%d SetDevice T=%-3d M=%d C=%-3d\r\n",Line,Device,InputInstance.DeviceType[Device],InputInstance.DeviceMode[Device],InputInstance.DeviceData[Device].Connection);
#endif
  }
}

void      cInputSetType(DATA8 Device,DATA8 Type,DATA8 Mode,int Line)
{
  if (InputInstance.DeviceData[Device].DevStatus == OK)
  {
    if (Type == TYPE_KEEP)
    {
      Type  =  InputInstance.DeviceType[Device];
    }
    if (Mode == MODE_KEEP)
    { // Get actual mode

      Mode  =  InputInstance.DeviceMode[Device];
    }
    if ((InputInstance.DeviceType[Device] != Type) || (InputInstance.DeviceMode[Device] != Mode))
    {
      cInputSetDeviceType(Device,Type,Mode,Line);
    }
  }
}

void      cInputCalDataInit(void)
{
  DATA8   Type;
  DATA8   Mode;
  int     File;
  char    PrgNameBuf[vmFILENAMESIZE];

  snprintf(PrgNameBuf,vmFILENAMESIZE,"%s/%s%s",vmSETTINGS_DIR,vmCALDATA_FILE_NAME,vmEXT_CONFIG);
  File  =  open(PrgNameBuf,O_RDONLY);
  if (File >= MIN_HANDLE)
  {
    if (read(File,(UBYTE*)&InputInstance.Calib,sizeof(InputInstance.Calib)) != sizeof(InputInstance.Calib))
    {
      close (File);
      File  =  -1;
    }
    else
    {
      close (File);
    }
  }
  if (File < 0)
  {
    for (Type = 0;Type < (MAX_DEVICE_TYPE);Type++)
    {
      for (Mode = 0;Mode < MAX_DEVICE_MODES;Mode++)
      {
        InputInstance.Calib[Type][Mode].InUse  =  0;
      }
    }
  }
}

RESULT    cInputInsertNewIicString(DATA8 Type,DATA8 Mode,DATA8 *pManufacturer,DATA8 *pSensorType,DATA8 SetupLng,ULONG SetupString,DATA8 PollLng,ULONG PollString,DATA8 ReadLng)
{
  RESULT  Result    = FAIL;  // FAIL=full, OK=new, BUSY=found
  IICSTR  *pTmp;
  UWORD   Index     = 0;

  if ((Type >= 0) && (Type < (MAX_DEVICE_TYPE + 1)) && (Mode >= 0) && (Mode < MAX_DEVICE_MODES))
  { // Type and mode valid


    while ((Index < InputInstance.IicDeviceTypes) && (Result != BUSY))
    { // trying to find device type

      if ((InputInstance.IicString[Index].Type == Type) && (InputInstance.IicString[Index].Mode == Mode))
      { // match on type and mode

        Result    =  BUSY;
      }
      Index++;
    }
    if (Result != BUSY)
    { // device type not found

      if (InputInstance.IicDeviceTypes < MAX_DEVICE_TYPES)
      { // Allocate room for a new type/mode

        if (cMemoryRealloc((void*)InputInstance.IicString,(void**)&pTmp,(DATA32)(sizeof(IICSTR) * (InputInstance.IicDeviceTypes + 1))) == OK)
        { // Success

          InputInstance.IicString  =  pTmp;

          InputInstance.IicString[Index].Type          =  Type;
          InputInstance.IicString[Index].Mode          =  Mode;
          snprintf((char*)InputInstance.IicString[Index].Manufacturer,IIC_NAME_LENGTH + 1,"%s",(char*)pManufacturer);
          snprintf((char*)InputInstance.IicString[Index].SensorType,IIC_NAME_LENGTH + 1,"%s",(char*)pSensorType);
          InputInstance.IicString[Index].SetupLng      =  SetupLng;
          InputInstance.IicString[Index].SetupString   =  SetupString;
          InputInstance.IicString[Index].PollLng       =  PollLng;
          InputInstance.IicString[Index].PollString    =  PollString;
          InputInstance.IicString[Index].ReadLng       =  ReadLng;
//          printf("cInputInsertNewIicString  %-3u %01u IIC %u 0x%08X %u 0x%08X %s %s\r\n",Type,Mode,SetupLng,SetupString,PollLng,PollString,pManufacturer,pSensorType);

          InputInstance.IicDeviceTypes++;
          Result    =  OK;
        }
      }
    }
    if (Result == FAIL)
    { // No room for type/mode

      
    }
  }
  else
  { // Type or mode invalid

    printf("Iic  error %d: m=%d IIC\r\n",Type,Mode);
  }

  return (Result);
}

RESULT    cInputGetNewTypeDataPointer(SBYTE *pName,DATA8 Type,DATA8 Mode,DATA8 Connection,TYPES **ppPlace)
{
  RESULT  Result    = FAIL;  // FAIL=full, OK=new, BUSY=found
  UWORD   Index     = 0;

  *ppPlace  =  NULL;

  if ((Type >= 0) && (Type < (MAX_DEVICE_TYPE + 1)) && (Mode >= 0) && (Mode < MAX_DEVICE_MODES))
  { // Type and mode valid

    while ((Index < InputInstance.MaxDeviceTypes) && (Result != BUSY))
    { // trying to find device type

      if ((InputInstance.TypeData[Index].Type == Type) && (InputInstance.TypeData[Index].Mode == Mode) && (InputInstance.TypeData[Index].Connection == Connection))
      { // match on type, mode and connection

        *ppPlace  =  &InputInstance.TypeData[Index];
        Result    =  BUSY;
      }
      Index++;
    }
    if (Result != BUSY)
    { // device type not found

      if (InputInstance.MaxDeviceTypes < MAX_DEVICE_TYPES)
      { // Allocate room for a new type/mode

        if (cMemoryRealloc((void*)InputInstance.TypeData,(void**)ppPlace,(DATA32)(sizeof(TYPES) * (InputInstance.MaxDeviceTypes + 1))) == OK)
        { // Success

          InputInstance.TypeData  =  *ppPlace;

          *ppPlace  =  &InputInstance.TypeData[InputInstance.MaxDeviceTypes];
          InputInstance.TypeModes[Type]++;
          InputInstance.MaxDeviceTypes++;
          Result    =  OK;
        }
      }
    }
    if (Result == FAIL)
    { // No room for type/mode

      
    }
  }
  else
  { // Type or mode invalid

    printf("Type error %d: m=%d c=%d n=%s\r\n",Type,Mode,Connection,pName);
  }

  return (Result);
}

RESULT    cInputInsertTypeData(char *pFilename)
{
  RESULT  Result = FAIL;
  LFILE   *pFile;
  char    Buf[256];
  char    Name[256];
  char    Symbol[256];
  char    Manufacturer[256];
  char    SensorType[256];
  unsigned int Type;
  unsigned int Connection;
  unsigned int Mode;
  unsigned int DataSets;
  unsigned int Format;
  unsigned int Figures;
  unsigned int Decimals;
  unsigned int Views;
  unsigned int Pins;
  unsigned int Time;
  unsigned int IdValue;
  unsigned int SetupLng;
  unsigned int SetupString;
  unsigned int PollLng;
  unsigned int PollString;
  int ReadLng;
  char    *Str;
  TYPES   Tmp;
  TYPES   *pTypes;
  int     Count;

  pFile = fopen (pFilename,"r");
  if (pFile != NULL)
  {
    do
    {
      Str       =  fgets(Buf,255,pFile);
      Buf[255]  =  0;
      if (Str != NULL)
      {
        if ((Buf[0] != '/') && (Buf[0] != '*'))
        {
          Count  =  sscanf(Buf,"%u %u %s %u %u %u %u %u %u %x %f %f %f %f %f %f %u %u %s",&Type,&Mode,Name,&DataSets,&Format,&Figures,&Decimals,&Views,&Connection,&Pins,&Tmp.RawMin,&Tmp.RawMax,&Tmp.PctMin,&Tmp.PctMax,&Tmp.SiMin,&Tmp.SiMax,&Time,&IdValue,Symbol);
          if (Count == TYPE_PARAMETERS)
          {
            Tmp.Type         =  (DATA8)Type;
            Tmp.Mode         =  (DATA8)Mode;
            Tmp.DataSets     =  (DATA8)DataSets;
            Tmp.Format       =  (DATA8)Format;
            Tmp.Figures      =  (DATA8)Figures;
            Tmp.Decimals     =  (DATA8)Decimals;
            Tmp.Connection   =  (DATA8)Connection;
            Tmp.Views        =  (DATA8)Views;
            Tmp.Pins         =  (DATA8)Pins;
            Tmp.InvalidTime  =  (UWORD)Time;
            Tmp.IdValue      =  (UWORD)IdValue;

            Result  =  cInputGetNewTypeDataPointer((SBYTE*)Name,(DATA8)Type,(DATA8)Mode,(DATA8)Connection,&pTypes);
//            printf("cInputTypeDataInit\r\n");
            if (Result == OK)
            {
              (*pTypes)  =  Tmp;

              Count  =  0;
              while ((Name[Count]) && (Count < TYPE_NAME_LENGTH))
              {
                if (Name[Count] == '_')
                {
                  (*pTypes).Name[Count]  =  ' ';
                }
                else
                {
                  (*pTypes).Name[Count]  =  Name[Count];
                }
                Count++;
              }
              (*pTypes).Name[Count]    =  0;

              if (Symbol[0] == '_')
              {
                (*pTypes).Symbol[0]  =  0;
              }
              else
              {
                Count  =  0;
                while ((Symbol[Count]) && (Count < SYMBOL_LENGTH))
                {
                  if (Symbol[Count] == '_')
                  {
                    (*pTypes).Symbol[Count]  =  ' ';
                  }
                  else
                  {
                    (*pTypes).Symbol[Count]  =  Symbol[Count];
                  }
                  Count++;
                }
                (*pTypes).Symbol[Count]    =  0;
              }
              if (Tmp.Connection == CONN_NXT_IIC)
              { // NXT IIC sensor

                // setup string + poll string
                // 3 0x01420000 2 0x01000000

                Count  =  sscanf(Buf,"%u %u %s %u %u %u %u %u %u %x %f %f %f %f %f %f %u %u %s %s %s %u %X %u %X %d",&Type,&Mode,Name,&DataSets,&Format,&Figures,&Decimals,&Views,&Connection,&Pins,&Tmp.RawMin,&Tmp.RawMax,&Tmp.PctMin,&Tmp.PctMax,&Tmp.SiMin,&Tmp.SiMax,&Time,&IdValue,Symbol,Manufacturer,SensorType,&SetupLng,&SetupString,&PollLng,&PollString,&ReadLng);
                if (Count == (TYPE_PARAMETERS + 7))
                {
                  cInputInsertNewIicString(Type,Mode,(DATA8*)Manufacturer,(DATA8*)SensorType,(DATA8)SetupLng,(ULONG)SetupString,(DATA8)PollLng,(ULONG)PollString,(DATA8)ReadLng);
//                  printf("%02u %01u IIC %u 0x%08X %u 0x%08X %u\r\n",Type,Mode,SetupLng,SetupString,PollLng,PollString,ReadLng);
                }
              }
            }
          }
        }
      }
    }
    while (Str != NULL);
    fclose (pFile);

    Result  =  OK;
  }

  return (Result);
}

void      cInputTypeDataInit(void)
{
  char    PrgNameBuf[255];
  UWORD   Index   = 0;
  UBYTE   TypeDataFound = 0;

  // Set TypeMode to mode = 0
  Index  =  0;
  while (Index < (MAX_DEVICE_TYPE + 1))
  {
    InputInstance.TypeModes[Index]  =  0;
    Index++;
  }

  // Insert default types into TypeData
  Index  =  0;
  while ((Index < (InputInstance.MaxDeviceTypes + 1)) && (TypeDefault[Index].Name[0] != 0))
  {
    InputInstance.TypeData[Index]  =  TypeDefault[Index];

    snprintf((char*)InputInstance.TypeData[Index].Name,TYPE_NAME_LENGTH + 1,"%s",(char*)TypeDefault[Index].Name);

    if (InputInstance.TypeData[Index].Type == TYPE_NONE)
    {
      InputInstance.NoneIndex  =  Index;
    }
    if (InputInstance.TypeData[Index].Type == TYPE_UNKNOWN)
    {
      InputInstance.UnknownIndex  =  Index;
    }
    InputInstance.TypeModes[InputInstance.TypeData[Index].Type]++;
    Index++;
  }

//  printf("Search start\r\n");
  snprintf(PrgNameBuf,vmFILENAMESIZE,"%s/%s%s",vmSETTINGS_DIR,TYPEDATE_FILE_NAME,EXT_CONFIG);

  if (cInputInsertTypeData(PrgNameBuf) == OK)
  {
    TypeDataFound  =  1;
  }

  for (Index = TYPE_THIRD_PARTY_START;Index <= TYPE_THIRD_PARTY_END;Index++)
  {
    snprintf(PrgNameBuf,vmFILENAMESIZE,"%s/%s%02d%s",vmSETTINGS_DIR,TYPEDATE_FILE_NAME,Index,EXT_CONFIG);
    if (cInputInsertTypeData(PrgNameBuf) == OK)
    {
      TypeDataFound  =  1;
    }
  }
//  printf("Done\r\n");

  if (!TypeDataFound)
  {
  }
}


RESULT    cInputInit(void)
{
  RESULT  Result = OK;
  ANALOG  *pAdcTmp;
  UART    *pUartTmp;
  IIC     *pIicTmp;
  PRGID   TmpPrgId;
  UWORD   Tmp;
  UWORD   Set;

  InputInstance.TypeDataIndex   =  DATA16_MAX;

  InputInstance.MaxDeviceTypes  =  3;

  cMemoryMalloc((void*)&InputInstance.TypeData,(DATA32)(sizeof(TYPES) * InputInstance.MaxDeviceTypes));

  InputInstance.IicDeviceTypes  =  1;

  cMemoryMalloc((void*)&InputInstance.IicString,(DATA32)(sizeof(IICSTR) * InputInstance.IicDeviceTypes));

  InputInstance.pAnalog     =  &InputInstance.Analog;

  InputInstance.pUart       =  &InputInstance.Uart;

  InputInstance.pIic        =  &InputInstance.Iic;

  InputInstance.AdcFile     =  open(ANALOG_DEVICE_NAME,O_RDWR | O_SYNC);
  InputInstance.UartFile    =  open(UART_DEVICE_NAME,O_RDWR | O_SYNC);
  InputInstance.DcmFile     =  open(DCM_DEVICE_NAME,O_RDWR | O_SYNC);
  InputInstance.IicFile     =  open(IIC_DEVICE_NAME,O_RDWR | O_SYNC);
  InputInstance.DCMUpdate   =  1;

  if (InputInstance.AdcFile >= MIN_HANDLE)
  {
    pAdcTmp  =  (ANALOG*)mmap(0, sizeof(ANALOG), PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, InputInstance.AdcFile, 0);

    if (pAdcTmp == MAP_FAILED)
    {
#ifndef Linux_X86
      
      Result  =  FAIL;
#endif
      InputInstance.DCMUpdate   =  0;
    }
    else
    {
      InputInstance.pAnalog  =  pAdcTmp;
    }
  }
  else
  {
#ifndef Linux_X86
    
    Result  =  FAIL;
#endif
    InputInstance.DCMUpdate   =  0;
  }


  if (InputInstance.UartFile >= MIN_HANDLE)
  {
    pUartTmp  =  (UART*)mmap(0, sizeof(UART), PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, InputInstance.UartFile, 0);

    if (pUartTmp == MAP_FAILED)
    {
#ifndef Linux_X86
      
      Result  =  FAIL;
#endif
      InputInstance.DCMUpdate   =  0;
    }
    else
    {
      InputInstance.pUart  =  pUartTmp;
    }
  }
  else
  {
#ifndef Linux_X86
    
    Result  =  FAIL;
#endif
//    InputInstance.DCMUpdate   =  0;
  }


  if (InputInstance.IicFile >= MIN_HANDLE)
  {
    pIicTmp  =  (IIC*)mmap(0, sizeof(UART), PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, InputInstance.IicFile, 0);

    if (pIicTmp == MAP_FAILED)
    {
#ifndef Linux_X86
      
      Result  =  FAIL;
#endif
      InputInstance.DCMUpdate   =  0;
    }
    else
    {
      InputInstance.pIic  =  pIicTmp;
    }
  }
  else
  {
#ifndef Linux_X86
    
    Result  =  FAIL;
#endif
//    InputInstance.DCMUpdate   =  0;
  }


  cInputTypeDataInit();
  cInputCalDataInit();

  for (Tmp = 0;Tmp < DEVICES;Tmp++)
  {
    for (Set = 0;Set < MAX_DEVICE_DATASETS;Set++)
    {
      InputInstance.DeviceData[Tmp].Raw[Set]  =  DATAF_NAN;
    }
    InputInstance.DeviceData[Tmp].Owner       =  0;
    InputInstance.DeviceData[Tmp].Busy        =  0;
    InputInstance.DeviceData[Tmp].Connection  =  CONN_NONE;
    InputInstance.DeviceData[Tmp].DevStatus   =  BUSY;
    InputInstance.DeviceData[Tmp].TypeIndex   =  InputInstance.NoneIndex;
    InputInstance.DeviceType[Tmp]             =  TYPE_NONE;
    InputInstance.DeviceMode[Tmp]             =  0;
#ifndef DISABLE_BUMBED
    InputInstance.DeviceData[Tmp].Changes     =  (DATA32)0;
    InputInstance.DeviceData[Tmp].Bumps       =  (DATA32)0;
#endif
  }

  for (Tmp = 0;Tmp < INPUT_PORTS;Tmp++)
  {
    InputInstance.TmpMode[Tmp]  =  MAX_DEVICE_MODES;
  }

  for (TmpPrgId = 0;TmpPrgId < MAX_PROGRAMS;TmpPrgId++)
  {
    InputInstance.ConfigurationChanged[TmpPrgId]  =  0;
  }

  return (Result);
}

DATA8     cInputGetDevice(DATA8 Layer, DATA8 No)
{
  return (No + (Layer * INPUT_PORTS));
}

void      cInputCalcFullScale(UWORD *pRawVal, UWORD ZeroPointOffset, UBYTE PctFullScale, UBYTE InvStatus)
{
  if (*pRawVal >= ZeroPointOffset)
  {
    *pRawVal -= ZeroPointOffset;
  }
  else
  {
    *pRawVal = 0;
  }

  *pRawVal = (*pRawVal * 100)/PctFullScale;
  if (*pRawVal > SENSOR_RESOLUTION)
  {
    *pRawVal = SENSOR_RESOLUTION;
  }
  if (TRUE == InvStatus)
  {
    *pRawVal = SENSOR_RESOLUTION - *pRawVal;
  }
}

void      cInputCalibrateColor(COLORSTRUCT *pC, UWORD *pNewVals)
{
  UBYTE CalRange;

  if ((pC->ADRaw[BLANK]) < pC->CalLimits[1])
  {
    CalRange = 2;
  }
  else
  {
    if ((pC->ADRaw[BLANK]) < pC->CalLimits[0])
    {
      CalRange = 1;
    }
    else
    {
      CalRange = 0;
    }
  }

  pNewVals[RED] = 0;
  if ((pC->ADRaw[RED]) > (pC->ADRaw[BLANK]))
  {
    pNewVals[RED] = (UWORD)(((ULONG)((pC->ADRaw[RED]) - (pC->ADRaw[BLANK])) * (pC->Calibration[CalRange][RED])) >> 16);
  }

  pNewVals[GREEN] = 0;
  if ((pC->ADRaw[GREEN]) > (pC->ADRaw[BLANK]))
  {
     pNewVals[GREEN] = (UWORD)(((ULONG)((pC->ADRaw[GREEN]) - (pC->ADRaw[BLANK])) * (pC->Calibration[CalRange][GREEN])) >> 16);
  }

  pNewVals[BLUE] = 0;
  if ((pC->ADRaw[BLUE]) > (pC->ADRaw[BLANK]))
  {
    pNewVals[BLUE] = (UWORD)(((ULONG)((pC->ADRaw[BLUE]) -(pC->ADRaw[BLANK])) * (pC->Calibration[CalRange][BLUE])) >> 16);
  }

  pNewVals[BLANK] = (pC->ADRaw[BLANK]);
  cInputCalcFullScale(&(pNewVals[BLANK]), COLORSENSORBGMIN, COLORSENSORBGPCTDYN, FALSE);
  (pNewVals[BLANK]) = (UWORD)(((ULONG)(pNewVals[BLANK]) * (pC->Calibration[CalRange][BLANK])) >> 16);
}

DATAF     cInputCalculateColor(COLORSTRUCT *pC)
{
  DATAF   Result ;


  Result  =  DATAF_NAN;

  // Color Sensor values has been calculated -
  // now calculate the color and put it in Sensor value
  if (((pC->SensorRaw[RED]) > (pC->SensorRaw[BLUE] )) &&
      ((pC->SensorRaw[RED]) > (pC->SensorRaw[GREEN])))
  {

    // If all 3 colors are less than 65 OR (Less that 110 and bg less than 40)
    if (((pC->SensorRaw[RED])   < 65) ||
        (((pC->SensorRaw[BLANK]) < 40) && ((pC->SensorRaw[RED])  < 110)))
    {
      Result  =  (DATAF)BLACKCOLOR;
    }
    else
    {
      if (((((pC->SensorRaw[BLUE]) >> 2)  + ((pC->SensorRaw[BLUE]) >> 3) + (pC->SensorRaw[BLUE])) < (pC->SensorRaw[GREEN])) &&
          ((((pC->SensorRaw[GREEN]) << 1)) > (pC->SensorRaw[RED])))
      {
        Result  =  (DATAF)YELLOWCOLOR;
      }
      else
      {

        if ((((pC->SensorRaw[GREEN]) << 1) - ((pC->SensorRaw[GREEN]) >> 2)) < (pC->SensorRaw[RED]))
        {

          Result  =  (DATAF)REDCOLOR;
        }
        else
        {

          if ((((pC->SensorRaw[BLUE]) < 70) ||
              ((pC->SensorRaw[GREEN]) < 70)) ||
             (((pC->SensorRaw[BLANK]) < 140) && ((pC->SensorRaw[RED]) < 140)))
          {
            Result  =  (DATAF)BLACKCOLOR;
          }
          else
          {
            Result  =  (DATAF)WHITECOLOR;
          }
        }
      }
    }
  }
  else
  {

    // Red is not the dominant color
    if ((pC->SensorRaw[GREEN]) > (pC->SensorRaw[BLUE]))
    {

      // Green is the dominant color
      // If all 3 colors are less than 40 OR (Less that 70 and bg less than 20)
      if (((pC->SensorRaw[GREEN])  < 40) ||
          (((pC->SensorRaw[BLANK]) < 30) && ((pC->SensorRaw[GREEN])  < 70)))
      {
        Result  =  (DATAF)BLACKCOLOR;
      }
      else
      {
        if ((((pC->SensorRaw[BLUE]) << 1)) < (pC->SensorRaw[RED]))
        {
          Result  =  (DATAF)YELLOWCOLOR;
        }
        else
        {
          if ((((pC->SensorRaw[RED]) + ((pC->SensorRaw[RED])>>2)) < (pC->SensorRaw[GREEN])) ||
              (((pC->SensorRaw[BLUE]) + ((pC->SensorRaw[BLUE])>>2)) < (pC->SensorRaw[GREEN])))
          {
            Result  =  (DATAF)GREENCOLOR;
          }
          else
          {
            if ((((pC->SensorRaw[RED]) < 70) ||
                ((pC->SensorRaw[BLUE]) < 70)) ||
                (((pC->SensorRaw[BLANK]) < 140) && ((pC->SensorRaw[GREEN]) < 140)))
            {
              Result  =  (DATAF)BLACKCOLOR;
            }
            else
            {
              Result  =  (DATAF)WHITECOLOR;
            }
          }
        }
      }
    }
    else
    {

      // Blue is the most dominant color
      // Colors can be blue, white or black
      // If all 3 colors are less than 48 OR (Less that 85 and bg less than 25)
      if (((pC->SensorRaw[BLUE])   < 48) ||
          (((pC->SensorRaw[BLANK]) < 25) && ((pC->SensorRaw[BLUE])  < 85)))
      {
        Result  =  (DATAF)BLACKCOLOR;
      }
      else
      {
        if ((((((pC->SensorRaw[RED]) * 48) >> 5) < (pC->SensorRaw[BLUE])) &&
            ((((pC->SensorRaw[GREEN]) * 48) >> 5) < (pC->SensorRaw[BLUE])))
            ||
            (((((pC->SensorRaw[RED])   * 58) >> 5) < (pC->SensorRaw[BLUE])) ||
             ((((pC->SensorRaw[GREEN]) * 58) >> 5) < (pC->SensorRaw[BLUE]))))
        {
          Result  =  (DATAF)BLUECOLOR;
        }
        else
        {

          // Color is white or Black
          if ((((pC->SensorRaw[RED])  < 60) ||
              ((pC->SensorRaw[GREEN]) < 60)) ||
             (((pC->SensorRaw[BLANK]) < 110) && ((pC->SensorRaw[BLUE]) < 120)))
          {
            Result  =  (DATAF)BLACKCOLOR;
          }
          else
          {
            if ((((pC->SensorRaw[RED])  + ((pC->SensorRaw[RED])   >> 3)) < (pC->SensorRaw[BLUE])) ||
                (((pC->SensorRaw[GREEN]) + ((pC->SensorRaw[GREEN]) >> 3)) < (pC->SensorRaw[BLUE])))
            {
              Result  =  (DATAF)BLUECOLOR;
            }
            else
            {
              Result  =  (DATAF)WHITECOLOR;
            }
          }
        }
      }
    }
  }


  return (Result);
}

DATAF     cInputGetColor(DATA8 Device,DATA8 Index)
{
  DATAF   Result ;

  Result  =  DATAF_NAN;

  cInputCalibrateColor(&(*InputInstance.pAnalog).NxtCol[Device],(*InputInstance.pAnalog).NxtCol[Device].SensorRaw);

  switch (InputInstance.DeviceMode[Device])
  {
    case 2 :
    { // NXT-COL-COL

      Result  =  cInputCalculateColor(&(*InputInstance.pAnalog).NxtCol[Device]);
    }
    break;

    case 1 :
    { // NXT-COL-AMB

      Result  =  (*InputInstance.pAnalog).NxtCol[Device].ADRaw[BLANK];
    }
    break;

    case 0 :
    { // NXT-COL-RED

      Result  =  (*InputInstance.pAnalog).NxtCol[Device].ADRaw[RED];
    }
    break;

    case 3 :
    { // NXT-COL-GRN

      Result  =  (*InputInstance.pAnalog).NxtCol[Device].ADRaw[GREEN];
    }
    break;

    case 4 :
    { // NXT-COL-BLU

      Result  =  (*InputInstance.pAnalog).NxtCol[Device].ADRaw[BLUE];
    }
    break;

    case 5 :
    { // NXT-COL-RAW

      if (Index < COLORS)
      {
        Result  =  (DATAF)(*InputInstance.pAnalog).NxtCol[Device].SensorRaw[Index];
      }
    }
    break;
  }

  return (Result);
}

DATAF     cInputReadDeviceRaw(DATA8 Device,DATA8 Index,DATA16 Time,DATA16 *pInit)
{
  DATAF   Result;
  DATA8   DataSets;
  void    *pResult;

  Result  =  DATAF_NAN;

#ifdef DEBUG_C_INPUT_DAISYCHAIN
  printf("c_input   cInputReadDeviceRaw:     D=%-3d B=%d\r\n",Device,InputInstance.DeviceData[Device].DevStatus);
#endif
  if ((Device >= 0) && (Device < DEVICES) && (Index >= 0) && (Index < MAX_DEVICE_DATASETS))
  {
    // Parameters are valid

    if (InputInstance.DeviceData[Device].DevStatus == OK)
    {

      if (Device < INPUTS)
      {
        // Device is a local sensor

        if ((InputInstance.DeviceData[Device].Connection != CONN_NONE) && (InputInstance.DeviceData[Device].Connection != CONN_ERROR))
        {
          // Device is connected right

          if (InputInstance.DeviceData[Device].Connection == CONN_INPUT_UART)
          {
            // Device is a UART sensor

            pResult   =  (void*)&(*InputInstance.pUart).Raw[Device];
            DataSets  =  InputInstance.TypeData[InputInstance.DeviceData[Device].TypeIndex].DataSets;

            if (Index < DataSets)
            {
              switch (InputInstance.TypeData[InputInstance.DeviceData[Device].TypeIndex].Format & 0x0F)
              {
                case DATA_8 :
                {
                  InputInstance.DeviceData[Device].Raw[Index]   =  (DATAF)*(((DATA8*)pResult) + Index);
                }
                break;

                case DATA_16 :
                {
                  InputInstance.DeviceData[Device].Raw[Index]   =  (DATAF)*(((DATA16*)pResult) + Index);
                }
                break;

                case DATA_32 :
                {
                  InputInstance.DeviceData[Device].Raw[Index]   =  (DATAF)*(((DATA32*)pResult) + Index);
                }
                break;

                case DATA_F :
                {
                  InputInstance.DeviceData[Device].Raw[Index]   =  (DATAF)*(((DATAF*)pResult) + Index);
                }
                break;

                default :
                {
                  InputInstance.DeviceData[Device].Raw[Index]   =  DATAF_NAN;
                }
                break;

              }
            }
            else
            {
              InputInstance.DeviceData[Device].Raw[Index]       =  DATAF_NAN;
            }
          }
          else
          {
            // Device is not a UART sensor

            if (InputInstance.DeviceData[Device].Connection == CONN_INPUT_DUMB)
            {
              // Device is new dumb

              InputInstance.DeviceData[Device].Raw[Index]       =  (DATAF)(*InputInstance.pAnalog).InPin6[Device];
            }
            else
            {
#ifndef DISABLE_OLD_COLOR

              if (InputInstance.DeviceData[Device].Connection == CONN_NXT_COLOR)
              {
                // Device is nxt color

                InputInstance.DeviceData[Device].Raw[Index]       =  (DATAF)cInputGetColor(Device,Index);
              }
              else
              {
                // Device is old dumb

                InputInstance.DeviceData[Device].Raw[Index]       =  (DATAF)(*InputInstance.pAnalog).InPin1[Device];
              }
#else
              // Device is old dumb

              InputInstance.DeviceData[Device].Raw[Index]       =  (DATAF)(*InputInstance.pAnalog).InPin1[Device];

#endif
            }
          }
        }
        else
        {
          InputInstance.DeviceData[Device].Raw[Index]           =  DATAF_NAN;
        }
      }
      Result  =  InputInstance.DeviceData[Device].Raw[Index];
    }
  }

  return (Result);
}

DATAF     cInputReadDeviceSi(DATA8 Device,DATA8 Index,DATA16 Time,DATA16 *pInit)
{
  UWORD   TypeIndex;
  DATAF   Raw;
  DATA8   Type;
  DATA8   Mode;
  DATA8   Connection;
  DATAF   Min;
  DATAF   Max;

  Raw           =  cInputReadDeviceRaw(Device,Index,Time,pInit);

  if (!(isnan(Raw)))
  {
    TypeIndex   =  InputInstance.DeviceData[Device].TypeIndex;

    Type        =  InputInstance.TypeData[TypeIndex].Type;
    Mode        =  InputInstance.TypeData[TypeIndex].Mode;
    Min         =  InputInstance.TypeData[TypeIndex].RawMin;
    Max         =  InputInstance.TypeData[TypeIndex].RawMax;

    if ((Type > 0) && (Type < (MAX_DEVICE_TYPE + 1)) && (Mode >= 0) && (Mode < MAX_DEVICE_MODES))
    {
      if (InputInstance.Calib[Type][Mode].InUse)
      {
        Min     =  InputInstance.Calib[Type][Mode].Min;
        Max     =  InputInstance.Calib[Type][Mode].Max;
      }
    }

    Raw         =  (((Raw - Min) * (InputInstance.TypeData[TypeIndex].SiMax - InputInstance.TypeData[TypeIndex].SiMin)) / (Max - Min) + InputInstance.TypeData[TypeIndex].SiMin);

    // Limit values on dumb connections if "pct" or "_"
    Connection  =  InputInstance.TypeData[TypeIndex].Connection;
    if ((Connection == CONN_NXT_DUMB) || (Connection == CONN_INPUT_DUMB) || (Connection == CONN_OUTPUT_DUMB) || (Connection == CONN_OUTPUT_TACHO))
    {
      if ((InputInstance.TypeData[TypeIndex].Symbol[0] == 'p') || (InputInstance.TypeData[TypeIndex].Symbol[0] == ' ') || (InputInstance.TypeData[TypeIndex].Symbol[0] == 0))
      {
        if (Raw > InputInstance.TypeData[TypeIndex].SiMax)
        {
          Raw     =  InputInstance.TypeData[TypeIndex].SiMax;
        }
        if (Raw < InputInstance.TypeData[TypeIndex].SiMin)
        {
          Raw     =  InputInstance.TypeData[TypeIndex].SiMin;
        }
      }
    }

  }

  return (Raw);
}

/*! \page cInput
 *  <hr size="1"/>
 *  <b>     opINPUT_READSI (LAYER, NO, TYPE, MODE, SI)  </b>
 *
 *- Read device value in SI units\n
 *- Dispatch status unchanged
 *
 *  \param  (DATA8)   LAYER   - Chain layer number [0..3]
 *  \param  (DATA8)   NO      - Port number
 *  \param  (DATA8) \ref types "TYPE" - Device type (0 = don't change type)
 *  \param  (DATA8)   MODE    - Device mode [0..7] (-1 = don't change mode)
 *  \return (DATAF)   SI      - SI unit value from device
 */
DATAF cInputReadSi(DATA8 Layer, DATA8 No, DATA8 Type, DATA8 Mode)
{
  DATA8   Device;

  Device      =  cInputGetDevice(Layer, No);

  if (Device < DEVICES)
  {
    cInputSetType(Device,Type,Mode,__LINE__);
  }
  return cInputReadDeviceSi(Device,0,0,NULL);
}

int main(int argc, char** argv) {

    cInputInit();

    int i = 0;
    for(i = 0; i < 1000; i++) {
	printf("%lf-", cInputReadSi(0, 0, 0, -1));
	printf("%lf-", cInputReadSi(0, 1, 0, -1));
	printf("%lf-", cInputReadSi(0, 2, 0, -1));
	printf("%lf\n", cInputReadSi(0, 3, 0, -1));
    }
    
    return 0;

}
