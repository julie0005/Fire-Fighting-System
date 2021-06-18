#include<sys/stat.h>
#include<stdint.h>
#include<sys/types.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<pthread.h>
#include<linux/spi/spidev.h>
#include<getopt.h>
#include<linux/types.h>
#include<sys/ioctl.h>

#include<arpa/inet.h>
#include<sys/socket.h>

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define PWM 2
#define PIN 20		//button1
#define PIN2 21		//button2
#define POUT 17		//led
#define POUT1 18	//buzzer

#define VALUE_MAX 256

#define ARRAY_SIZE(array) sizeof(array)/sizeof(array[0])
static const char *DEVICE ="/dev/spidev0.0";
static uint8_t MODE = SPI_MODE_0;
static uint8_t BITS = 8;
static uint32_t CLOCK = 1000000;
static uint16_t DELAY = 5;

int serv_sock,clnt_sock=-1;
int serv_sock2, clnt_sock2=-1;
struct sockaddr_in serv_addr, clnt_addr, serv_addr2, clnt_addr2;
socklen_t clnt_addr_size, clnt_addr_size2;
int isFire=0;
int button_val=0;
int prev_state=0;
int so_num=2;

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
    #define DIRECTION_MAX2 35
    char path[DIRECTION_MAX2]="/sys/class/gpio/gpio%d/direction";
    int fd;
    snprintf(path, DIRECTION_MAX2, "/sys/class/gpio/gpio%d/direction", pin);
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
    #define VALUE_MAX2 30
    char path[VALUE_MAX2];
    char value_str[3];
    int fd;
    snprintf(path, VALUE_MAX2, "/sys/class/gpio/gpio%d/value", pin);
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
    static const char s_values_str[] = "01";
    char path[VALUE_MAX2];
    int fd;
	
    snprintf(path, VALUE_MAX2, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if(-1==fd){
	fprintf(stderr, "Failed to open gpio value for writing!\n");
	return(-1);
    }
    if(1 != write(fd, &s_values_str[LOW == value ? 0:1], 1)){
	fprintf(stderr, "failed to write value!\n");
	return(-1);
	
	close(fd);
	return(0);
    }
}

