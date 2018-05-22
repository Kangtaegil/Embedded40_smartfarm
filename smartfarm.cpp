#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <mysql/mysql.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <string.h>

//#include "locking.h"

#define MAXTIMINGS 85
#define FAN 22
#define LED 24
#define LIGHTSEN_OUT 2

int ret_humid, ret_temp;

//static int DHTPIN = 7;
static int DHTPIN = 11;
static int dht22_dat[5] = {0,0,0,0,0};
static int Temp[100] = {0,};
static int Light[100] = {0,};
static char Time[100][100] = {};
static int count = -1;
static int FanCheck = 0;
static int LedCheck = 0;

#define DBHOST "localhost"
#define DBUSER "root"
#define DBPASS "root"
#define DBNAME "demofarmdb"

MYSQL *connector;
MYSQL_RES *result;
MYSQL_ROW row;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void sig_handler(int signo){
	printf("Process Stop\n");
	digitalWrite(FAN, 0);
	digitalWrite(LED, 0);
	exit(0);
}

static uint8_t sizecvt(const int read)
{
  /* digitalRead() and friends from wiringpi are defined as returning a value
  < 256. However, they are returned as int() types. This is a safety function */

  if (read > 255 || read < 0)
  {
    printf("Invalid data from wiringPi library\n");
    exit(EXIT_FAILURE);
  }
  return (uint8_t)read;
}

int read_dht22_dat()
{
  uint8_t laststate = HIGH;
  uint8_t counter = 0;
  uint8_t j = 0, i;

  dht22_dat[0] = dht22_dat[1] = dht22_dat[2] = dht22_dat[3] = dht22_dat[4] = 0;

  // pull pin down for 18 milliseconds
  pinMode(DHTPIN, OUTPUT);
  digitalWrite(DHTPIN, HIGH);
  delay(10);
  digitalWrite(DHTPIN, LOW);
  delay(18);
  // then pull it up for 40 microseconds
  digitalWrite(DHTPIN, HIGH);
  delayMicroseconds(40); 
  // prepare to read the pin
  pinMode(DHTPIN, INPUT);

  // detect change and read data
  for ( i=0; i< MAXTIMINGS; i++) {
    counter = 0;
    while (sizecvt(digitalRead(DHTPIN)) == laststate) {
      counter++;
      delayMicroseconds(1);
      if (counter == 255) {
        break;
      }
    }
    laststate = sizecvt(digitalRead(DHTPIN));

    if (counter == 255) break;

    if ((i >= 4) && (i%2 == 0)) {
      dht22_dat[j/8] <<= 1;
      if (counter > 50)
        dht22_dat[j/8] |= 1;
      j++;
    }
  }

  if ((j >= 40) && (dht22_dat[4] == ((dht22_dat[0] + dht22_dat[1] + dht22_dat[2] + dht22_dat[3]) & 0xFF)) ) {
        float t, h;

        h = (float)dht22_dat[0] * 256 + (float)dht22_dat[1];
        h /= 10;
        t = (float)(dht22_dat[2] & 0x7F)* 256 + (float)dht22_dat[3];
        t /= 10.0;

        if ((dht22_dat[2] & 0x80) != 0)  t *= -1;

	ret_humid = (int)h;
	ret_temp = (int)t;
	

	return 1;
  }
  else
  {
    return 0;
  }
}

int get_light_sensor(void)
{
	// sets up the wiringPi library
	if (wiringPiSetup () < 0)
	{
		fprintf (stderr, "Unable to setup wiringPi: %s\n", strerror (errno));
		return 1;
	}

	if(digitalRead(LIGHTSEN_OUT))	//day
		return 1;
	else //night
		return 0;

}

void *GetData(void *arg){
	while(1){
        if(read_dht22_dat() == 1){
            pthread_mutex_lock(&mutex);

            struct tm *t;
            time_t timer;
            
            timer = time(NULL);
            t = localtime(&timer);
            
            count++;
            
            Temp[count] = ret_temp;
            Light[count] = get_light_sensor();
            sprintf(Time[count],"%04d-%02d-%02d %02d:%02d:%02d", t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            
            printf("Time = %s Temp = %d Light = %d\n",Time[count], Temp[count], Light[count]);
            
            if(Temp[count] >= 20){
                FanCheck = 1;
            }

            if(Light[count] == 1){
                LedCheck = 0;
            }
            
            else{
                LedCheck = 1;
            }
            
            pthread_mutex_unlock(&mutex);

            delay(1000);
        }
    }
}

void *PutData(void *arg){
    int i = 0;
    
    while(1){
        delay(10000);
        
        pthread_mutex_lock(&mutex);
        
        for(count;count >= 0;count--){
            char query[1024];
            
            sprintf(query,"insert into thl values ('%s',%d,%d)", Time[count], Temp[count],Light[count]);
            
            if(mysql_query(connector, query))
            {
                fprintf(stderr, "%s\n", mysql_error(connector));
                printf("Write DB error\n");
            }
        }
        
        pthread_mutex_unlock(&mutex);
    }
}

void *FanOn(void *arg){
    while(1){
        if(FanCheck == 1){
            digitalWrite(FAN, 1);
            delay(5000);
            digitalWrite(FAN, 0);
            FanCheck = 0;
        }
    }
}

void *LedOn(void *arg){
    while(1){
        if(LedCheck == 1)
            digitalWrite(LED, 1);
        
        else if(LedCheck == 0)
            digitalWrite(LED, 0);
    }
}

int main (void)
{

    
    pthread_t get, put, fan, led;

	DHTPIN = 11;

	connector = mysql_init(NULL);
	if (!mysql_real_connect(connector, DBHOST, DBUSER, DBPASS, DBNAME, 3306, NULL, 0))
	{
  		fprintf(stderr, "%s\n", mysql_error(connector));
  		return 0;
 	}

	printf("MySQL(rpidb) opened.\n");

	if (wiringPiSetup() == -1)
		exit(EXIT_FAILURE) ;

	if (setuid(getuid()) < 0)
	{
		perror("Dropping privileges failed\n");
		exit(EXIT_FAILURE);
	}

	pinMode(FAN, OUTPUT);
	pinMode(LED, OUTPUT);

	while (read_dht22_dat() == 0)
	{
		delay(500); // wait 1sec to refresh
	}

    pthread_create(&get, NULL, GetData, NULL);
    pthread_create(&put, NULL, PutData, NULL);
    pthread_create(&fan, NULL, FanOn, NULL);
    pthread_create(&led, NULL, LedOn, NULL);
	
    pthread_join(get, NULL);
    pthread_join(put, NULL);
    pthread_join(fan, NULL);
    pthread_join(led, NULL);
    
	return 0;
}

