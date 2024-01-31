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
#include "liveness.h"
#include "KdUtils.h"
#include "KernelDescriptor.h"
#include "helper.hpp"

using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::SymtabAPI;

using std::string, std::cout , std::endl;
char * filename;
void analyzeLiveness(ParseAPI::Function * f, std::map<std::string, per_kernel_data> & kernel_data){
    //printf("callling analyze liveness on function %s\n",f->name().c_str());
    LivenessAnalyzer la(f->isrc()->getArch(),f->obj()->cs()->getAddressWidth());
    uint32_t sgpr_count ,vgpr_count ,agpr_count;
    auto blocks = f->blocks();
    auto bit = blocks.begin();
    vector<bitArray> liveRegs;
    vector<Address> addrs;
    std::string kname = f->name();
    const char * ckname = kname.c_str();
    bitArray blockOutLiveRegs;
    for(; bit != blocks.end(); bit++){
        Block * bb = * bit;
        Address curAddr = bb->start();
        Dyninst::Address endAddr = bb->end();
        printf("%s,%s,%d,%d\n",filename,ckname,kernel_data[kname].note_sgpr_count,kernel_data[kname].note_vgpr_count);
        
        Location loc(f,bb);
        if(la.queryBlock(loc,LivenessAnalyzer::Before, addrs, liveRegs,blockOutLiveRegs)){
            assert(addrs.size() == liveRegs.size());
            for ( int r_i = addrs.size() - 1 ; r_i >=0 ; r_i --){
                std::cout << std::hex << addrs[r_i] << "," << liveRegs[r_i] << std::endl;
                //parseBitArray(liveRegs[r_i], sgpr_count, vgpr_count, agpr_count);     
                //printf(",0x%lx,%d,%d",(unsigned long) addrs[r_i], sgpr_count, vgpr_count);
            }
        }
        //puts("");
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
        analyzeLiveness(f,kernel_data);
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
    ELFIO::section * noteSection;
    if( !(noteSection = getSection(".note", codeObject)) ){
        assert(0 && "failed to load .note section "); 
    }
    filename = argv[1];
    std::map<std::string, std::string> prettyNameMap;
    std::map<std::string, std::string> uglyNameMap;
    std::map<std::string, per_kernel_data> kernel_data;
    parse_note(noteSection,kernel_data,prettyNameMap);
    parseLiveness(argv[1],kernel_data);

    /*parse_note(noteSection);
      parseInstructions(argv[1]);
      parseKD(argv[1]);

      bool first_print = true;
      for ( auto & kit : kernel_data){
      if(first_print)
      cout << argv[1] << ",";
      else
      cout << ",";
      cout << kit.first << ",";
      cout << kit.second.parse_sgpr_count << "," << kit.second.note_sgpr_count << "," << kit.second.kd_sgpr_count << ",";
      cout << kit.second.parse_vgpr_count << "," << kit.second.note_vgpr_count << "," << kit.second.kd_vgpr_count << endl;
      first_print = false;
      }*/
    return 0;    
}

