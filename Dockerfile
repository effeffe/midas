FROM rootproject/root-cc7
RUN mkdir /midas
ENV MIDASSYS /midas
ENV MIDAS_DIR /midas
WORKDIR /midas
COPY . /midas
RUN git clone --quiet https://bitbucket.org/tmidas/mxml /mxml
RUN git clone --quiet https://bitbucket.org/tmidas/mscb /mscb
RUN cd /midas/build && rm -rf * && cmake3 .. && make && make install
WORKDIR /midas/bin
RUN ls -l
RUN ./odbedit -c ls

