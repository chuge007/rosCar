"""Offline algorithm study, NOT a Qt/controller or hardware acceptance test.

Compares image-only preview steering with direct lateral P and local-path
feed-forward in a planar differential-drive model. Units are metres, seconds,
radians; positive yaw turns right. No external libraries or hardware I/O.
"""
import json
import math
from collections import deque


def clip(x, lo, hi):
    return max(lo, min(x, hi))


def simulate(method, speed, initial_error, delay, heading=0.0):
    dt, lookahead, track, gain = 0.01, 0.25, 0.1473, 1.8
    y, yaw, actual_w, command_w = -initial_error, heading, 0.0, 0.0
    first_e = -y / math.cos(yaw) - lookahead * math.tan(yaw)
    filtered_e, signed_rate, last_e = first_e, 0.0, first_e
    history = deque([first_e] * (round(delay / dt) + 1))
    errors, turns = [], []
    for _ in range(round(1.5 / speed / dt)):
        e = -y / math.cos(yaw) - lookahead * math.tan(yaw)
        history.append(e)
        observed_e = history.popleft()
        filtered_e += (1 - math.exp(-dt / 0.2)) * (observed_e - filtered_e)
        signed_rate += (1 - math.exp(-dt / 0.3)) * (
            clip((observed_e - last_e) / dt, -0.08, 0.08) - signed_rate)
        last_e = observed_e
        if method == "body_lateral_p":
            # The wrong interpretation: ignoring that the laser is ahead of
            # the axle and controlling body lateral displacement alone.
            demand = speed * 2 * gain * (-y) / lookahead ** 2
        elif method == "exact_scan_model":
            slope = math.tan(-yaw)
            demand = speed * (slope + filtered_e / 0.12) / max(
                0.05, lookahead + filtered_e * slope)
        else:
            predicted_e = filtered_e + clip(signed_rate * 0.2, -0.008, 0.008)
            demand = speed * 2 * gain * predicted_e / (
                lookahead ** 2 + predicted_e ** 2)
        curvature = 0.9 + (2.4 - 0.9) * clip(abs(filtered_e) / 0.012, 0, 1)
        max_w = min(0.25, speed * curvature,
                    2 * speed / track * (1 - 0.45) / (1 + 0.45))
        demand = clip(demand, -max_w, max_w)
        acceleration = 0.5 if demand * command_w < 0 or abs(demand) < abs(command_w) else 0.2
        command_w += clip(demand - command_w, -acceleration * dt, acceleration * dt)
        actual_w += (1 - math.exp(-dt / 0.12)) * (command_w - actual_w)
        yaw += actual_w * dt
        y += speed * math.sin(yaw) * dt
        errors.append(e)
        turns.append(command_w)
    sign = 1 if first_e >= 0 else -1
    return {
        "method": method, "speed_mmps": speed * 1000,
        "initial_scan_error_mm": round(first_e * 1000, 3),
        "delay_ms": delay * 1000, "initial_yaw_deg": math.degrees(heading),
        "final_scan_error_mm": round(errors[-1] * 1000, 4),
        "maximum_scan_error_mm": round(max(map(abs, errors)) * 1000, 3),
        "cross_center_peak_mm": round(max(0, max(-sign * e for e in errors)) * 1000, 3),
        "final_yaw_deg": round(math.degrees(yaw), 4),
        "max_turn_degps": round(math.degrees(max(map(abs, turns))), 4),
    }


def main():
    rows = [simulate(method, v, e, delay, heading)
            for method in ("body_lateral_p", "image_preview", "exact_scan_model")
            for v in (0.005, 0.03, 0.1)
            for e in (-0.05, 0.05)
            for delay in (0.03, 0.15)
            for heading in (0, math.radians(4))]
    for row in rows:
        if row["method"] == "image_preview":
            assert abs(row["final_scan_error_mm"]) < 2.0, row
            assert row["maximum_scan_error_mm"] < 100.0, row
    summary = {}
    for method in ("body_lateral_p", "image_preview", "exact_scan_model"):
        selected = [r for r in rows if r["method"] == method]
        summary[method] = {
            "cases": len(selected),
            "worst_final_abs_scan_error_mm": max(abs(r["final_scan_error_mm"]) for r in selected),
            "worst_cross_center_peak_mm": max(r["cross_center_peak_mm"] for r in selected),
        }
    print(json.dumps({"scope": "ideal planar model; not production replay or physical validation",
                      "distance_m": 1.5, "summary": summary, "cases": rows}, indent=2))


if __name__ == "__main__":
    main()
