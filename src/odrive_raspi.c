#include "odrive_raspi.h"

#define speed B115200//921600//230400//115200
struct odrive_Settings *od_sets;
int32_T od_f;
pthread_t od_thread;
struct odrive_Data *od_pdata;

#ifdef __cplusplus
extern "C" {
#endif

void odrive_initialize(struct odrive_Settings *settings)
{
    od_sets = settings;
    
    uint8_T portName[64];
    if (settings->isPort)
    {
        strcpy(portName, settings->portName);
    }
    else
    {
        uint8_T ret = odrive_detectOdrivePort(settings->serial, portName);
        if (ret == 0)
        {
            printf("\nThe ODrive of serial number '%s' can't be found.\n", settings->serial);
            printf("Check the serial number or connection of the ODrive.\n");
            exit(1);
        }
    }
    
    od_f = odrive_openSerialPort(portName);
    if (od_f == -1)
    {
        fprintf(stderr, "Failed to open the serial port: %s\n", strerror(errno));
		exit(1);
    }
    
    for (uint8_T i = 0; i < 2; ++i)
    {
        if (settings->isAxis[i])
        {
            odrive_startupSequence(od_f, i);
        }
    }
    
    odrive_waitSetupStatus(od_f, settings);
    
    for (uint8_T i = 0; i < 2; ++i)
    {
        if (settings->isAxis[i])
        {
            odrive_setConfiguration(od_f, i, settings);
        }
    }
    
    od_pdata = (struct odrive_Data *)malloc(sizeof(struct odrive_Data));
    for (uint8_T i = 0; i < 2; ++i)
    {
        od_pdata->error[i] = 0;
        od_pdata->actualPosition[i] = 0;
        od_pdata->actualVelocity[i] = 0;
        od_pdata->actualCurrent[i] = 0;
        od_pdata->velIntegratorCurrentAct[i] = 0;
    }
}

void odrive_step(struct odrive_Data *data)
{
    pthread_join(od_thread, NULL);
    
    for (uint8_T i = 0; i < 2; ++i)
    {
        data->error[i] = od_pdata->error[i];
        od_pdata->posSetpoint[i] = data->posSetpoint[i];
        od_pdata->velSetpoint[i] = data->velSetpoint[i];
        od_pdata->currentSetpoint[i] = data->currentSetpoint[i];
        od_pdata->posGain[i] = data->posGain[i];
        od_pdata->velGain[i] = data->velGain[i];
        od_pdata->velIntegratorGain[i] = data->velIntegratorGain[i];
        od_pdata->velIntegratorCurrentRef[i] = data->velIntegratorCurrentRef[i];
        od_pdata->velIntegratorCurrentTrigger[i] = data->velIntegratorCurrentTrigger[i];
        od_pdata->velRampEnable[i] = data->velRampEnable[i];
        od_pdata->velRampTarget[i] = data->velRampTarget[i];
        od_pdata->velRampRate[i] = data->velRampRate[i];
        od_pdata->velLimit[i] = data->velLimit[i];
        od_pdata->velLimitTolerance[i] = data->velLimitTolerance[i];
        data->actualPosition[i] = od_pdata->actualPosition[i];
        data->actualVelocity[i] = od_pdata->actualVelocity[i];
        data->actualCurrent[i] = od_pdata->actualCurrent[i];
        data->velIntegratorCurrentAct[i] = od_pdata->velIntegratorCurrentAct[i];
    }
    
    pthread_create(&od_thread, NULL, (void *)odrive_tic, (void *)od_pdata);
}

void odrive_terminate()
{
    pthread_join(od_thread, NULL);
    
    uint8_T send[64];
    for (uint8_T i = 0; i < 2; ++i)
    {
        sprintf(send, "%s%d%s %d", "w axis", i, ".requested_state", AXIS_STATE_IDLE);
        odrive_sendMessage(od_f, send);
    }
        
    fclose((FILE*)od_f);
}

void *odrive_tic(void *pdata)
{
    struct odrive_Data *data = (struct odrive_Data *)pdata;
    
    uint8_T send[64];
    uint8_T receive[64];
    union {int i; float f;}pos, vel, cur;
    
    for (uint8_T i = 0; i < 2; ++i)
    {
        if (od_sets->isAxis[i])
        {
            sprintf(send, "%s %d", "u", i);
            odrive_sendMessage(od_f, send);
            
            if (od_sets->isExternal[i])
            {
                sprintf(send, "%s%d%s %f", "w axis", i, ".controller.config.pos_gain", data->posGain[i]);
                odrive_sendMessage(od_f, send);
                
                sprintf(send, "%s%d%s %f", "w axis", i, ".controller.config.vel_gain", data->velGain[i]);
                odrive_sendMessage(od_f, send);
                
                sprintf(send, "%s%d%s %f", "w axis", i, ".controller.config.vel_integrator_gain", data->velIntegratorGain[i]);
                odrive_sendMessage(od_f, send);
                
                if (data->velIntegratorCurrentTrigger[i])
                {
                    sprintf(send, "%s%d%s %f", "w axis", i, ".controller.vel_integrator_current", data->velIntegratorCurrentRef[i]);
                    odrive_sendMessage(od_f, send);
                }
                
                if (od_sets->velRampEnable[i])
                {
                    sprintf(send, "%s%d%s %d", "w axis", i, ".controller.vel_ramp_enable", data->velRampEnable[i]);
                    odrive_sendMessage(od_f, send);
                }
                
                sprintf(send, "%s%d%s %d", "w axis", i, ".controller.config.vel_ramp_rate", data->velRampRate[i]);
                odrive_sendMessage(od_f, send);
                
                sprintf(send, "%s%d%s %f", "w axis", i, ".controller.config.vel_limit", data->velLimit[i]);
                odrive_sendMessage(od_f, send);
                
                sprintf(send, "%s%d%s %f", "w axis", i, ".controller.config.vel_limit_tolerance", data->velLimitTolerance[i]);
                odrive_sendMessage(od_f, send);
            }
            
            switch (od_sets->controlMode[i])
            {
                case CTRL_MODE_POSITION_CONTROL:
                    pos.f = data->posSetpoint[i];
                    vel.f = data->velSetpoint[i];
                    cur.f = data->currentSetpoint[i];
                    sprintf(send, "%s %d %8x %8x %8x", "p", i, pos.i, vel.i, cur.i);
//                     sprintf(send, "%s %d %f %f %f", "p", i, data->posSetpoint[i], data->velSetpoint[i], data->currentSetpoint[i]);
                    odrive_sendMessage(od_f, send);
                    break;
                    
                case CTRL_MODE_VELOCITY_CONTROL:
                    if (od_sets->velRampEnable[i])
                    {
                        sprintf(send, "%s%d%s %d", "w axis", i, ".controller.vel_ramp_target", data->velRampTarget[i]);
                        odrive_sendMessage(od_f, send);
                    }
                    else
                    {
                        vel.f = data->velSetpoint[i];
                        cur.f = data->currentSetpoint[i];
                        sprintf(send, "%s %d %8x %8x", "v", i, vel.i, cur.i);
//                         sprintf(send, "%s %d %f %f", "v", i, data->velSetpoint[i], data->currentSetpoint[i]);
                        odrive_sendMessage(od_f, send);
                    }
                    break;
                    
                case CTRL_MODE_CURRENT_CONTROL:
                    cur.f = data->currentSetpoint[i];
                    sprintf(send, "%s %d %8x", "c", i, cur.i);
//                     sprintf(send, "%s %d %f", "c", i, data->currentSetpoint[i]);
                    odrive_sendMessage(od_f, send);
                    break;
                default:
                    break;
            }
            
// //             real32_T pos;
// //             real32_T vel;
// //             union {int i; float f;}pos, vel, cur;
//             sprintf(send, "%s %d", "f", i);
//             odrive_sendMessage(od_f, send);
//             odrive_receiveMessage(od_f, receive, sizeof(receive));
// //             sscanf(receive, "%f %f", &pos, &vel);
// //             data->actualPosition[i] = pos;
// //             data->actualVelocity[i] = vel;
//             sscanf(receive, "%8x %8x %8x", &pos.i, &vel.i, &cur.i);
//             data->actualPosition[i] = pos.f;
//             data->actualVelocity[i] = vel.f;
//             data->actualCurrent[i] = cur.f;
            
//             real32_T cur;
//             sprintf(send, "%s%d%s", "r axis", i, ".motor.current_control.Iq_measured");
//             odrive_sendMessage(od_f, send);
//             odrive_receiveMessage(od_f, receive, sizeof(receive));
//             sscanf(receive, "%f", &cur);
//             data->actualCurrent[i] = cur;
            
//             real32_T velcur;
//             sprintf(send, "%s%d%s", "r axis", i, ".controller.vel_integrator_current");
//             odrive_sendMessage(od_f, send);
//             odrive_receiveMessage(od_f, receive, sizeof(receive));
//             sscanf(receive, "%f", &velcur);
//             data->velIntegratorCurrentAct[i] = velcur;
            
//             real32_T err;
//             sprintf(send, "%s%d%s", "r axis", i, ".error");
//             odrive_sendMessage(od_f, send);
//             odrive_receiveMessage(od_f, receive, sizeof(receive));
//             sscanf(receive, "%f", &err);
//             data->error[i] = err;
//             
//             if (err)
//             {
//                 odrive_terminate();
//             }
        }
    }
}

uint8_T odrive_detectOdrivePort(uint8_T *serial, uint8_T *portName)
{
    uint8_T numDetect = 0;
    struct dirent **ttyList;
    int ret = scandir("/sys/class/tty", &ttyList, NULL, NULL);
    
    for (uint8_T i = 0; i < ret; ++i)
    {
        struct stat st;        
        uint8_T fullPath[256];
        sprintf(fullPath, "%s%s%s", "/sys/class/tty/", ttyList[i]->d_name, "/device");
        
        if (stat(fullPath, &st) == 0)
        {
            uint8_T cmdline[256];
            sprintf(cmdline, "%s%s%s%s%s", "udevadm info /dev/", ttyList[i]->d_name, 
                                           " | grep '", serial, "'");
            FILE *fp = popen(cmdline, "r");
            uint8_T buf[256];
            
            if (fgets(buf, sizeof(buf), fp) != NULL)
            {
                sprintf(portName, "%s%s", "/dev/", ttyList[i]->d_name);
                ++numDetect;
            }
            
            pclose(fp);
        }
    }
    
    free(ttyList);
    return numDetect;
}

int32_T odrive_openSerialPort(uint8_T *portName)
{
    int32_T fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
    
    struct termios tty;
    
    int16_T getStatus = tcgetattr(fd, &tty);
    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);
    
    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;                 /* 8-bit characters */
    tty.c_cflag &= ~PARENB;             /* no parity bit */
    tty.c_cflag &= ~CSTOPB;             /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;            /* no hardware flowcontrol */
    
    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;
    
    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;
    
    int32_T od_setstatus = tcsetattr(fd, TCSANOW, &tty);
    
    if (fd == -1 || getStatus == -1 || od_setstatus == -1)
    {
        return -1;
    }
    else
    {
        return (int32_T) fdopen(fd, "r+");
    }
}

