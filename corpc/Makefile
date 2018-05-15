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

v=debug

DIR_SRC = ./ ../proto/
DIR_INC = ./ ../proto/ ../co/
DIR_OBJ = ./${v}/obj/
DIR_STATICLIB = ./lib/
DIR_DYNAMICLIB = ./solib/

OBJS = corpc_option.pb.o corpc_thirdparty.pb.o corpc_utils.o corpc_routine_env.o corpc_io.o corpc_rpc_server.o corpc_rpc_client.o corpc_inner_rpc.o corpc_message_server.o
OBJS_WITH_PATH = ${foreach n,${OBJS},${DIR_OBJ}${n}}

empty =  
space =${empty} ${empty}  
VPATH := ${subst ${space},:,${DIR_SRC}}:${DIR_STATICLIB}:${DIR_DYNAMICLIB}  
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

CPPCOMPILE = ${CXX} ${CFLAGS} $< -o ${DIR_OBJ}$@

ARSTATICLIB = ar -rc $(DIR_STATICLIB)$@ ${foreach n,${filter %.o, $^},${DIR_OBJ}${notdir ${n}}}

BUILDSHARELIB = ${CXX} -fPIC -shared -O2 -pipe -L../co/solib/ -o $(DIR_DYNAMICLIB)$@ ${foreach n,${filter %.o, $^},${DIR_OBJ}${notdir ${n}}}

STATICLIB = libcorpc.a
DYNAMICLIB = libcorpc.so

%.o: %.cpp
	${CPPCOMPILE}
%.o: %.cc
	${CPPCOMPILE}

%.d:%.cpp
	${CXX} $(CFLAGS) -MM -MT "${subst .cpp,.o,${notdir $<}} ${subst .cpp,.d,${notdir $<}}" -MF "${subst .cpp,.d,${DIR_OBJ}${notdir $<}}" $< 
%.d:%.cc
	${CXX} $(CFLAGS) -MM -MT "${subst .cc,.o,${notdir $<}} ${subst .cc,.d,${notdir $<}}" -MF "${subst .cc,.d,${DIR_OBJ}${notdir $<}}" $< 
  
.PHONY: all
all: directories ${STATICLIB} ${DYNAMICLIB}

libcorpc.a: ${OBJS:.o=.d} ${OBJS}
	${ARSTATICLIB}

libcorpc.so: ${OBJS:.o=.d} ${OBJS}
	${BUILDSHARELIB}

.PHONY: directories 
directories:
	mkdir -p ${DIR_OBJ}
	mkdir -p ${DIR_STATICLIB}
	mkdir -p ${DIR_DYNAMICLIB}

ifneq ($(MAKECMDGOALS),clean)
-include ${OBJS_WITH_PATH:.o=.d}
endif

.PHONY: clean
clean:
	rm -rf ${DIR_OBJ}
	rm -rf ${DIR_STATICLIB}
	rm -rf ${DIR_DYNAMICLIB}