static int
PWMExport(int pwmnum){
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

static int PWMEnable(int pwmnum){
   static const char s_unenable_str[] = "0";
   static const char s_enable_str[] = "1";
   
#define DIRECTION_MAX 45
   char path[DIRECTION_MAX];
   int fd;
   
   //
   snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
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

static int PWMWritePeriod(int pwmnum, int value){
   char s_values_str[VALUE_MAX];
   char path[VALUE_MAX];
   int fd, byte;
   
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

static int PWMWriteDutyCycle(int pwmnum, int value){
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

void *press_thd(){
    
    //Enable GPIO pins
    if(-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN) || -1 == GPIOExport(PIN2)){
        printf("gpio export err\n");
        exit(0);
    }
    usleep(100000);
    
    //Set GPIO directions
    if(-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN) || -1 == GPIODirection(PIN2, IN)){
        printf("gpio direction err\n");
        exit(0);
    }
    GPIOWrite(POUT, 0);
    usleep(10000);
    
    int fd=open(DEVICE, O_RDWR);
    if(fd<=0){
        printf("Device %s not found\n", DEVICE);
        exit(0);
    }
    if(prepare(fd)==-1){
	printf("prepare error-line 331\n");
        exit(0);
    }
    
    PWMExport(0);
    PWMWritePeriod(0, 2000000);
    PWMWriteDutyCycle(0, 0);
    PWMEnable(0);
	    
    while(1){
	if(!isFire){
	    GPIOWrite(POUT, 0);
	    PWMWriteDutyCycle(0,0);
	    usleep(50000);
	    printf("turned off when it's gone\n");
	    break;
	}
	int press;
        press=readadc(fd,0);
        printf("%d\n", press);
	if(press > 20){ //pressure detected
	    printf("pressure detected\n");
	    
	    //led on
	    GPIOWrite(POUT, 1);
	    
	    //buzzer on
	    PWMWriteDutyCycle(0, 10000);
	    usleep(50000);
	    printf("1\n");
	    
	    
	}
	if(press <= 20){ //pressure undetected
	    printf("pressure undetected\n");
	    
	    //led off
	    GPIOWrite(POUT, 0);
	    
	    //buzzer off
	    PWMWriteDutyCycle(0,0);
	    usleep(50000);
	    printf("0\n");
	    
	    
	}
        usleep(500000);
    }
    close(fd);
    
    
}
char msg[3]={0};

typedef struct{
    int clnt_sock;
    int button_val;
}bt_arg;

pthread_mutex_t mutex_lock;

void *button_thd(void * arg){
    printf("button thread printed\n");
    
    while(1){
	printf("start thread : %d\n",((bt_arg*)arg)->button_val);
	pthread_mutex_lock(&mutex_lock);
	if(!isFire) {printf("escape button_thd %d\n",((bt_arg*)arg)->button_val); pthread_mutex_unlock(&mutex_lock); break; }
	bt_arg bi;
	bi=*((bt_arg*)arg);
	int gpio=19+bi.button_val;
	
	printf("gpio : %d\nthread : %d\nGPIORead(gpio) : %d\n",gpio,bi.button_val,GPIORead(gpio));
	if(GPIORead(gpio) == 1){
	    printf("button%d pushed\n",bi.button_val);
	    //LCD print
	    msg[1] = bi.button_val + '0';
	    write(bi.clnt_sock,msg,sizeof(msg));
	}
	usleep(100000);
	pthread_mutex_unlock(&mutex_lock);
	printf("end thread : %d\n",bi.button_val);
    }
    
}
void error_handling(char *message){
   fputs(message, stderr);
   fputc('\n', stderr);
   exit(1);
}


void * soc1_thread(void * arg){
    //07 pie
    bt_arg bi[so_num+1];
    int clnt=*((int*)arg);
	  while(1){
	    printf("isFire is %d\n",isFire);
	    msg[0]=isFire+'0';
	    msg[1]=0+'0';
	    
	    //which button is pressed?
	    if(isFire){
		//buzzer on, led on
		write(clnt,msg,sizeof(msg));
		
		printf("hi\n");
		pthread_t p_thread[so_num+1];
		int thr_id;
		int status;
		char p1[] = "pressure";
		
		thr_id = pthread_create(&p_thread[0], NULL, press_thd, (void *)p1);
		if(thr_id < 0){
		  perror("thread create error: ");
		  exit(0);
	        }
		for(int i=1; i<so_num+1; i++){
		 printf("button thread %d in soc1_thread\n",i);
		 bi[i-1].clnt_sock=clnt;
		 bi[i-1].button_val=i;
		 thr_id=pthread_create(&p_thread[i],NULL, button_thd, (void *)&bi[i-1]);
		 if(thr_id<0){
		     perror("thread create error: ");
		     exit(0);
		 }
		}
		write(clnt,msg,sizeof(msg));
		for(int i=0; i<so_num+1; i++){
		    
		    pthread_join(p_thread[i], (void **)&status);
		    printf("join waiting !! \n");
		}
		
	    }
	    else if(isFire==0 && prev_state==1){
		write(clnt,msg,sizeof(msg));
	    }
	  }	
}

void * soc2_thread(void * arg){
    int clnt_sock=*((int*)arg);
    printf("%d",clnt_sock);
    int str_len;
    char msg[2];
    
	while(1){

	    str_len=read(clnt_sock,msg,sizeof(msg));
	    if(str_len==-1)
		error_handling("read error\n");
	    
	    printf("Receive message from 22 : %s\n",msg);
	    printf("change to integer : %d\n",atoi(msg));
	    if(strlen(msg)>0 && atoi(msg)==1){
		printf("isFire is changed to 1, PREV_STATE=0\n");
		prev_state=0;
		isFire=1;
		
	    }	    
	    else if(strlen(msg)>0 && atoi(msg)==0){
		/* if end button is pressed, isFire=0*/
		printf("is Fire is Changed to 0, PREV_STATE=1\n");
		prev_state=1;
		isFire=0;
	    }
	}
}

int main(int argc, char *argv[]){
   int state=1;
   int prev_state=1;
   int light=0;
   int i=0;
   
   pthread_t sock_1, sock_2;
   int p_id;
   
   char msg[2];
   pthread_mutex_unlock(&mutex_lock);
   
   if(argc!=2){
      printf("Usage : %s <port>\n", argv[0]);
   }
   serv_sock=socket(AF_INET, SOCK_STREAM, 0);
   if(serv_sock==-1)
      error_handling("socket() error");
   memset(&serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_family=AF_INET;
   serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);
   serv_addr.sin_port=htons(atoi(argv[1]));
   
   if(bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr))==-1)
      error_handling("bind()1 error");
	  
    if(listen(serv_sock, 5)==-1)
	  error_handling("listen() error");
    
    while(1){
      clnt_addr_size=sizeof(clnt_addr);
      clnt_sock=accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
      if(clnt_sock==-1)
	 error_handling("accept() error");
	
      printf("%s\n",inet_ntoa(clnt_addr.sin_addr));
      if(strcmp(inet_ntoa(clnt_addr.sin_addr),"192.168.0.7")==0){
	  printf("192.168.0.7 is connected\n");
	  pthread_create(&sock_1,NULL,soc1_thread,(void *)&clnt_sock);
	  i++;	
      }
      else if(strcmp(inet_ntoa(clnt_addr.sin_addr),"192.168.0.22")==0){
	  printf("192.168.0.22 is connected\n");
	  pthread_create(&sock_2,NULL,soc2_thread,(void *)&clnt_sock);
	  i++;
      }
      if(i>=2){
	 break;
       }
    }
    
    pthread_join(sock_1,NULL);
    pthread_join(sock_2,NULL);    
  
}
