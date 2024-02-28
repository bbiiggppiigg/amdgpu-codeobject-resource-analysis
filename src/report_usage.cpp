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

using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::SymtabAPI;

using std::string, std::cout , std::endl;
#include "helper.hpp"
char * filename;
bool first = true;

bool fname_not_printed = true;

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

    setupPrettyNameMapping(filename,prettyNameMap,uglyNameMap);
    parse_note(noteSection,kernel_data,prettyNameMap);
    parseKD(argv[1],kernel_data);

    
    for ( auto & kit : kernel_data){
        cout << argv[1] << ",";
        cout << uglyNameMap[kit.first] << ",";
        if(kit.second.note_sgpr_count > kit.second.kd_sgpr_count){
            assert(0);
        }
        if(kit.second.note_vgpr_count > kit.second.kd_vgpr_count){
            assert(0);
        }
        cout <<  kit.second.note_sgpr_count << "," << kit.second.kd_sgpr_count << ",";
        cout <<  kit.second.note_vgpr_count << "," << kit.second.kd_vgpr_count << ",";
        cout <<  kit.second.note_agpr_count << "," << kit.second.kd_agpr_count <<  endl;
  }
    return 0;    
}

