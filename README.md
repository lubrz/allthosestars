# All Those Stars

Constellations Night-Light

---

## Project Overview

This project is an interactive night-light constellation, designed for educational and decorative use.

It features:

- 7 LEDs arranged as stars (pins D2, D3, D4, D5, D6, D9, D10)
  - PWM pins: D3, D5, D6, D9, D10
- Light-dependent resistor (LDR) for ambient light detection (A0)
- Arduino Nano Every (ATmega4809, megaAVR)
- Powered by 4×AA batteries (6 V)

### Features
- Light-activated constellation (on at night, off during day)
- Visual calibration with cascade LED animation
- Breathing and twinkle animation for star LEDs
- Hidden Morse code message on a configurable star
- Power-saving sleep modes (standby / idle)

## Circuit Diagram

![All Those Stars](/images/the_circuit.png)

## Usage

1. Upload the code to your Arduino Nano Every.
2. Assemble the circuit as shown in the diagram above.
3. Power the device with 4×AA batteries (6 V) or 1x9V battery.
4. The constellation will automatically turn on at night and off during the day.
5. One star will blink a hidden Morse code message (configurable in the code).

## Customization
- Change the hidden Morse message or which star blinks it by editing the following lines in the code:
  ```cpp
  const int MORSE_STAR_INDEX = 0; // 0 = first star, 6 = last
  const char MORSE_MESSAGE[] = "HELLO WORLD"; // Change this to your desired message (A-Z, 0-9, space)
  const unsigned long MORSE_DOT_MS = 400; // dot duration (ms)
  ```
- Adjust brightness, animation, and power-saving settings as needed.

## Disclaimer

> **Please read the following carefully before attempting to replicate any of the projects documented on this website, the YouTube channel, or any associated video or social media content.**
>
> **For demonstrative and educational purposes only.** All projects, builds, tutorials, and guides published on allthosestars.com and across all associated channels (YouTube, Instagram, TikTok, GitHub) are shared strictly for personal inspiration and educational reference. They do not constitute professional electrical, engineering, or technical instructions.
>
> **No responsibility for damage to components or equipment.** Working with electronics carries inherent risks. Components can be damaged if wired incorrectly, if incorrect voltages are applied, or if instructions are followed imprecisely. The author accepts no responsibility whatsoever for any components, devices, or equipment damaged by anyone attempting to replicate these projects.
>
> **No responsibility for personal injury.** These projects involve tools including drills, hammers, leather punches, and soldering irons, as well as electrical components. The author accepts no responsibility for any personal injury — including but not limited to cuts, burns, electric shock, or any other harm — sustained by anyone attempting to replicate any part of the projects shown here or in any associated content.
>
> **Proceed entirely at your own risk.** By choosing to replicate any project shown here, you accept full and sole responsibility for your own safety and for any outcomes — including damage to property or injury to yourself or others. The author cannot be held liable under any circumstances.
>
> **Seek qualified help when in doubt.** If you are unfamiliar with basic electronics, woodworking tools, or electrical safety, please consult a qualified professional before proceeding. Do not attempt to replicate these projects unsupervised if you are a minor.
>
> This disclaimer applies to all content published on allthosestars.com, the All Those Stars YouTube channel, Instagram, TikTok, GitHub, and any other platform or medium where this content may appear. Last updated 2026.
