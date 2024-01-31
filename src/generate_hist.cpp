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
#include "helper.hpp"

using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::SymtabAPI;

using std::string, std::cout , std::endl;

char * filename;
bool first = true;


void analyzeLiveness(ParseAPI::Function * f, std::map<std::string, per_kernel_data> & kernel_data){
    //printf("callling analyze liveness on function %s\n",f->name().c_str());
    LivenessAnalyzer la(f->isrc()->getArch(),f->obj()->cs()->getAddressWidth());
    uint32_t sgpr_count ,vgpr_count ,agpr_count;
    auto blocks = f->blocks();
    auto bit = blocks.begin();
    bitArray liveRegs;

    std::string kname = f->name();
    const char * ckname = kname.c_str();

    for(; bit != blocks.end(); bit++){
        Block * bb = * bit;
        Address curAddr = bb->start();
        Dyninst::Address endAddr = bb->end();
        printf("%s,%s,%d,%d",filename,ckname,kernel_data[kname].note_sgpr_count,kernel_data[kname].note_vgpr_count);
        while( curAddr < endAddr){
            Instruction curInsn = bb->getInsn(curAddr);
            InsnLoc i(bb,curAddr,curInsn);
            Location loc(f,i);
            liveRegs = la.getABI()->getBitArray();;
            if(la.query(loc,LivenessAnalyzer::Before,liveRegs)){
                parseBitArray(liveRegs,sgpr_count ,vgpr_count , agpr_count);
                if(sgpr_count < 5)
                    printf(",0x%lx,%d,%d",(unsigned long) curAddr,sgpr_count,vgpr_count);
            } 
            liveRegs.reset();
            curAddr += curInsn.size();
        }
        puts("");
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

