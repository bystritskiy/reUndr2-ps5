/* Behavioral tests for two physical users, hotplug and per-port rumble. */
#include "pad_ps5.h"
#include "platform/win32_compat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef struct { int user[4]; } LoginUsers;
static int users[4] = {10,20,-1,-1}, initial=10, list_error;
static int live[4], borrowed[4], opened[4], closes[4], read_error[4];
static unsigned buttons[4], filter_calls[2];
static uint8_t motors[4][2];
static int initialized, pad_initialized, list_calls;
static time_t ticks = 1;
static int index_of(int user) { assert(user>=10 && user<=40 && user%10==0);return user/10-1; }
int clock_gettime(clockid_t id, struct timespec *out)
{ assert(id==CLOCK_MONOTONIC);*out=(struct timespec){.tv_sec=ticks};return 0; }
int sceUserServiceInitialize(const void *p) { assert(!p);initialized++;return 0; }
int scePadInit(void) { pad_initialized++;return 0; }
int sceUserServiceGetInitialUser(int *out) { *out=initial;return initial<0?-1:0; }
int sceUserServiceGetLoginUserIdList(LoginUsers *out)
{ list_calls++;if(list_error)return -1;memcpy(out->user,users,sizeof users);return 0; }
int scePadOpen(int user, int type, int index, const void *p)
{
    assert(!type&&!index&&!p);
    int i=index_of(user);if(borrowed[i])return -1;
    assert(!opened[i]);opened[i]=1;return 100+i;
}
int scePadGetHandle(int user, int type, int index)
{ assert(!type&&!index);int i=index_of(user);return borrowed[i]?100+i:-1; }
int scePadClose(int handle)
{ int i=handle-100;assert(i>=0&&i<4&&!borrowed[i]&&opened[i]);opened[i]=0;closes[i]++;return 0; }
int scePadSetVibrationMode(int handle, int mode)
{ assert(handle>=100&&handle<104&&mode==2);return 0; }
int scePadReadState(int handle, void *out)
{
    int i=handle-100;assert(i>=0&&i<4&&(opened[i]||borrowed[i]));
    if(read_error[i])return -1;
    Nfsu2PadData *p=out;memset(p,0,sizeof(*p));p->connected=live[i];
    p->lx=p->ly=p->rx=p->ry=128;p->buttons=buttons[i];return 0;
}
int scePadSetVibration(int handle, const void *data)
{ int i=handle-100;assert(i>=0&&i<4);memcpy(motors[i],data,2);return 0; }
static void filter(void *state, unsigned port)
{ assert(state&&port<2);filter_calls[port]++; }
extern void xbox_InputSetHostFilter(void (*)(void *,unsigned));
static XBOX_INPUT_STATE get(unsigned port, int connected)
{
    XBOX_INPUT_STATE s;memset(&s,0xcc,sizeof s);
    assert(xbox_InputGetState(port,&s)==(connected?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED));
    if(!connected){XBOX_INPUT_STATE zero={0};assert(!memcmp(&s,&zero,sizeof s));}
    return s;
}
int main(void)
{
    live[0]=live[1]=1;buttons[0]=0x4000;buttons[1]=0x2000;
    xbox_InputSetHostFilter(filter);
    XBOX_INPUT_STATE p1=get(0,1),p2=get(1,1);
    assert(p1.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255 && !p1.Gamepad.bAnalogButtons[XBOX_BUTTON_B]);
    assert(p2.Gamepad.bAnalogButtons[XBOX_BUTTON_B]==255 && !p2.Gamepad.bAnalogButtons[XBOX_BUTTON_A]);
    assert(p1.dwPacketNumber==1 && p2.dwPacketNumber==1);
    assert(filter_calls[0]==1&&filter_calls[1]==1);
    get(2,0);get(0xffffffff,0);assert(xbox_InputGetState(0,NULL)==ERROR_DEVICE_NOT_CONNECTED);
    int scans=list_calls;for(int n=0;n<20;n++)get(0,1);assert(scans==list_calls);
    XBOX_VIBRATION v1={.wLeftMotorSpeed=0xabcd,.wRightMotorSpeed=0x1234};
    XBOX_VIBRATION v2={.wLeftMotorSpeed=0x4567,.wRightMotorSpeed=0x89ab};
    assert(!xbox_InputSetState(0,&v1)&&!xbox_InputSetState(1,&v2));
    assert(motors[0][0]==0xab&&motors[0][1]==0x12&&motors[1][0]==0x45&&motors[1][1]==0x89);
    assert(xbox_InputSetState(2,&v1)==ERROR_DEVICE_NOT_CONNECTED);
    XBOX_INPUT_CAPABILITIES caps;assert(!xbox_InputGetCapabilities(1,0,&caps)&&caps.Type==1);
    /* Reordering the system list cannot swap player numbers. */
    users[0]=20;users[1]=10;ticks++;p2=get(1,1);assert(p2.Gamepad.bAnalogButtons[XBOX_BUTTON_B]);
    live[0]=0;get(0,0);get(1,1);live[0]=1;get(0,1);
    /* Mid-game disconnect/reconnect, with no stale state or rumble. */
    live[1]=0;get(1,0);assert(xbox_InputSetState(1,&v2)==ERROR_DEVICE_NOT_CONNECTED);
    live[1]=1;assert(get(1,1).dwPacketNumber>p2.dwPacketNumber);
    read_error[1]=1;get(1,0);assert(closes[1]==1&&!motors[1][0]&&!motors[1][1]);
    read_error[1]=0;ticks++;get(1,1);
    /* A transient UserService failure preserves both players. */
    list_error=1;ticks++;get(0,1);get(1,1);list_error=0;
    /* Logout evicts the old user. Skip a logged-in user without a pad,
     * and pick the next connected user (including borrowed handle cleanup). */
    users[0]=10;users[1]=30;users[2]=40;users[3]=-1;
    live[2]=0;live[3]=1;buttons[3]=0x8000;borrowed[3]=1;ticks++;
    p2=get(1,1);assert(p2.Gamepad.bAnalogButtons[XBOX_BUTTON_X]==255);
    assert(!opened[1]&&!opened[2]);
    assert(!xbox_InputSetState(1,&v2)&&motors[3][0]==0x45);
    /* No newly assigned pad can take a still-logged-in player's slot 0. */
    live[0]=0;ticks++;get(0,0);get(1,1);live[0]=1;
    nfsu2_ps5_input_shutdown();
    assert(!opened[0]&&!opened[1]&&!opened[2]&&!opened[3]);
    assert(closes[3]==0&&!motors[3][0]&&!motors[3][1]);
    /* Initial-user fallback if enumeration is temporarily unavailable. */
    list_error=1;get(0,1);get(1,0);list_error=0;
    users[1]=20;users[2]=-1;live[1]=1;ticks++;get(1,1);
    assert(initialized==1&&pad_initialized==1);
    nfsu2_ps5_input_shutdown();
    puts("Two-user DualSense input: independent buttons/rumble, stable slots, hotplug, logout, failures and ownership: pass");
}
