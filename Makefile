#srcs=expand_args update_note_phdr report_args_loc disassemble bd_base split_kernel merge_kernel bd_inplace bd_inplace_global update_text_phdr update_dynamic debug update_kd pure_bd
srcs=report_usage

BINS=$(addprefix bin/,$(srcs))

all : $(BINS)

DYNINST_ROOT=/home/wuxx1279/bin/dyninst-main
ifeq ($(DYNINST_ROOT),)
$(error DYNINST_ROOT is not set)
endif

TBB=/opt/spack/opt/spack/linux-centos8-zen2/gcc-10.2.0/intel-tbb-2020.3-zbj2dajg6q53hhsfd7kglxwqhyc7ie3v/include
lDyninst= -ldyninstAPI -lsymtabAPI -lparseAPI -linstructionAPI -lcommon -lboost_filesystem -lboost_system  -ldynElf

bin/report_usage: src/report_usage.cpp src/symbolDemangle.c amdgpu-tooling/KernelDescriptor.cpp amdgpu-tooling/KdUtils.cpp
	g++ -std=c++17 -g -Wall -Wextra -Wno-class-memaccess -I$(DYNINST_ROOT)/include -I$(TBB) -I ELFIO -I amdgpu-tooling -I msgpack-c $< amdgpu-tooling/KdUtils.cpp amdgpu-tooling/KernelDescriptor.cpp -L$(DYNINST_ROOT)/lib -Iinclude -I$(TBB) $(lDyninst) -I /opt/rocm/include -Wl,--demangle -Wl,-rpath,/opt/rocm/lib/ -L/opt/rocm/lib/-lamd_comgr -o $@

clean:
	rm -f *.bundle *.hsaco *.isa src/*.o lib/*.o bin/*

