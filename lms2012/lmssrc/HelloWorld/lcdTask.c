#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/fb.h>

typedef   unsigned char         UBYTE;  //!< Basic Type used to symbolise 8  bit unsigned values
typedef   unsigned short        UWORD;  //!< Basic Type used to symbolise 16 bit unsigned values
typedef   unsigned int          ULONG;  //!< Basic Type used to symbolise 32 bit unsigned values

typedef   signed char           SBYTE;  //!< Basic Type used to symbolise 8  bit signed values
typedef   signed short          SWORD;  //!< Basic Type used to symbolise 16 bit signed values
typedef   signed int            SLONG;  //!< Basic Type used to symbolise 32 bit signed values

typedef   float                 FLOAT;  //!< Basic Type used to symbolise 32 bit floating point values

typedef   SBYTE                 DATA8;  //!< VM Type for 1 byte signed value
typedef   SWORD                 DATA16; //!< VM Type for 2 byte signed value
typedef   SLONG                 DATA32; //!< VM Type for 4 byte signed value
typedef   FLOAT                 DATAF;  //!< VM Type for 4 byte floating point value

#define FBCTL(cmd, arg)                 \
    if(ioctl(lcdFile, cmd, arg) == -1) { \
            printf("LCD_DEVICE_FILE_NOT_FOUND"); }

#define LCDCopy(S,D,L) memcpy((void*)D,(const void*)S,L)  // Copy S to D

#define MY_PRIORITY (49) /* we use 49 as the PRREMPT_RT use 50
                        as the priority of kernel tasklets
                        and interrupt handler by default */

#define MAX_SAFE_STACK (8*1024) /* The maximum stack size which is
                               guaranteed safe to access without
                               faulting */

#define NSEC_PER_SEC    (1000000000) /* The number of nsecs per sec. */

#define   LCD_WIDTH                     178                   //!< LCD horizontal pixels
#define   LCD_HEIGHT                    128                  //!< LCD vertical pixels
#define   LCD_BUFFER_SIZE (((LCD_WIDTH + 7) / 8) * LCD_HEIGHT)
typedef   struct
{
UBYTE   Lcd[LCD_BUFFER_SIZE];
}
LCD;

UBYTE     PixelTab[] =
{
0x00, // 000 00000000
0xE0, // 001 11100000
0x1C, // 010 00011100
0xFC, // 011 11111100
0x03, // 100 00000011
0xE3, // 101 11100011
0x1F, // 110 00011111
0xFF  // 111 11111111
};

unsigned char *dbuf=NULL;

void stack_prefault(void) {

unsigned char dummy[MAX_SAFE_STACK];

memset(dummy, 0, MAX_SAFE_STACK);
return;
}

DATA8     readPixel(UBYTE *pImage, DATA16 X0, DATA16 Y0)
{
DATA8   Result = 0;

if ((X0 >= 0) && (X0 < LCD_WIDTH) && (Y0 >= 0) && (Y0 < LCD_HEIGHT))
{
if ((pImage[(X0 >> 3) + Y0 * ((LCD_WIDTH + 7) >> 3)] & (1 << (X0 % 8))))
{
  Result  =  1;
}
}

return (Result);
}


void      dLcdExec(LCD *pDisp)
{
UBYTE   *pSrc;
UBYTE   *pDst;
ULONG   Pixels;
UWORD   X;
UWORD   Y;


if (dbuf)
{
  pSrc  =  (*pDisp).Lcd;
  pDst  =  dbuf;

  for (Y = 0;Y < 128;Y++)
  {
    for (X = 0;X < 7;X++)
    {
      Pixels  =  (ULONG)*pSrc;
      pSrc++;
      Pixels |=  (ULONG)*pSrc << 8;
      pSrc++;
      Pixels |=  (ULONG)*pSrc << 16;
      pSrc++;

      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
      Pixels >>= 3;
      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
      Pixels >>= 3;
      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
      Pixels >>= 3;
      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
      Pixels >>= 3;
      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
      Pixels >>= 3;
      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
      Pixels >>= 3;
      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
      Pixels >>= 3;
      *pDst   =  PixelTab[Pixels & 0x07];
      pDst++;
    }
    Pixels  =  (ULONG)*pSrc;
    pSrc++;
    Pixels |=  (ULONG)*pSrc << 8;
    pSrc++;

    *pDst   =  PixelTab[Pixels & 0x07];
    pDst++;
    Pixels >>= 3;
    *pDst   =  PixelTab[Pixels & 0x07];
    pDst++;
    Pixels >>= 3;
    *pDst   =  PixelTab[Pixels & 0x07];
    pDst++;
    Pixels >>= 3;
    *pDst   =  PixelTab[Pixels & 0x07];
    pDst++;
  }

} else {
  
}
}

