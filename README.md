Datagrind is a valgrind tool that captures all read and write accesses made by
a program and records them in a log file. A separate tool (`dg_view`), can then
be used to visually represent the reads and writes.

To get started, run

    git clone https://github.com/bmerry/datagrind.git
    cd datagrind
    git submodule update --init
    ./autogen.sh
    ./configure
    make
    sudo make install
    cd .
    git clone https://github.com/bmerry/dg_view.git
    cd dg_view
    scons

To see it in action, run

    valgrind --tool=exp-datagrind --datagrind-out=ls.out ls
    build/dg_view ls.out

For more detailed usage instructions, either compile the included
documentation, or refer to the [online
version](http://www.brucemerry.org.za/files/datagrind-docs/dg-manual.html).
