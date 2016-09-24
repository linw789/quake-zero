Quake Zero is a project out of curiosity about how 3D renderer was "hande made"
in early days. To fully understand what's actually going on under the hood, I
built a world renderer for Quake 1. Here is the result.

Thank you to [Casey Muratori](https://handmadehero.org/) and 
[Philip Buuck](http://philipbuuck.com/handmadequake) whose "handmade" projects 
inspired me to start this project. And James Gregory for putting together the 
ebook version of Michael Abrash's 
[Graphics Programming Black Book](https://github.com/jagregory/abrash-black-book)
which is the main resource for studying Quake 1 rendering code.


## To Build

- Create a folder with the name "assets" under the project root directory, and 
  put the PAK file of the original Quake 1 in it. 

- Set up Visual C++ environment by typing the following line into your command
  prompt. 

  "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64

  If you intalled the Visual Studio somewhere else, you need to navigate there 
  and run "vcvarsall.bat amd64". It has to be 64-bit environment otherwise the 
  unit tests will fail.

- In command prompt, go into folder "code", run "build.bat". You might get
  various warnings if you are not using Visual Studio Community 2015.

- Then run "vs.bat" to debug.

## Tests

If you run buil.bat under the "tests" folder, and run tests.exe, you can see,
after unit tests finish, a demo program illustrating how Quake's Cache Allocator
works.

## Demo 

[Youtube Video](https://youtu.be/Y_GQc0QeKPU)

![quake example 1](docs/quake_example_00.PNG)
![quake example 2](docs/quake_example_01.PNG)
![quake example 3](docs/quake_example_02.PNG)
![quake example 4](docs/quake_example_03.PNG)
