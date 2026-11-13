/* reUndr2 PS5, MIT. Two independent Xbox ports through public libScePad. */
#include "pad_ps5.h"
#include "platform/win32_compat.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
extern int scePadInit(void);
extern int scePadOpen(int, int, int, const void *);
extern int scePadGetHandle(int, int, int);
extern int scePadClose(int);
extern int scePadReadState(int, void *);
extern int scePadSetVibration(int, const void *);
extern int scePadSetVibrationMode(int, int);
extern int sceUserServiceInitialize(const void *);
extern int sceUserServiceGetInitialUser(int *);
/* Public UserService ABI: four signed user IDs; unused entries are -1. */
typedef struct { int user[4]; } LoginUsers;
extern int sceUserServiceGetLoginUserIdList(LoginUsers *);
#define PAD_PORTS 2
#define INVALID_USER (-1)
typedef struct {
    int user, handle, owned, connected;
    DWORD packet;
} PadSlot;
static PadSlot slots[PAD_PORTS] = {
    {.user=INVALID_USER, .handle=-1}, {.user=INVALID_USER, .handle=-1}
};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static uint64_t next_scan;
static void (*host_filter)(void *, unsigned);
/* Port menu (port_menu.cpp): while it is open, and until the buttons that
 * opened or closed it are released, the title sees no buttons from pad 1. */
#define ALL_BUTTONS 0x0011FFFEu
static volatile int hold_game, hold_menu;
/* Touchpad + Options opens the menu, but alone they are Back (reset the car)
 * and Start. Pad 1's press of either reaches the title COMBO_MS late: if the
 * other joins in that time, neither does. A tap shorter than that is still
 * seen, as a press of TAP_MS. */
#define COMBO_BUTTONS 0x00100008u
#define COMBO_MS 100
#define TAP_MS 50
static uint64_t now_ms(void);
static uint32_t combo_filter(uint32_t raw)
{
    static uint32_t passed, pend, pulse;
    static uint64_t pend_at, pulse_until;
    static int swallow;
    uint64_t now = now_ms();
    uint32_t b = raw & COMBO_BUTTONS, out;
    if (b == COMBO_BUTTONS) { swallow = 1; pend = passed = 0; }
    if (swallow) {
        if (!b) swallow = 0;
        return raw & ~COMBO_BUTTONS;
    }
    uint32_t newly = b & ~(passed | pend);
    if (newly) { if (!pend) pend_at = now; pend |= newly; }
    uint32_t gone = pend & ~b;
    if (gone) { pulse |= gone; pulse_until = now + TAP_MS; pend &= ~gone; }
    if (pend && now - pend_at >= COMBO_MS) { passed |= pend; pend = 0; }
    passed &= b;
    if (now >= pulse_until) pulse = 0;
    out = passed | pulse;
    return (raw & ~COMBO_BUTTONS) | out;
}
static void init_services(void)
{
    sceUserServiceInitialize(NULL);
    scePadInit();
}
void xbox_InputInit(void) { pthread_once(&once, init_services); }
void xbox_InputSetHostFilter(void (*filter)(void *, unsigned))
{
    pthread_mutex_lock(&lock); host_filter = filter; pthread_mutex_unlock(&lock);
}
static int valid_user(int user) { return user >= 0 && user != 255; }
static uint64_t now_ms(void)
{
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static void connection(PadSlot *slot, int connected)
{
    if (slot->connected != connected) {
        fprintf(stderr, "[pad] player %u %s\n", (unsigned)(slot-slots)+1,
                connected ? "connected" : "disconnected");
        slot->connected = connected;
    }
}
static void release(PadSlot *slot)
{
    if (slot->handle >= 0) {
        const uint8_t stop[2] = {0,0};
        scePadSetVibration(slot->handle, stop);
        if (slot->owned) scePadClose(slot->handle);
    }
    connection(slot, 0);
    slot->handle = -1; slot->owned = 0;
}
static void open_slot(PadSlot *slot)
{
    if (slot->handle >= 0 || !valid_user(slot->user)) return;
    slot->handle = scePadOpen(slot->user, 0, 0, NULL);
    slot->owned = slot->handle >= 0;
    if (slot->handle < 0) slot->handle = scePadGetHandle(slot->user, 0, 0);
    if (slot->handle >= 0) scePadSetVibrationMode(slot->handle, 2);
}
static int read_slot(PadSlot *slot, Nfsu2PadData *data)
{
    memset(data, 0, sizeof(*data));
    if (slot->handle < 0) return 0;
    if (scePadReadState(slot->handle, data) < 0) {
        release(slot);
        return 0;
    }
    connection(slot, data->connected != 0);
    return slot->connected;
}
static int contains(const LoginUsers *users, int user)
{
    for (unsigned i=0; i<4; ++i) if (users->user[i] == user) return 1;
    return 0;
}
static void scan_locked(void)
{
    uint64_t now = now_ms();
    if (now < next_scan) return;
    next_scan = now + 1000;
    LoginUsers users = {{-1,-1,-1,-1}};
    int listed = sceUserServiceGetLoginUserIdList(&users) >= 0;
    int initial = INVALID_USER;
    sceUserServiceGetInitialUser(&initial);
    /* Keep player 1 on the initial user's slot, including while its pad is
     * disconnected. A login-list failure must not evict existing players. */
    for (unsigned i=0; i<PAD_PORTS; ++i) {
        PadSlot *slot = &slots[i];
        if (listed && valid_user(slot->user) && !contains(&users, slot->user)) {
            release(slot); slot->user = INVALID_USER;
        }
    }
    if (!valid_user(slots[0].user) && valid_user(initial)
            && (!listed || contains(&users, initial))) {
        /* Never bind the same user to both ports, even after a login change. */
        if (slots[1].user == initial) {
            release(&slots[1]); slots[1].user = INVALID_USER;
        }
        slots[0].user = initial;
    }
    for (unsigned i=0; i<PAD_PORTS; ++i) open_slot(&slots[i]);
    if (!listed) return;
    Nfsu2PadData probe;
    if (read_slot(&slots[1], &probe)) return;
    /* Probe all other logged-in users. A user without a connected pad must
     * not occupy player 2 and hide a later user's physical controller. */
    for (unsigned i=0; i<4; ++i) {
        int user = users.user[i];
        if (!valid_user(user) || user == slots[0].user || user == slots[1].user) continue;
        PadSlot candidate = {.user=user, .handle=-1};
        open_slot(&candidate);
        memset(&probe, 0, sizeof(probe));
        if (candidate.handle >= 0 && scePadReadState(candidate.handle, &probe) >= 0
                && probe.connected) {
            release(&slots[1]);
            /* Packet counters belong to Xbox ports, not physical pads. */
            candidate.packet = slots[1].packet;
            slots[1] = candidate;
            connection(&slots[1], 1);
            break;
        }
        /* candidate is not in slots: do not emit a connection log for it. */
        if (candidate.handle >= 0 && candidate.owned) scePadClose(candidate.handle);
    }
}
DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *state)
{
    if (state) memset(state, 0, sizeof(*state));
    if (port >= PAD_PORTS || !state) return ERROR_DEVICE_NOT_CONNECTED;
    xbox_InputInit();
    Nfsu2PadData data;
    pthread_mutex_lock(&lock);
    scan_locked();
    int ok = read_slot(&slots[port], &data);
    void (*filter)(void *, unsigned) = host_filter;
    if (ok) {
        /* The port menu's buttons stay with it until every one is let go. */
        if (port == 0 && hold_game) {
            if (!hold_menu && !(data.buttons & ALL_BUTTONS)) hold_game = 0;
            else data.buttons = 0;
        }
        if (port == 0) data.buttons = combo_filter(data.buttons);
        nfsu2_ps5_translate_pad(&data, &state->Gamepad);
        state->dwPacketNumber = ++slots[port].packet;
    }
    pthread_mutex_unlock(&lock);
    if (ok && filter) filter(state, port);
    return ok ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}
