FROM debian:12.2-slim as builder
RUN apt update && apt install -y libboost-all-dev libpq-dev libpq5 wget bison gperf libsqlite3-dev libexpat1-dev git libaspell-dev cmake libpcre3-dev nettle-dev g++ libcurl4-openssl-dev libargon2-dev libssl-dev 

# Need to upgrade to a better CMAKE version, as we're super cool bleeding edge neato.
WORKDIR /opt
RUN wget -O cmake-build.sh https://github.com/Kitware/CMake/releases/download/v3.24.0-rc4/cmake-3.24.0-rc4-linux-x86_64.sh
RUN chmod +x cmake-build.sh
RUN yes | ./cmake-build.sh
RUN apt remove -y --purge cmake 
RUN ln -s /opt/cmake-3.24.0-rc4-linux-x86_64/bin/* /usr/local/bin

# Now we need to compile libpqxx for postgres support.
RUN git clone https://github.com/jtv/libpqxx.git
WORKDIR /opt/libpqxx
RUN git checkout 7.6
RUN pwd ;  cmake -DCMAKE_BUILD_TYPE=LeakCheck . && make -j2 && make install

# Moving on to building toaststunt...
WORKDIR /toaststunt
COPY src /toaststunt/src/

COPY CMakeLists.txt  /toaststunt/
COPY CMakeModules  /toaststunt/CMakeModules

WORKDIR /toaststunt/
RUN pwd ;  mkdir build && cd build && cmake ../
RUN cd /toaststunt/build && make -j2

# Make an entirely new image...
FROM debian:12.2-slim 

# Bring over only necessary packages
RUN apt update && apt install -y tini libpq-dev libpq5 libsqlite3-dev libexpat1-dev libaspell-dev libpcre3-dev nettle-dev libcurl4-openssl-dev libargon2-dev libssl-dev 

# Bring over what we compiled.
COPY --from=builder \
     /toaststunt/build \
     /toaststunt
COPY --from=builder \
     /usr/local/lib/libpqxx* \
     /usr/local/lib/

# A special restart which output on stdout is needed for docker
COPY docker_restart.sh /toaststunt/
RUN chmod +x /toaststunt/docker_restart.sh
EXPOSE 7777
WORKDIR /toaststunt/
ENTRYPOINT ./docker_restart.sh /cores/$CORE_TO_LOAD
