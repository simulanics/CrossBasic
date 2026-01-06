
::–– Build XPoints.dll ––::
windres XPoints.rc -O coff -o XPoints.res
g++ -s -std=c++17 -shared -m64 -O2 -static-libgcc -static-libstdc++ -o XPoints.dll XPoints.cpp XPoints.res -lgdiplus -lgdi32 -luser32

::–– Build XGraphics.dll ––::
windres XGraphics.rc -O coff -o XGraphics.res
g++ -s -std=c++17 -shared -m64 -O2 -static-libgcc -static-libstdc++ -o XGraphics.dll XGraphics.cpp XGraphics.res -lgdiplus -lgdi32 -luser32

::–– Build XPicture.dll ––::
windres XPicture.rc -O coff -o XPicture.res
g++ -s -std=c++17 -shared -m64 -O2 -static -static-libgcc -static-libstdc++ -o XPicture.dll XPicture.cpp XPicture.res -Wl,--unresolved-symbols=ignore-all -lgdiplus -lgdi32 -luser32

::–– Build XCanvas.dll ––::
windres XCanvas.rc -O coff -o XCanvas.res
g++ -s -std=c++17 -shared -m64 -O2 -static -static-libgcc -static-libstdc++ -o XCanvas.dll XCanvas.cpp XCanvas.res -Wl,--unresolved-symbols=ignore-all -lcomctl32 -lgdiplus -lgdi32 -luser32
