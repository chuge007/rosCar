#ifndef __MOTOR_CONTROL_H
#define __MOTOR_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Motor_SensorCallback)(float roll, float pitch, float yaw);


int  Motor_Init(unsigned long port1, unsigned long baud1,
                unsigned long port2, unsigned long baud2);


void Motor_Uninit(void);


void Motor_Start(char addr, int speed);                 
void Motor_Stop(char addr);                             
void Motor_SetSpeed(char addr, unsigned int speed);     
void Motor_GoStraight(unsigned int speed);              
void Motor_SetPlace(unsigned int place, int speed = 40000);     
void Motor_SetZero(char addr);                          


void Motor_QueryPlace(char addr);                       
void Motor_QueryAngle(void);                            
void Motor_QueryEncoder(void);                          
void Motor_QuerySensor(void);                           

int  Motor_GetLeftPlace(void);      
int  Motor_GetRightPlace(void);     
int  Motor_GetAngle(void);          
int  Motor_GetEncoderPlace(void);   

void Motor_RegisterSensorCallback(Motor_SensorCallback cb);  
int  Motor_SendRaw(int port, const char *data, int len);     

#ifdef __cplusplus
}
#endif

#endif 