void odrive_startupSequence(int32_T f, uint8_T axis)
{
    uint8_T send[64];
    uint8_T receive[64];
    int32_T value;
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".motor.error", false);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".encoder.error", false);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".controller.error", false);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".error", false);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s", "r axis", axis, ".motor.is_calibrated");
    odrive_sendMessage(f, send);
    odrive_receiveMessage(f, receive, sizeof(receive));
    sscanf(receive, "%d", &value);
    
    if (value == 0)
    {
        sprintf(send, "%s%d%s %d", "w axis", axis, ".config.startup_motor_calibration", true);
        odrive_sendMessage(f, send);
    }
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".config.startup_encoder_index_search", true);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s", "r axis", axis, ".encoder.is_ready");
    odrive_sendMessage(f, send);
    odrive_receiveMessage(f, receive, sizeof(receive));
    sscanf(receive, "%d", &value);
    
    if (value == 0)
    {
        sprintf(send, "%s%d%s %d", "w axis", axis, ".config.startup_encoder_offset_calibration", true);
        odrive_sendMessage(f, send);
    }
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".requested_state", AXIS_STATE_STARTUP_SEQUENCE);
    odrive_sendMessage(f, send);
}

void odrive_waitSetupStatus(int32_T f, struct odrive_Settings *settings)
{
    for (uint8_T i = 0; i < 2; ++i)
    {
        if (settings->isAxis[i])
        {
            int32_T isMotorCalib = 0;
            int32_T isEncoder1Calib = 0;
            while(isMotorCalib == 0 || isEncoder1Calib == 0)
            {
                uint8_T send[64];
                uint8_T receive[64];
                
                sprintf(send, "%s%d%s", "r axis", i, ".motor.is_calibrated");
                odrive_sendMessage(f, send);
                odrive_receiveMessage(f, receive, sizeof(receive));
                sscanf(receive, "%d", &isMotorCalib);
                
                sprintf(send, "%s%d%s", "r axis", i, ".encoder.is_ready");
                odrive_sendMessage(f, send);
                odrive_receiveMessage(f, receive, sizeof(receive));
                sscanf(receive, "%d", &isEncoder1Calib);
            }
        }
    }
}

