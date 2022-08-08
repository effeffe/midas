# Dockerfile to build a standard midas container based on root-cc7
# SR Feb. 2020
#
# Usage:
#   $ docker pull drstefanritt/midas
#   $ docker run -it drstefanritt/midas /bin/bash
#

FROM rootproject/root-cc7

# install required packages: root-cc7 requires yum
RUN yum -y -qq update 
RUN yum -y -qq install emacs curl-devel

# clone midas
RUN mkdir /midas
ENV MIDASSYS /midas
ENV MIDAS_DIR /midas
WORKDIR /
RUN git clone --recursive --quiet https://bitbucket.org/tmidas/midas.git
WORKDIR /midas

# build midas
RUN mkdir build
RUN cd build && rm -rf * && cmake3 .. && make && make install
RUN pip3 install -e $MIDASSYS/python --user
WORKDIR /midas/bin

# show executables and run test program
RUN ls -l
RUN ./odbedit -c ls

