#include<sys/stat.h>
#include<stdint.h>
#include<sys/types.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<linux/spi/spidev.h>
#include<getopt.h>
#include<pthread.h>
#include<linux/types.h>
#include<sys/ioctl.h>
#include<sys/socket.h>
#include<arpa/inet.h>


#define IN 0
#define OUT 1
#define PWM 2
#define LOW 0
#define HIGH 1
#define PIN 20
#define PIN2 23
#define POUT 21
#define PIN3 4
#define POUT2 21
#define VALUE_MAX 256
#define DIRECTION_MAX 35
#define MAXLINE 1024

struct sockaddr_in serveraddr;
int server_sockfd;
int client_len;
pthread_mutex_t mutex_lock;
char buf[MAXLINE];


#define ARRAY_SIZE(array) sizeof(array)/sizeof(array[0])
static const char *DEVICE ="/dev/spidev0.0";
static uint8_t MODE = SPI_MODE_0;
static uint8_t BITS = 8;
static uint32_t CLOCK = 1000000;
static uint16_t DELAY = 5;

void error_handling(char *message){
    fputs(message,stderr);
    fputc('\n',stderr);
    exit(1);
}

static int GPIOExport(int pin){
    #define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd= open("/sys/class/gpio/export", O_WRONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open export for writing!\n");
        return(-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

static int GPIOUnexport(int pin){
    char buffer[BUFFER_MAX];
    size_t bytes_written;
    int fd;

    fd=open("/sys/class/gpio/unexport", O_WRONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return(-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

static int GPIODirection(int pin, int dir){
    static const char s_direction_str[]="in\0out";
  
    char path[DIRECTION_MAX]="/sys/class/gpio/gpio%d/direction";
    int fd;
    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd=open(path, O_WRONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return(-1);
    }
    if(-1==write(fd, &s_direction_str[IN==dir ? 0:3], IN==dir?2:3)){
        fprintf(stderr, "Failed to set direction!\n");
        return(-1);
    }
    close(fd);
    return(0);
}

static int GPIORead(int pin){
    #define VALUE 30
    char path[VALUE];
    char value_str[3];
    int fd;
    snprintf(path, VALUE, "/sys/class/gpio/gpio%d/value", pin);
    fd=open(path, O_RDONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return(-1);
    }
    if(-1==read(fd, value_str, 3)){
        fprintf(stderr, "Failed to read value!\n");
        return(-1);
    }
    close(fd);
    return(atoi(value_str));
}

static int GPIOWrite(int pin, int value){
   #define VALUE_MA 30
    static const char s_value_str[]="01";
    char path[VALUE_MA];
    int fd;
    snprintf(path, VALUE_MA, "/sys/class/gpio/gpio%d/value", pin);
    fd=open(path, O_WRONLY);
    if(-1==fd){
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return(-1);
    }
    if(1!=write(fd, &s_value_str[LOW==value ? 0:1],1)){
        fprintf(stderr, "Failed to write value!\n");
        return(-1);
        
    }
    close(fd);
    return(0);
}

static int
PWMExport(int pwmnum){
#define BUFFER_MAX 3
      char buffer[BUFFER_MAX];
      int bytes_written;
      int fd;
      
      fd = open("/sys/class/pwm/pwmchip0/unexport", O_WRONLY);
      if(-1 == fd){
         fprintf(stderr, "Failed to open in unexport!\n");
         return (-1);
      }
      
      bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
      write(fd, buffer, bytes_written);
      close(fd);
      
      sleep(1);
      fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
      if(-1 == fd){
         fprintf(stderr, "Failed to open in export!\n");
         return(-1);
      }
      bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
      write(fd, buffer, bytes_written);
      close(fd);
      sleep(1);
      return(0);
}

static int
PWMEnable(int pwmnum){
   static const char s_unenable_str[] = "0";
   static const char s_enable_str[] = "1";
   #define DIRECTION_M 45
   char path[DIRECTION_M];
   int fd;
   
   
   snprintf(path, DIRECTION_M, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
   fd = open(path, O_WRONLY);
   if(-1 == fd){
      fprintf(stderr, "Failed to open in enable!\n");
      return -1;
   }
   
   write(fd, s_unenable_str, strlen(s_unenable_str));
   close(fd);
   
   fd = open(path, O_WRONLY);
   if(-1 == fd){
      fprintf(stderr, "Failed to open in enable!\n");
      return -1;
   }
   
   write(fd, s_enable_str, strlen(s_enable_str));
   close(fd);
   return (0);
   
}

static int
PWMWritePeriod(int pwmnum, int value){
   char s_values_str[VALUE_MAX];
   char path[VALUE_MAX];
   int fd, byte;
   
   //
   snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/period", pwmnum);
   fd = open(path, O_WRONLY);
   if(-1 == fd){
      fprintf(stderr, "Failed to open in period!\n");
      return(-1);
   }
   
   byte = snprintf(s_values_str, 10, "%d", value);
   if(-1 == write(fd, s_values_str, byte)){
      fprintf(stderr, "Failed to write value in period!\n");
      close(fd);
      return(-1);
   }
   
   close(fd);
   return(0);
}

static int
PWMWriteDutyCycle(int pwmnum, int value){
   char path[VALUE_MAX];
   char s_values_str[VALUE_MAX];
   int fd, byte;
   
   snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/duty_cycle", pwmnum);
   fd = open(path, O_WRONLY);
   
   if(-1 == fd){
      fprintf(stderr, "Failed to open in duty_cycle!\n");
      return(-1);
   }
   
   byte = snprintf(s_values_str, 10, "%d", value);
   
   if(-1 == write(fd, s_values_str, byte)){
      fprintf(stderr, "Failed to write value! in duty_cycle\n");
      close(fd);
      return(-1);
   }
   
   close(fd);
   return(0);
}


static int prepare(int fd) {
    if (ioctl(fd, SPI_IOC_WR_MODE, &MODE) == -1) {
        perror("Can't set MODE");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &BITS) == -1) {
        perror("Can't set number of BITS");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &CLOCK) == -1) {
        perror("Can't set write CLOCK");
        return -1;
    }
    if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &CLOCK) == -1) {
        perror("Can't set read CLOCK");
        return -1;
    }
    return 0;
}
uint8_t control_bits_differential(uint8_t channel) {
    return (channel & 7) << 4;
}
uint8_t control_bits(uint8_t channel) {
    return 0x8 | control_bits_differential(channel);
}
int readadc(int fd, uint8_t channel) {
    uint8_t tx[] = { 1, control_bits(channel), 0 };
    uint8_t rx[3];
    struct spi_ioc_transfer tr = {
       .tx_buf = (unsigned long)tx,
       .rx_buf = (unsigned long)rx,
       .len = ARRAY_SIZE(tx),
       .delay_usecs = DELAY,
       .speed_hz = CLOCK,
       .bits_per_word = BITS,
    };
    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) == 1) {
        perror("IO Error");
        abort();
    }
    return ((rx[1] << 8) & 0x300) | (rx[2] & 0xFF);
}

