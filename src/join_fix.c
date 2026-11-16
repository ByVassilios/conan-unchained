// Stuck join fix: separate step counter for the teleport wait
// AConanPlayerController's "Waiting for streaming completed (%s)" (0x431350) keeps its step (0,1,2)
// in ONE int at pc+0x11C0, but two waits use it: "game" (join; Done -> ClientCompleteSpawn removes the
// loading screen) and "teleport". If the teleport wait reaches Done first it resets the shared step to
// 0, the game wait never logs Done and the loading screen stays forever. Fix: the teleport wait gets
// its own step counter (swapped in around its call); the game wait keeps the original one.

#include <math.h>
#include "unchained.h"

typedef char (*Wait_t)(void *pc, const wchar_t *label);
static Wait_t original_wait;
static void *teleport_controller; static int teleport_step;   // there's only one local player controller; reset if it changes
#define WAIT_RVA 0x431350
#define PC_WAITSTEP 0x11C0
#define PC_TRANSITION 0x1494    // byte: 2 = join waits for streaming once the camera is near the target
#define PC_JOINTARGET 0x1498    // FVector target of that wait
#define VT_VIEWPOINT 0x6D8      // APlayerController::GetPlayerViewPoint(FVector*, FRotator*), used by the check
typedef void (*ViewPoint_t)(void *pc, float *loc, float *rot);
static const short WAIT_SIG[] = {
    0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x01,0x48,0x8b,0xf2,
    0x48,0x8b,0xd9,0xff,0x90,0x38,0x01,0x00,0x00};

/* Replacement for the controller's "Waiting for streaming completed" step. Gives the teleport wait
   its own step counter and, after a teleport, moves the join target to the camera if it's out of
   range. Game thread only. */
static char wait_hook(void *pc, const wchar_t *label) {
    if (!pc || !label || wcscmp(label, L"teleport")) return original_wait(pc, label);
    if (pc != teleport_controller) { teleport_controller = pc; teleport_step = 0; }
    int *step = (int *)((char *)pc + PC_WAITSTEP), game = *step;
    *step = teleport_step;
    char r = original_wait(pc, label);
    teleport_step = *step; *step = game;
    if (r && game) log_line("teleport wait done while join wait was at step %d: join wait kept\n", game);
    // The join wait only runs while the camera is within 2000 units (XY) of its target (pc+0x1498, tick
    // 0x469AF2). A teleport can drop the player further away, so it never runs again. The teleport wait
    // just finished streaming at the new spot, so the join target moves there (XY only, what's compared).
    if (r && *((unsigned char *)pc + PC_TRANSITION) == 2) {
        float loc[3] = {0}, rot[3] = {0}, *tgt = (float *)((char *)pc + PC_JOINTARGET);
        (*(ViewPoint_t *)(*(char **)pc + VT_VIEWPOINT))(pc, loc, rot);
        float dx = loc[0] - tgt[0], dy = loc[1] - tgt[1];
        if (dx * dx + dy * dy >= 4000000.0f) {
            log_line("join target (%.0f, %.0f) is %.0f units from the player after teleport: moved to (%.0f, %.0f)\n",
                  tgt[0], tgt[1], sqrtf(dx * dx + dy * dy), loc[0], loc[1]);
            tgt[0] = loc[0]; tgt[1] = loc[1];
        }
    }
    return r;
}

/* Hooks the streaming wait function. */
int install_join_fix(void) {
    unsigned char *p = find_signature(WAIT_SIG, sizeof WAIT_SIG / sizeof WAIT_SIG[0], WAIT_RVA);
    if (!p) { log_line("stuck join fix: signature not found\n"); return 0; }
    if (!(original_wait = (Wait_t)hook_function(p, 15, (void *)wait_hook))) return 0;
    log_line("stuck join fix installed\n");
    return 1;
}
