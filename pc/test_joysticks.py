"""
Quick diagnostic script to test joystick enumeration.
Run this separately to verify pedals are detected before running rc_sender.py
"""
import os
import time
# CRITICAL: Set SDL backend BEFORE importing pygame
os.environ["SDL_JOYSTICK_RAWINPUT"] = "1"
os.environ["SDL_JOYSTICK_HIDAPI"] = "1"
# If pedals still don't show, try: os.environ["SDL_JOYSTICK_HIDAPI"] = "0"

import pygame

pygame.init()
pygame.joystick.init()

print("=" * 60)
print("Joystick Detection Test")
print("=" * 60)
print(f"pygame version: {pygame.version.ver}")
print(f"SDL version: {pygame.get_sdl_version()}")
print(f"SDL_JOYSTICK_RAWINPUT: {os.environ.get('SDL_JOYSTICK_RAWINPUT', 'not set')}")
print(f"SDL_JOYSTICK_HIDAPI: {os.environ.get('SDL_JOYSTICK_HIDAPI', 'not set')}")
print("=" * 60)

print(f"\nDetected joysticks: {pygame.joystick.get_count()}")
for i in range(pygame.joystick.get_count()):
    js = pygame.joystick.Joystick(i)
    js.init()
    print(f"\n[{i}] {js.get_name()}")
    print(f"    axes={js.get_numaxes()} buttons={js.get_numbuttons()}")
    print(f"    guid={js.get_guid()}")

if pygame.joystick.get_count() == 0:
    print("\n⚠️  No joysticks detected!")
    print("   - Check joy.cpl to verify Windows sees your devices")
    print("   - Try setting SDL_JOYSTICK_HIDAPI = '0' instead")
    print("   - Restart this script after changing environment variables")
else:
    print("\n✅ Devices detected! Move pedals now. Press Ctrl+C to exit.")
    print("   Watch for axis values changing as you move controls...\n")
    
    try:
        while True:
            pygame.event.pump()
            for i in range(pygame.joystick.get_count()):
                js = pygame.joystick.Joystick(i)
                axis_vals = []
                for axis_idx in range(js.get_numaxes()):
                    try:
                        val = js.get_axis(axis_idx)
                        if abs(val) > 0.01:  # Only show non-zero values
                            axis_vals.append(f"A{axis_idx}={val:.3f}")
                    except:
                        pass
                if axis_vals:
                    print(f"[{i}] {js.get_name()}: {', '.join(axis_vals)}", end='\r')
            time.sleep(0.05)
    except KeyboardInterrupt:
        print("\n\nExiting...")
