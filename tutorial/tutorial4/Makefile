#
# Created by Xianke Liu on 2018/3/14.
#
# Licensed under the Apache License, Version 2.0 (the "License"); 
# you may not use this file except in compliance with the License. 
# You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, 
# software distributed under the License is distributed on an "AS IS" BASIS, 
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
# See the License for the specific language governing permissions and 
# limitations under the License.
#


COMM_MAKE = 1
COMM_ECHO = 1
version=0.1
v=debug

DIR_SRC = ./ ../proto/
DIR_LIB = ../../co/lib/ ../../corpc/lib/ /usr/local/lib/
DIR_INC = ./ ../proto/ ../../co/ ../../corpc/
DIR_OBJ = ./${v}/obj/
DIR_BIN = ./${v}/bin/

PROTO_OBJS = helloworld.pb.o
OBJS = ${PROTO_OBJS} main.o
OBJS_WITH_PATH = ${foreach n,${OBJS},${DIR_OBJ}${n}}

empty =  
space =${empty} ${empty}  
VPATH := ${subst ${space},:,${DIR_SRC}}:${DIR_BIN}  
vpath %.o ${DIR_OBJ}  
vpath %.d ${DIR_OBJ}  

INCLS = ${foreach n,${DIR_INC},-I${n}}

CFLAGS = ${INCLS} -std=gnu++11 -fPIC -DLINUX -pipe -c \
    -fno-strict-aliasing -Wall -export-dynamic \
    -D_GNU_SOURCE -D_REENTRANT -Wno-deprecated -m64

ifeq ($v,release)
CFLAGS += -O2
else
CFLAGS += -g -Ddebug -fno-inline
endif

ifneq ($v,release)
BFLAGS = -g
endif

UNAME := ${shell uname -s}

ifeq (${UNAME}, FreeBSD)
LINKS = -g ${foreach n,${DIR_LIB},-L${n}} -lcorpclib -lcolib -lprotobuf -lpthread
else
LINKS = -g ${foreach n,${DIR_LIB},-L${n}} -lcorpclib -lcolib -lprotobuf -lpthread -ldl
endif

CPPCOMPILE = ${CXX} ${CFLAGS} $< -o ${DIR_OBJ}$@
BUILDEXE = ${CXX} ${BFLAGS} -o ${DIR_BIN}$@ ${foreach n,${filter %.o, $^},${DIR_OBJ}${n}} ${LINKS} 

BUILDDEP = ${CXX} $(CFLAGS) -MM -MT "$(subst .c,.o,${notdir $<}) $(subst .c,.d,${notdir $<})" -MF "$(subst .c,.d,${DIR_OBJ}${notdir $<})" $< 

PROGS = tutorial4

%.o: %.cpp
	${CPPCOMPILE}
%.o: %.cc
	${CPPCOMPILE}

%.d:%.cpp
	${CXX} $(CFLAGS) -MM -MT "${subst .cpp,.o,${notdir $<}} ${subst .cpp,.d,${notdir $<}}" -MF "${subst .cpp,.d,${DIR_OBJ}${notdir $<}}" $< 
%.d:%.cc
	${CXX} $(CFLAGS) -MM -MT "${subst .cc,.o,${notdir $<}} ${subst .cc,.d,${notdir $<}}" -MF "${subst .cc,.d,${DIR_OBJ}${notdir $<}}" $< 
  
.PHONY: all
all: directories ${PROGS}

tutorial4: ${OBJS:.o=.d} ${OBJS}
	${BUILDEXE}

.PHONY: directories 
directories:
	mkdir -p ${DIR_OBJ}
	mkdir -p ${DIR_BIN}

ifneq ($(MAKECMDGOALS),clean)
-include ${OBJS_WITH_PATH:.o=.d}
endif

.PHONY: clean
clean:
	rm -rf ${DIR_OBJ}
	rm -rf ${DIR_BIN}
