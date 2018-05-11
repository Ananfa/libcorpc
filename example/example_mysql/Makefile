#
# Created by Xianke Liu on 2018/5/10.
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

v=debug

DIR_SRC = ./ ../proto/
DIR_LIB = ../../co/lib/ ../../corpc/lib/ ../../corpc_mysql/lib/ /usr/local/lib/ /usr/lib64/mysql/
DIR_INC = ./ ../../proto/ ../../co/ ../../corpc/ ../../corpc_mysql/ /usr/include/mysql/
DIR_OBJ = ./${v}/obj/
DIR_BIN = ./${v}/bin/

OBJS = main.o
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
CFLAGS += -g -DDebug -fno-inline
endif

ifneq ($v,release)
BFLAGS = -g
endif

UNAME := ${shell uname -s}

ifeq (${UNAME}, FreeBSD)
LINKS = -g ${foreach n,${DIR_LIB},-L${n}} -lcorpc_mysql -lcorpc -lco -lprotobuf -lmysqlclient -lpthread
else
LINKS = -g ${foreach n,${DIR_LIB},-L${n}} -lcorpc_mysql -lcorpc -lco -lprotobuf -lmysqlclient -lpthread -ldl
endif

CPPCOMPILE = ${CXX} ${CFLAGS} $< -o ${DIR_OBJ}$@
BUILDEXE = ${CXX} ${BFLAGS} -o ${DIR_BIN}$@ ${foreach n,${filter %.o, $^},${DIR_OBJ}${notdir ${n}}} ${LINKS} 

PROGS = example_mysql

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

example_mysql: ${OBJS:.o=.d} ${OBJS}
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
