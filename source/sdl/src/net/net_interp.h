#ifndef NET_INTERP_H
#define NET_INTERP_H

/* Network state interpolation for remote-player rendering smoothing.
 *
 * Remote player state arrives at the simulation rate (~30 Hz), with UDP
 * jitter. Each peer simulates its own player; interpolation smooths only
 * remote players, on both hosts and clients.
 *
 * NetInterpReset() — clear all slot history on race entry and session close.
 *   Slots can belong to different players, including the local player,
 *   when a later session assigns a different join order.
 *
 * NetInterpRecord(i) — call once after each remote state update, with the
 *   player index that was just overwritten in g_playerBase.  Captures the
 *   freshly-written state as the new "curr" snapshot and rotates the prior
 *   curr into "prev".
 *
 * NetInterpApply() — call once per race-loop iteration, after
 *   network packet processing and before any code that reads player state
 *   for camera/render.  Lerps prev->curr into g_playerBase for posX/Y/Z and
 *   the four angle fields.  Renders one packet interval behind so we
 *   interpolate, with extrapolation capped at half an interval on packet loss.
 *   The local player is never modified.
 *
 * Other player fields (ringCount, lapsCompleted, animId, etc.) are left
 * untouched — they snap at packet rate, which is fine visually.
 */
void NetInterpReset(void);
void NetInterpRecord(int playerIdx);
void NetInterpApply(void);

#endif
