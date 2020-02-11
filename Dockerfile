# Dockerfile to build a standard midas container based on root-cc7
# SR Feb. 2020

FROM rootproject/root-cc7

# install required packages
RUN apt-get -qq update 
RUN apt-get -y -qq install emacs

# clone midas
RUN mkdir /midas
ENV MIDASSYS /midas
ENV MIDAS_DIR /midas
WORKDIR /
RUN git clone --quiet https://bitbucket.org/tmidas/midas.git
WORKDIR /midas
RUN git submodule update --init

# build midas
RUN mkdir build
RUN cd build && rm -rf * && cmake3 .. && make && make install
WORKDIR /midas/bin

# show executables and run test program
RUN ls -l
RUN ./odbedit -c ls