int f=0;
int g=0;
int b=0;
int *fire=&f;
int *gas=&g;
int *but=&b;

void *gas_thd(){
   int light=0;
    PWMExport(0);
    PWMWritePeriod(0,20000000);
    PWMWriteDutyCycle(0,0);
    PWMEnable(0);
    
    int fd=open(DEVICE, O_RDWR);
    if(fd<=0){
        printf("Device %s not found\n", DEVICE);
        return 0;
    }
     if(prepare(fd)==-1){
        return 0;
    }
    while(1){
        light=readadc(fd,0);
        g=light;
        printf("%d\n", light);
        PWMWriteDutyCycle(0, light*7500);
        usleep(400000);
    }
    close(fd);
    return 0;
}
void *fire_thd(){
   if(-1==GPIOExport(POUT2) || -1 ==GPIOExport(PIN2))
        return 0;
   
    if(-1==GPIODirection(POUT2, OUT) || -1 ==GPIODirection(PIN2, IN))
        return 0;
   
    int repeat=1000;
    

    
    do{
        if(-1==GPIOWrite(POUT2, 1))
            return 0;
        printf("I'm reading %d in GPIO %d\n", GPIORead(PIN2), PIN2);
        f=GPIORead(PIN2);
        usleep(400000);
    }
    while(repeat--);

    if(-1 == GPIOUnexport(POUT2) || -1 == GPIOUnexport(PIN2))
        return 0;
    return 0;
}
void *button_thd(){
    #define POUT3 13
   if(-1==GPIOExport(POUT3) || -1 ==GPIOExport(PIN3))
        return 0;
   
    if(-1==GPIODirection(POUT3, OUT) || -1 ==GPIODirection(PIN3, IN))
        return 0;
   
    int repeat=1000;
    

    
    do{
        if(-1==GPIOWrite(POUT3, 1))
            return 0;
        printf("I'm reading %d in GPIO %d\n", GPIORead(PIN3), PIN3);
        b=GPIORead(PIN3);
        usleep(400000);
    }
    while(repeat--);

    if(-1 == GPIOUnexport(POUT3) || -1 == GPIOUnexport(PIN3))
        return 0;
    return 0;
}



