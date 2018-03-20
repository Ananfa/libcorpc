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
include ../co/co.mk

########## options ##########
CFLAGS += -fno-strict-aliasing -Wall -export-dynamic \
	-Wall -pipe  -D_GNU_SOURCE -D_REENTRANT -fPIC -Wno-deprecated -m64

UNAME := $(shell uname -s)

INCLS += -I../co

ifeq ($(UNAME), FreeBSD)
LINKS += -g -L../co/lib -L/usr/local/lib -lcolib -lprotobuf -lpthread
else
LINKS += -g -L../co/lib -L/usr/local/lib -lcolib -lprotobuf -lpthread -ldl
endif

CORPCLIB_OBJS=corpc_option.pb.o corpc_utils.o corpc_routine_env.o corpc_io.o corpc_server.o corpc_client.o corpc_inner.o

PROGS = corpclib

all:$(PROGS)

corpclib:libcorpclib.a libcorpclib.so

libcorpclib.a: $(CORPCLIB_OBJS)
	$(ARSTATICLIB) 
libcorpclib.so: $(CORPCLIB_OBJS)
	$(BUILDSHARELIB) 

dist: clean libcorpc-$(version).src.tar.gz

libcorpc-$(version).src.tar.gz:
	@find . -type f | grep -v CVS | grep -v .svn | sed s:^./:libcorpc-$(version)/: > MANIFEST
	@(cd ..; ln -s libcorpc_pub libcorpc-$(version))
	(cd ..; tar cvf - `cat libcorpc_pub/MANIFEST` | gzip > libcorpc_pub/libco-$(version).src.tar.gz)
	@(cd ..; rm libcorpc-$(version))

clean:
	$(CLEAN) *.o $(PROGS)
	rm -fr MANIFEST lib solib libcorpc-$(version).src.tar.gz libcorpc-$(version)

