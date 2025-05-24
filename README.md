# THOTD-Feeder
vJoy feeder, which allows to play The House of the Dead (PC, 1996) with two mice simultaneously.

This tiny (~42Kb) application expands original multiplayer capabilities of THOTD using virtual joystick interface.

# Requirements
[+] vJoy needs to be installed. (https://sourceforge.net/projects/vjoystick/)\n
[+] 2 Virtual devices (#0 & #1) must be created.
[+] Game must be configured to use Joystick1 and Joystick2 as HID.
[+] Two mice and running instance of feeder.

# Configuration
Run "feeder.exe set MOUSE_1_DPI MOUSE_2_DPI" to create configuration with chosen mice DPI.
Example: feeder.exe set 32 64

Run "feeder.exe debug" to see raw mouse delta-coordinates.

# My cursor is too slow!
This is where things may become hard. In my opinion, original game has poor joystick support. But this can be fixed. Everything can be.
In order to solve joystick sensivitity problem original game executable has to be patched. What did I do?
1) Disassembled original executable.
2) Found functions, which change cursor position.
3) Found problem: reported joystick axis value is divided by 65535 (0xFFFF) resulting in a small number, which is used further to move cursor. Changing this value solved problem.
4) I hooked original function and fixed it (only for joysticks). If you have same THOTD.EXE as me (SHA1:139629FA0EAB369995FFF3A32FF72162F9BE2D43), you can just patch your executable as I did.
Using HEX-Editor replace following bytes (FILE OFFSET | DATA BEFORE -> DATA AFTER):
0x01AC67 | 61 FA FF FF -> 25 C6 16 00
0x01AC84 | 44 FA FF FF -> 08 C6 16 00
0x187290 | (ZEROS)     -> BB FF 20 00 00 29 D3 75 03 31 C0 C3 29 D0 89 C2 C1 E0 02 29 D0 C1 E0 03 01 C2 C1 E2 03 89 D0 C1 FA 1F F7 FB 2D 08 03 00 00 C3
6) Pseudocode of original function: return (REPORTED_VALUE * 0xC8) / (MAX_VALUE - MIN_VALUE) - [(0x7FFF * 0xC8 / (MAX_VALUE - MIN_VALUE) + 1)] <- Precalculated
My patch inserts changed MAX_VALUE at function start.

Game of my childhood... Feel free to ask any questions. :)
Shurafen, 2025.