BOOL xbox_InputIsConnected(DWORD port)
{
    XBOX_INPUT_STATE state;
    return xbox_InputGetState(port, &state) == ERROR_SUCCESS;
}
DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *vibration)
{
    if (port >= PAD_PORTS || !vibration) return ERROR_DEVICE_NOT_CONNECTED;
    xbox_InputInit();
    uint8_t motors[2] = { vibration->wLeftMotorSpeed >> 8, vibration->wRightMotorSpeed >> 8 };
    Nfsu2PadData data;
    pthread_mutex_lock(&lock);
    scan_locked();
    int rc = read_slot(&slots[port], &data)
        ? scePadSetVibration(slots[port].handle, motors) : -1;
    pthread_mutex_unlock(&lock);
    return rc >= 0 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}
DWORD xbox_InputGetCapabilities(DWORD port, DWORD flags, XBOX_INPUT_CAPABILITIES *caps)
{
    (void)flags;
    if (caps) memset(caps, 0, sizeof(*caps));
    if (!caps || !xbox_InputIsConnected(port)) return ERROR_DEVICE_NOT_CONNECTED;
    caps->Type = 1; caps->SubType = 1;
    return ERROR_SUCCESS;
}
int nfsu2_movie_skip_down(void)
{
    XBOX_INPUT_STATE state;
    return xbox_InputGetState(0, &state) == ERROR_SUCCESS && state.Gamepad.bAnalogButtons[XBOX_BUTTON_A] > 30;
}
void nfsu2_ps5_input_shutdown(void)
{
    pthread_mutex_lock(&lock);
    for (unsigned i=0; i<PAD_PORTS; ++i) {
        release(&slots[i]); slots[i].user = INVALID_USER;
    }
    next_scan = 0;
    pthread_mutex_unlock(&lock);
}

/* Pad 1's raw buttons for the port menu; 0 when it is not connected. */
uint32_t nfsu2_ps5_pad_buttons(void)
{
    Nfsu2PadData data;
    uint32_t b = 0;
    xbox_InputInit();
    pthread_mutex_lock(&lock);
    scan_locked();
    if (read_slot(&slots[0], &data)) b = data.buttons;
    pthread_mutex_unlock(&lock);
    return b;
}
void nfsu2_ps5_pad_hold(int menu_open)
{
    hold_menu = menu_open != 0;
    hold_game = 1;
}
