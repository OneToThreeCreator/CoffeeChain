# CoffeeChain
2D crossplatform game engine. ~WIP~  
Superceeded by <a href=https://github.com/OneToThreeCreator/Conservative_Creators_Engine>Conservative creator's engine</a>, no longer being developed
# Building on unix-like system:
Required packages: glfw, cmake.
```
# Change directory to project's directory
cd [Project dir]
# Build
cmake -B ./build && cmake --build ./build
```
Install:
```
cmake --install ./build
```
Run test:
```
ctest --test-dir ./build
```