void drawPixel(UBYTE *pImage, signed char Color, signed short X0, signed short Y0)
{
if ((X0 >= 0) && (X0 < LCD_WIDTH) && (Y0 >= 0) && (Y0 < LCD_HEIGHT)) {
if (Color)
{
  pImage[(X0 >> 3) + Y0 * ((LCD_WIDTH + 7) >> 3)]  |=  (1 << (X0 % 8));
}
else
{
  pImage[(X0 >> 3) + Y0 * ((LCD_WIDTH + 7) >> 3)]  &= ~(1 << (X0 % 8));
}
}
}

int countNeighbors(UBYTE* field, signed short i, signed short j) {

int x, y; 
int count=0;
for(x=i-1; x <= (i+1) ; x++)
{
    for(y=j-1; y <= (j+1) ; y++)
    {
        if ( (x==i) && (y==j) ) continue;
        if ( (y<LCD_HEIGHT) && (x<LCD_WIDTH) &&
                (x>=0)   && (y>=0) )
        {
            count += readPixel(field, x,y);
        }
    }
}
return count;

}

int main(int argc, char* argv[])
{
    struct timespec t;
    struct sched_param param;
    int interval = 250000000; 

    /* Declare ourself as a real time task */

    param.sched_priority = MY_PRIORITY;
    if(sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("sched_setscheduler failed");
        exit(-1);
    }

    /* Lock memory */

    if(mlockall(MCL_CURRENT|MCL_FUTURE) == -1) {
        perror("mlockall failed");
        exit(-2);
    }


    /* Init LCD */
    int dll, fll;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    FILE* lcdFile = open("/dev/fb0", O_RDWR);
    ioctl(lcdFile, _IOW('S',0, int), NULL);

    FBCTL(FBIOGET_VSCREENINFO, &var);
    FBCTL(FBIOGET_FSCREENINFO, &fix);

    /* Display line length in bytes */
    dll = fix.line_length;
    /* Image file line length in bytes */
    fll = (var.xres >> 3) + 1;

    /* MMap LCD */
    dbuf = (unsigned char *)mmap(0, var.yres * dll, PROT_WRITE | PROT_READ, MAP_SHARED, lcdFile, 0);
    if (dbuf == MAP_FAILED) printf("ERROR MMAP LCD!");
    printf("checkpoint after initiating lcd\n");

    /* Initialize GoL fields */
    unsigned long round = 0;
    LCD field0 = {0};
    LCD field1  = {0};
    LCD *srcField, *destField;
    int i = 0;
    srand(time(NULL));
    for(i = 0; i < LCD_BUFFER_SIZE; i++) {
        field0.Lcd[i] = rand() % 256;
    }

    printf("checkpoint before writing lcd\n");

    dLcdExec(&field0);

    printf("checkpoint after writing lcd\n");

    int cellCount; 
    /* Pre-fault our stack */
    stack_prefault();

    clock_gettime(CLOCK_MONOTONIC ,&t);
    /* start after one second */
    t.tv_sec++;

    printf("checkpoint after lcd\n");
    while(1) {

        /* wait until next shot */
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);

        if(round == 250) {
            round = 0;
            for(i = 0; i < LCD_BUFFER_SIZE; i++) {
                field0.Lcd[i] = rand() % 256;
            }
        } else if (round == 50) {
            for(i = 0; i < LCD_BUFFER_SIZE; i++) {
                field0.Lcd[i] = 0;
            }

            drawPixel(field0.Lcd, 1, 80, 70);   
            drawPixel(field0.Lcd, 1, 82, 70);   
            drawPixel(field0.Lcd, 1, 82, 69);   
            drawPixel(field0.Lcd, 1, 84, 68);   
            drawPixel(field0.Lcd, 1, 84, 67);   
            drawPixel(field0.Lcd, 1, 84, 66);   

            drawPixel(field0.Lcd, 1, 86, 67);   
            drawPixel(field0.Lcd, 1, 86, 66);   
            drawPixel(field0.Lcd, 1, 87, 66);   
            drawPixel(field0.Lcd, 1, 86, 65);   
        }

        printf("New round %d\n", round);
        srcField = (round % 2 == 0) ? &field0 : &field1; 
        destField = (round % 2 == 1) ? &field0 : &field1; 
        signed short x,y;
        for(x = 0; x < LCD_WIDTH; x++) {
            for(y = 0; y < LCD_HEIGHT; y++) {
                cellCount = countNeighbors(srcField->Lcd, x, y);
                if(cellCount == 3) {
                    drawPixel(destField->Lcd, 1, x, y);   
                } else if (cellCount == 2) {
                    drawPixel(destField->Lcd, readPixel(srcField->Lcd, x, y), x, y);    
                } else {
                    drawPixel(destField->Lcd, 0, x, y);   
                };
            }
        }
        printf("Calculation Done%d\n", round);
        dLcdExec(destField);
        printf("Rendering Done%d\n", round);
        round++;

        /* calculate next shot */
        t.tv_nsec += interval;

        while (t.tv_nsec >= NSEC_PER_SEC) {
            t.tv_nsec -= NSEC_PER_SEC;
            t.tv_sec++;
        }
    }
}
