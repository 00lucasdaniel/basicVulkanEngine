#!/bin/bash

g++ -g -std=c++20 main.cpp -o app -lSDL3 -lvulkan -lvolk -ltinyobjloader -lktx -lslang-compiler -lshaderc -lSPIRV
./app
