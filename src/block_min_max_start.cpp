#include <msgpack.hpp>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <elfio/elfio.hpp>
#include "CodeObject.h"
#include "InstructionDecoder.h"
#include <vector>
#include  <map>
#include <algorithm>
#include "liveness.h"
#include "KdUtils.h"
#include "KernelDescriptor.h"
#include "helper.hpp"

using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::SymtabAPI;
char * filename;
void analyzeLiveness(ParseAPI::Function * f, InstructionDecoder & decoder, std::map<std::string, per_kernel_data> & kernel_data)
{
    //printf("callling analyze liveness on function %s\n",f->name().c_str());
    LivenessAnalyzer la(f->isrc()->getArch(),f->obj()->cs()->getAddressWidth());
    uint32_t sgpr_count ,vgpr_count ,agpr_count;
    auto blocks = f->blocks();
    auto bit = blocks.begin();
    vector<bitArray> liveRegs;
    vector<Address> addrs;
    std::string kname = f->name();
    const char * ckname = kname.c_str();
    
    //printf("%s,%s,%d,%d",filename,ckname,kernel_data[kname].note_sgpr_count,kernel_data[kname].note_vgpr_count);
    uint32_t s_alloc = std::min(kernel_data[kname].kd_sgpr_count,102u);
    uint32_t v_alloc = std::min(kernel_data[kname].kd_vgpr_count,256u);
    printf("%u %u\n",s_alloc,v_alloc);
    for(; bit != blocks.end(); bit++){
        Block * bb = * bit;
        Location loc(f,bb);
        uint32_t s_block_min = 102 , s_block_start , s_block_max = 0, s_block_last, s_block_post;
        uint32_t v_block_min = 102, v_block_start , v_block_max = 0 , v_block_last , v_block_post;
        Instruction instr = decoder.decode(
            (unsigned char * ) f->isrc()->getPtrToInstruction(bb->lastInsnAddr()));    
        //cout << "BB Last Instr @" << std::hex << bb->lastInsnAddr()  << " : " << instr.format() << endl;
        auto category = instr.getCategory();
        bitArray blockOutLiveRegs;
        if(la.queryBlock(loc,LivenessAnalyzer::Before, addrs, liveRegs, blockOutLiveRegs)){
            assert(addrs.size() == liveRegs.size());
            for ( int r_i = addrs.size() - 1 ; r_i >=0 ; r_i --){
                parseBitArray(liveRegs[r_i], sgpr_count, vgpr_count, agpr_count);     
                //printf(",0x%lx,%d,%d",(unsigned long) addrs[r_i], sgpr_count, vgpr_count);
                if(sgpr_count < s_block_min)
                    s_block_min = sgpr_count;
                if(vgpr_count < v_block_min)
                    v_block_min = vgpr_count;
            }

            parseBitArray(liveRegs[addrs.size()-1],s_block_start,v_block_start,agpr_count);
            parseBitArray(liveRegs[0],s_block_last,v_block_last,agpr_count);
            parseBitArray(blockOutLiveRegs,s_block_post,v_block_post,agpr_count);

            if (category == c_BranchInsn || category == c_GPUKernelExitInsn){
                //cout << " Is BRNAHC !! " << endl;
                printf("%u %u %u %u %u %u\n",s_alloc - s_block_min, s_alloc - s_block_start , s_alloc - s_block_last, v_alloc - v_block_min, v_alloc - v_block_start, v_alloc - v_block_last);
            }else{

                printf("%u %u %u %u %u %u\n",s_alloc - s_block_min, s_alloc - s_block_start , s_alloc - s_block_post, v_alloc - v_block_min, v_alloc - v_block_start, v_alloc - v_block_post);
            }
     
            //printf("%u %u %u %u %u %u\n",s_alloc - s_block_min, s_alloc - s_block_max, s_alloc - s_block_start, v_alloc - v_block_min, v_alloc - v_block_max , v_alloc - v_block_start);
        }

        liveRegs.clear();
        addrs.clear();
    }

} 

void parseLiveness( char * binary_path, std::map<std::string, per_kernel_data> & kernel_data){
    SymtabCodeSource * scs = new SymtabCodeSource(binary_path);

    CodeObject * dyn_co = new CodeObject(scs);

    dyn_co->parse();
    auto arch = scs -> getArch();
    InstructionDecoder decoder("",InstructionDecoder::maxInstructionLength,arch);
    const CodeObject::funclist & all = dyn_co->funcs();
    auto fit = all.begin();
    for( ; fit != all.end(); fit++){
        ParseAPI::Function * f = * fit;
        analyzeLiveness(f, decoder, kernel_data);
    }

}
int main(int argc, char * argv[]){
    if(argc < 2){
        printf("usage list_args <name of .note file>\n");
        return -1;
    }
    ELFIO::elfio codeObject;
    if(!codeObject.load(argv[1])){
        assert(0 && "failed to load code object using elfio ");
    }
    filename = argv[1];
    std::map<std::string, std::string> prettyNameMap;
    std::map<std::string, std::string> uglyNameMap;
    std::map<std::string, per_kernel_data> kernel_data;
    parseKD(argv[1],kernel_data);
    parseLiveness(argv[1],kernel_data);

    return 0;    
}

