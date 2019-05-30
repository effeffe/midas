FROM rootproject/root-cc7
RUN mkdir /midas
ENV MIDASSYS /midas
WORKDIR /midas
COPY . /midas
RUN git clone --quiet https://bitbucket.org/tmidas/mxml /mxml && \
git clone --quiet https://bitbucket.org/tmidas/mscb /mscb && \
cd /midas && make
WORKDIR /midas/bin
RUN ./odbedit -c
#RUN make install
RUN ls -l
RUN echo ${MIDASSYS}
