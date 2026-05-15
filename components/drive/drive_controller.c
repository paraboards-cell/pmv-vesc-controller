#include "drive_controller.h"
#include "vesc_uart.h"
#include "castor_control.h"
#include "vehicle_config.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#define TAG "drive"

static drive_mode_t  s_mode  = DRIVE_MODE_WALK;
static drive_state_t s_state = DRIVE_STATE_STOPPED;
static bool          s_parking_brake = false;

// ─── Input shaping ────────────────────────────────────────────────────────────

static float apply_deadband(float v, float db)
{
    if (fabsf(v) < db) return 0.0f;
    // Rescale so output starts at 0 just past the deadband
    return (v > 0) ? (v - db) / (1.0f - db) : (v + db) / (1.0f - db);
}

static float apply_expo(float v, float expo)
{
    // Blend linear and cubic: out = v*(1-expo) + v^3*expo
    return v * (1.0f - expo) + v * v * v * expo;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ─── Tank steering mix ────────────────────────────────────────────────────────
// throttle: [-1, +1]  (forward positive)
// steer:    [-1, +1]  (right positive)
//
// left  = throttle + steer * STEER_MIX_RATIO
// right = throttle - steer * STEER_MIX_RATIO
//
// Both outputs clamped to [-1, +1] before scaling to amps.
// When |throttle| < |steer| (pivot turn), the weaker side goes negative
// (counter-rotation) allowing zero-radius turns.

static void tank_mix(float throttle, float steer, float *left, float *right)
{
    *left  = throttle + steer * STEER_MIX_RATIO;
    *right = throttle - steer * STEER_MIX_RATIO;

    // Normalise: if either channel exceeds ±1, scale both down together
    float maxval = fmaxf(fabsf(*left), fabsf(*right));
    if (maxval > 1.0f) {
        *left  /= maxval;
        *right /= maxval;
    }
}

// ─── Mode current scale ───────────────────────────────────────────────────────

static float mode_scale(void)
{
    switch (s_mode) {
    case DRIVE_MODE_WALK:   return SPEED_MODE_WALK_FRAC;
    case DRIVE_MODE_CRUISE: return SPEED_MODE_CRUISE_FRAC;
    default:                return SPEED_MODE_SPORT_FRAC;
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void drive_controller_init(void)
{
    castor_init();
    s_state = DRIVE_STATE_STOPPED;
    ESP_LOGI(TAG, "initialized");
}

drive_output_t drive_update(const joycon_state_t *input)
{
    drive_output_t out = {0};
    out.mode  = s_mode;
    out.state = s_state;

    if (s_parking_brake || s_state == DRIVE_STATE_FAULT) {
        out.state = s_parking_brake ? DRIVE_STATE_PARKING_BRAKE : DRIVE_STATE_FAULT;
        return out;
    }

    // Speed mode cycling: Plus button increments mode
    static bool last_plus = false;
    if (input->btn_plus && !last_plus) {
        s_mode = (drive_mode_t)((s_mode + 1) % 3);
        ESP_LOGI(TAG, "speed mode -> %d", s_mode);
    }
    last_plus = input->btn_plus;

    // ZR = throttle (forward), ZL = brake/reverse
    // Use right stick Y for throttle (intuitive for a vehicle controller)
    // Use right stick X (or left stick X) for steering
    float raw_throttle = -input->right_y;  // invert: push forward = positive
    float raw_steer    =  input->right_x;

    // ZL pressed = reverse mode: invert throttle
    if (input->btn_zl && raw_throttle > 0) raw_throttle = -raw_throttle;

    // ZR as dead-man: release ZR = coasting stop
    bool dead_man = input->btn_zr;

    float throttle = apply_expo(apply_deadband(raw_throttle, THROTTLE_DEADBAND), THROTTLE_EXPO);
    float steer    = apply_expo(apply_deadband(raw_steer,    STEER_DEADBAND),    STEER_EXPO);

    if (!dead_man) throttle = 0.0f;  // dead-man released — coast

    float left_frac, right_frac;
    tank_mix(throttle, steer, &left_frac, &right_frac);

    float scale = mode_scale() * MOTOR_MAX_CURRENT_A;

    float left_a  = left_frac  * scale;
    float right_a = right_frac * scale;

    // Motor direction inversion (set in vehicle_config.h if motor runs backwards)
    if (MOTOR_LEFT_INVERT)  left_a  = -left_a;
    if (MOTOR_RIGHT_INVERT) right_a = -right_a;

    // Brake current when stick is near zero but ZR still held — regenerative
    if (dead_man && fabsf(throttle) < 0.01f && fabsf(steer) < 0.01f) {
        vesc_set_current_brake(VESC_MASTER_ID, MOTOR_BRAKE_CURRENT_A, false);
        vesc_set_current_brake(VESC_SLAVE_CAN_ID, MOTOR_BRAKE_CURRENT_A, true);
        out.state = DRIVE_STATE_BRAKING;
    } else {
        vesc_set_current(VESC_MASTER_ID, left_a,  false);
        vesc_set_current(VESC_SLAVE_CAN_ID, right_a, true);
        out.state = (fabsf(left_a) > 0.1f || fabsf(right_a) > 0.1f)
                    ? DRIVE_STATE_RUNNING : DRIVE_STATE_STOPPED;
    }

    // Castor logic: activate at low speed + high steer input
    // Speed check uses master VESC RPM (approximate — both motors similar speed)
    bool castor = (fabsf(steer) > CASTOR_STEER_THRESHOLD);
    castor_set(castor);
    out.castor_active = castor;

    out.left_current_a  = left_a;
    out.right_current_a = right_a;
    out.mode  = s_mode;
    out.state = out.state;
    return out;
}

void drive_estop(void)
{
    vesc_set_current_zero(VESC_MASTER_ID, false);
    vesc_set_current_zero(VESC_SLAVE_CAN_ID, true);
    castor_set(false);
    s_state = DRIVE_STATE_STOPPED;
    ESP_LOGW(TAG, "E-STOP");
}

void drive_parking_brake(bool engage)
{
    s_parking_brake = engage;
    if (engage) {
        vesc_set_handbrake(VESC_MASTER_ID,   MOTOR_HANDBRAKE_CURRENT_A, false);
        vesc_set_handbrake(VESC_SLAVE_CAN_ID, MOTOR_HANDBRAKE_CURRENT_A, true);
        s_state = DRIVE_STATE_PARKING_BRAKE;
        ESP_LOGI(TAG, "parking brake ON");
    } else {
        vesc_set_current_zero(VESC_MASTER_ID, false);
        vesc_set_current_zero(VESC_SLAVE_CAN_ID, true);
        s_state = DRIVE_STATE_STOPPED;
        ESP_LOGI(TAG, "parking brake OFF");
    }
}

void drive_set_mode(drive_mode_t mode) { s_mode = mode; }
drive_mode_t drive_get_mode(void)      { return s_mode; }
