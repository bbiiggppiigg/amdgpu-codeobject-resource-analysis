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
#include "helper.hpp"

using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::SymtabAPI;

using std::string, std::cout , std::endl;

char * filename;
uint64_t min_addr = 0xffffffffffff;
uint64_t min_sgpr = 0xffffffffffff;
uint64_t min_vaddr = 0xffffffffffff;
uint64_t min_vgpr = 0xffffffffffff;
bool hasNoPair = false;
void parseCritical(ParseAPI::Function * f, InstructionDecoder & decoder, int N, std::map<std::string, per_kernel_data> kernel_data)
{
    //printf("callling analyze liveness on function %s\n",f->name().c_str());
    LivenessAnalyzer la(f->isrc()->getArch(),f->obj()->cs()->getAddressWidth());
    uint32_t sgpr_count ,vgpr_count ,agpr_count;
    auto blocks = f->blocks();
    auto bit = blocks.begin();
    vector<bitArray> liveRegs;
    vector<bitArray> useRegs;
    vector<bitArray> defRegs;
    vector<Address> addrs;
    std::string kname = f->name();
    const char * ckname = kname.c_str();
    
    //printf("%s,%s,%d,%d",filename,ckname,kernel_data[kname].note_sgpr_count,kernel_data[kname].note_vgpr_count);
    uint32_t s_alloc = std::min(kernel_data[kname].kd_sgpr_count,102u);
    uint32_t v_alloc = std::min(kernel_data[kname].kd_vgpr_count,256u);
    //printf("%u %u\n",s_alloc,v_alloc);
    for(; bit != blocks.end(); bit++){
        Block * bb = * bit;
        Location loc(f,bb);
        //uint32_t s_block_min = 102 , s_block_start , s_block_max = 0, s_block_last, s_block_post;
        //uint32_t v_block_min = 102, v_block_start , v_block_max = 0 , v_block_last , v_block_post;
        Instruction instr = decoder.decode(
            (unsigned char * ) f->isrc()->getPtrToInstruction(bb->lastInsnAddr()));    
        //cout << "BB Last Instr @" << std::hex << bb->lastInsnAddr()  << " : " << instr.format() << endl;
        
        auto category = instr.getCategory();
        bitArray blockOutLiveRegs;
        bool hasPair = false;
        if(la.queryBlock(loc,LivenessAnalyzer::Before, addrs, liveRegs, blockOutLiveRegs,defRegs,useRegs)){
            assert(addrs.size() == liveRegs.size());
            int r_i = addrs.size() - 1;
            int start_i , end_i;;
            bool inCritical = false;
            bitArray unSpillable = bitArray(s_alloc);
            while ( r_i >= 0 ) 
            {
                bitArray realDead = liveRegs[r_i] | defRegs[r_i];
                realDead.resize(s_alloc);
                string buffer;
                to_string(realDead, buffer);
                for (uint32_t i =0 ; i < buffer.size(); i+=2){
                    if((!buffer[i]) && !(buffer[i+1]))
                    hasPair = true;
                }
                sgpr_count = s_alloc - realDead.count();
                bitArray vgpr_array = liveRegs[r_i];
                vgpr_array = vgpr_array >> 102;
                vgpr_array.resize(256);
                vgpr_count = v_alloc - vgpr_array.count();
                if(sgpr_count < min_sgpr){
                    min_sgpr = sgpr_count;
                    min_addr = addrs[r_i];
                }
                if(vgpr_count < min_vgpr){
                    min_vgpr = vgpr_count;
                    min_vaddr = addrs[r_i];
                }

                printf("parsing addr =%lx, s_alloc = %u, sgpr_count = %u\n",addrs[r_i],s_alloc,sgpr_count);
                if(r_i == addrs.size() - 1 )
                    cout <<  realDead << endl;
                //parseBitArray(realDead, sgpr_count, vgpr_count, agpr_count);     
                if( inCritical) {
                    if ( sgpr_count >= N) {
                        inCritical = false;
                        end_i = r_i;
                        printf("Critical Block from %lx to %lx, count = %u\n",addrs[start_i],addrs[end_i],unSpillable.count());
                    }else{
                        unSpillable |= defRegs[r_i];
                        unSpillable |= useRegs[r_i];
                    }
                }
                else
                {
                    if ( sgpr_count < N ) {
                        inCritical = true;
                        start_i = r_i;
                        unSpillable |= defRegs[r_i];
                        unSpillable |= useRegs[r_i];
                    }
                }
                r_i--;
            }
            if ( inCritical )
            {
                printf("Ends ins Critical Block !!\n");
            }
        }
        if(!hasPair)
            hasNoPair = true;
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
        parseCritical(f, decoder, 3, kernel_data);
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
    printf("min addr = %llx , sgpr_count = %llu\n",min_addr,min_sgpr);
    printf("min addr = %llx , vgpr_count = %llu\n",min_vaddr,min_vgpr);
    cout << "has no pair = " << hasNoPair << endl;
    /*for ( auto & kit : kernel_data){
        cout <<  kit.second.note_sgpr_count << "," << kit.second.kd_sgpr_count << ",";
        cout <<  kit.second.note_vgpr_count << "," << kit.second.kd_vgpr_count << endl;
  }*/

    return 0;    
}