void *print(void *argv){
    
    int sock;
    struct sockaddr_in serv_addr;
    char msg[2];
   int light=0;
   
    sock=socket(PF_INET, SOCK_STREAM,0);
    if(sock==-1)
        error_handling("socket() error");
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=inet_addr("192.168.0.23");
    serv_addr.sin_port=htons(8888);
    
    if(connect(sock,(struct sockaddr*)&serv_addr,sizeof(serv_addr))==-1)
        error_handling("connect() error");
    int count=0;
    int chec=0;
    while(1){
        char buf_tmp[MAXLINE];
        memset(buf_tmp, 0x00, MAXLINE);
        buf_tmp[0] = '\0';
        int lig=0;
        if(*but==0 && chec==1){
            lig=0;
            chec=0;
            printf("Button is detected\n");
            snprintf(msg, 2, "%d", lig);
            write(sock, msg, sizeof(msg));
        }

        if(*fire==0 && *gas>50){
            count+=1;
            printf("Fire is detected\n");
        }
        else{
            printf("nice\n");
            count=0;
        }
        if(count>5){
            light=1;
            snprintf(msg, 2, "%d", light);
            write(sock, msg, sizeof(msg));
            chec=1;
            printf("Gas and Fire is both detected, send signal to 23 pie.\n");
            /*sprintf(buf_tmp, "%d\n", a);
            if (write(server_sockfd, buf_tmp, MAXLINE) <= 0)
            {
                perror("write error : ");
                exit(0);
            }
            */
        }
        
        usleep(400000);
   }

    close(sock);
    

    return 0;
}

int main(int argc, char *argv[]){

        
   
    pthread_t p_thread[4];
    int thr_id;
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";
    char p3[] = "thread_3";
    
    char p4[] = "thread_4";
    /*if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("error :");
        return 1;
    }

    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr("192.168.0.17");
    serveraddr.sin_port = htons(8888);
    */
    
    thr_id = pthread_create(&p_thread[0], NULL, fire_thd, (void *)p1);
    if(thr_id < 0){
      perror("thread create error: ");
      exit(0);
   }
   thr_id = pthread_create(&p_thread[1], NULL, gas_thd, (void *)p2);
   if(thr_id < 0){
      perror("thread create error : ");
      exit(0);
   }
   thr_id = pthread_create(&p_thread[2], NULL, print, (void *)p3);
   if(thr_id < 0){
      perror("thread create error : ");
      exit(0);
   }
   thr_id = pthread_create(&p_thread[3], NULL, button_thd, (void *)p4);
   if(thr_id < 0){
      perror("thread create error : ");
      exit(0);
   }

   pthread_join(p_thread[0], (void **)&status);
   pthread_join(p_thread[1], (void **)&status);
   pthread_join(p_thread[2], (void **)&status);
   pthread_join(p_thread[3], (void **)&status);
    
}


