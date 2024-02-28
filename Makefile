srcs=report_usage block_hist raw_hist debug_hist #block_min_max_start critical_block generate_hist

EXES := $(foreach item,$(srcs),bin/$(item).exe)
all: bin/helper.o $(EXES)

DYNINST_ROOT=/home/wuxx1279/bin/dyninst-liveness
ifeq ($(DYNINST_ROOT),)
$(error DYNINST_ROOT is not set)
endif

TBB=/opt/spack/opt/spack/linux-centos8-zen2/gcc-10.2.0/intel-tbb-2020.3-zbj2dajg6q53hhsfd7kglxwqhyc7ie3v/include
lDyninst= -ldyninstAPI -lsymtabAPI -lparseAPI -linstructionAPI -lcommon -lboost_filesystem -lboost_system  -ldynElf
iLib= -I$(DYNINST_ROOT)/include -I$(TBB) -I ELFIO -I amdgpu-tooling -I msgpack-c -Iinclude -I/opt/rocm/include
lLib= -L$(DYNINST_ROOT)/lib -L/opt/rocm/lib/
options= -std=c++17 -g -Wall -Wextra -Wno-class-memaccess
loptions= -Wl,--demangle -Wl,-rpath,/opt/rocm/lib
links= $(lDyninst) -lamd_comgr
bin/%.exe: src/%.cpp $(CURDIR)/bin/helper.o
	g++ $(options) $(iLib) $^ $(iLib) $(lLib) $(links) $(loptions) -o $@

bin/helper.o: src/helper.cpp amdgpu-tooling/KdUtils.cpp amdgpu-tooling/KernelDescriptor.cpp
	g++ -fPIC --shared $(options) $(iLib) $^ $(iLib) $(lLib) $(links) $(loptions) -o $@
clean:
	rm -f *.bundle *.hsaco *.isa src/*.o lib/*.o bin/*.o bin/*.exe

