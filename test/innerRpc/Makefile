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
include ../../co/co.mk

########## options ##########
CFLAGS += -g -fno-strict-aliasing -O2 -Wall -export-dynamic \
	-Wall -pipe  -D_GNU_SOURCE -D_REENTRANT -fPIC -Wno-deprecated -m64

UNAME := $(shell uname -s)

INCLS += -I../proto -I../../co -I../../corpc

ifeq ($(UNAME), FreeBSD)
LINKS += -g -L../../co/lib -L../../corpc/lib -L/usr/local/lib -lcorpclib -lcolib -lprotobuf -lpthread
else
LINKS += -g -L../../co/lib -L../../corpc/lib -L/usr/local/lib -lcorpclib -lcolib -lprotobuf -lpthread -ldl
endif

INNER_RPC_OBJS=../proto/foo.pb.o ../proto/bar.pb.o ../proto/baz.pb.o main.o

PROGS = innerRpc

all:$(PROGS)

innerRpc: $(INNER_RPC_OBJS)
	$(BUILDEXE)

clean:
	$(CLEAN) *.o $(PROGS)