void odrive_setConfiguration(int32_T f, uint8_T axis, struct odrive_Settings *settings)
{
    uint8_T send[64];
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".requested_state", AXIS_STATE_IDLE);
    odrive_sendMessage(f, send);
    
    struct timespec ts1 = {0, 10 * 1000000};
    nanosleep(&ts1, &ts1);
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".controller.config.control_mode", settings->controlMode[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %f", "w axis", axis, ".controller.config.pos_gain", settings->posGain[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %f", "w axis", axis, ".controller.config.vel_gain", settings->velGain[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %f", "w axis", axis, ".controller.config.vel_integrator_gain", settings->velIntegratorGain[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %f", "w axis", axis, ".controller.config.vel_limit", settings->velLimit[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %f", "w axis", axis, ".controller.config.vel_limit_tolerance", settings->velLimitTolerance[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %f", "w axis", axis, ".controller.config.vel_ramp_rate", settings->velRampRate[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".controller.config.setpoints_in_cpr", settings->setPointsInCpr[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".controller.vel_ramp_enable", settings->velRampEnable[axis]);
    odrive_sendMessage(f, send);
    
    sprintf(send, "%s%d%s %d", "w axis", axis, ".requested_state", AXIS_STATE_CLOSED_LOOP_CONTROL);
    odrive_sendMessage(f, send);
    
    struct timespec ts2 = {0, 10 * 1000000};
    nanosleep(&ts2, &ts2);
    
    sprintf(send, "%s%d%s %f", "w axis", axis, ".config.watchdog_timeout", settings->watchdogTimeout[axis]);
    odrive_sendMessage(f, send);
}

void odrive_sendMessage(int32_T f, uint8_T *message)
{
    FILE *fp = (FILE *)f;
    fprintf(fp, "%s\n", message);
}

void odrive_receiveMessage(int32_T f, uint8_T *message, uint8_T len)
{
    FILE *fp = (FILE *)f;
    fgets(message, len, fp);
}

#ifdef __cplusplus
}
#endif