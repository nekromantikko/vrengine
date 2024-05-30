# VR game engine for Oculus Quest using OpenXR and Vulkan (WIP)

A lot of the basic rendering boilerplate has been brought over from my other rendering projects. I mostly use almost pure C these days but for efficiency I decided to actually use stl this time for dynamic arrays and messing with strings.

Maya export notes:
- Z-axis is inverted (So Z-rotations and X-translation get reversed)
- Maya2glTF export flags: -mts -i32 -bsf
- Scale factor 0.01
