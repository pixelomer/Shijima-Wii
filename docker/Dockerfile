FROM devkitpro/devkitppc:20250102
WORKDIR /opt
RUN git clone https://github.com/GRRLIB/GRRLIB
WORKDIR /opt/GRRLIB
RUN make -CGRRLIB clean all
RUN make -CGRRLIB install
RUN /opt/devkitpro/portlibs/wii/bin/powerpc-eabi-cmake -B build
RUN cmake --build build --target install
WORKDIR /work
