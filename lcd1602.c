#include <stdio.h>
#include <unistd.h>            //Needed for I2C port
#include <fcntl.h>            //Needed for I2C port
#include <sys/ioctl.h>         //Needed for I2C port
#include <linux/i2c-dev.h>      //Needed for I2C port
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define IN  0
#define OUT 1
#define PWM 0

#define LOW  0
#define HIGH 1
#define VALUE_MAX    256

#define i2c_addr 0x3f
#define LCD_WIDTH 16

#define LCD_CHR 1
#define LCD_CMD 0

#define LCD_LINE_1 0x80
#define LCD_LINE_2 0xC0
#define LCD_LINE_3 0x94
#define LCD_LINE_4 0xD4

#define LCD_ON 0x08

#define ENABLE 0b00000100

#define E_delay 0.0005
#define E_pulse 0.0005


int start = 0;
int isFire=0;
int isEnd=0;
int prev_state=0;
int button_val=0;
char argv1[20], argv2[20];

void error_handling(char *message)
{
   fputs(message, stderr);
   fputc('\n', stderr);
   exit(1);
}

int file_i2c;

unsigned char buffer[17] = { 0, };
int length = 0;

static void OPEN_I2C_BUS()
{
   char *filename = (char*)"/dev/i2c-1";
   if ((file_i2c = open(filename, O_RDWR)) < 0)
   {
      //ERROR HANDLING: you can check errno to see what went wrong
      printf("Failed to open the i2c bus");
   }
   int addr = i2c_addr;          //<<<<<The I2C address of the slave
   if (ioctl(file_i2c, I2C_SLAVE, addr) < 0)
   {
      printf("Failed to acquire bus access and/or talk to slave.\n");
      //ERROR HANDLING; you can check errno to see what went wrong
   }
}

static int WRITE_BYTES(int addr, int output)
{
   length = 1;         //<<< Number of bytes to write
   ioctl(file_i2c, 0, i2c_addr);
   if (write(file_i2c, &output, length) != length)
      //write() returns the number of bytes actually written, 
      //if it doesn't match then an error occurred (e.g. no response from the device)
   {
      /* ERROR HANDLING: i2c transaction failed */
      printf("Failed to write to the i2c bus.\n");
   }
   return 0;
}

static void lcd_toggle_enable(int bits)
{
   usleep(E_delay);
   WRITE_BYTES(i2c_addr, (bits | ENABLE));
   usleep(E_pulse);
   WRITE_BYTES(i2c_addr, (bits & ~ENABLE));
   usleep(E_delay);

}
static void LCD_BYTE(int bits, int mode) // bits = ascii code
{ 
   unsigned int bits_high = mode | (bits & 0xF0) | LCD_ON;
   unsigned int bits_low = mode | ((bits << 4) & 0xF0) | LCD_ON;
    
   //HIGH BITS
   WRITE_BYTES(i2c_addr, bits_high);
   lcd_toggle_enable(bits_high);
   //LOW BITS
   WRITE_BYTES(i2c_addr, bits_low);
   lcd_toggle_enable(bits_low);

}
static void LCD_INIT()
{

   LCD_BYTE(0x33, LCD_CMD);
   LCD_BYTE(0x32, LCD_CMD);
   LCD_BYTE(0x06, LCD_CMD);
   LCD_BYTE(0x0C, LCD_CMD);
   LCD_BYTE(0x28, LCD_CMD);
   LCD_BYTE(0x01, LCD_CMD);

   usleep(E_delay);

}
static void lcd_string(char buffer[], int line)
{
   LCD_BYTE(line, LCD_CMD);
    char value;
   for (int i = 0; i < LCD_WIDTH; i++)
   {   if(i>=strlen(buffer)){
            value=' ';
        }
        else{
            value=(char)buffer[i];
        }
      LCD_BYTE(value, LCD_CHR);
   }
}


void * lcd()
{   
   char buf[16];
   while (1)
   {
      LCD_INIT();
      sleep(1);
      
      if(isFire==1){
         if(button_val==0){
            lcd_string("Fire Breaks OUT! ", LCD_LINE_1);
         }
         else{
            sprintf(buf,"button %d ",button_val);
            printf("%s\n",buf);
            lcd_string("Location : ", LCD_LINE_1);
            lcd_string(buf, LCD_LINE_2);
         }
         prev_state=isFire;
      }
      else if(isFire==0){
         
         if(prev_state==1 || isEnd){
            isEnd=1;
            lcd_string("Fire is Ended",LCD_LINE_1);
         }
         else if(prev_state==0){
            lcd_string("Nothing Happens ",LCD_LINE_1);
         }
         prev_state=isFire;
      }
      sleep(1);
      LCD_INIT();
   }
   LCD_BYTE(0x01, LCD_CMD);
   exit(0);

}

int main(int argc, char *argv[])
{
   int sock;
    struct sockaddr_in serv_addr;
    char msg[3];
    int str_len;
    int light=0;

   pthread_t lcd_td;

   OPEN_I2C_BUS();
   LCD_INIT();

    if(argc!=3){
        printf("Usage : %s <IP> <port>\n",argv[0]);
        exit(1);
    }

   sock=socket(PF_INET, SOCK_STREAM,0);
    if(sock==-1)
        error_handling("socket() error");
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=inet_addr(argv[1]);
    serv_addr.sin_port=htons(atoi(argv[2]));

    if(connect(sock,(struct sockaddr*)&serv_addr,sizeof(serv_addr))==-1)
        error_handling("connect() error");
   
   printf("lcd working\n");
   pthread_create(&lcd_td,NULL,lcd,NULL);
   
   while(1){
        str_len=read(sock,msg,sizeof(msg));
      isFire=msg[0]-'0';
      button_val=atoi(&msg[1]);
      printf("isFire : %d, button_val : %d\n",isFire,button_val);
        if(str_len==-1)
            error_handling("read() error");
        
    }

   pthread_join(lcd_td,NULL);
    close(sock);
    
	return 0;
}
