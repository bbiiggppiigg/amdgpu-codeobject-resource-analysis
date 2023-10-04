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


using namespace Dyninst::ParseAPI;
using namespace Dyninst::InstructionAPI;
using namespace Dyninst::SymtabAPI;

using std::string, std::cout , std::endl;

typedef struct {
    uint32_t note_sgpr_count;
    uint32_t note_vgpr_count;
    uint32_t kd_sgpr_count;
    uint32_t kd_vgpr_count;
    uint32_t parse_sgpr_count;
    uint32_t parse_vgpr_count;
}per_kernel_data;

std::map<std::string, per_kernel_data > kernel_data;
std::map<std::string, std::string> prettyNameMap;
std::map<std::string, std::string> uglyNameMap;

void parse_note(ELFIO::section * noteSection )
{
    uint32_t offset = 0;
    const char * noteSectionData = noteSection->get_data();
    uint32_t noteSectionSize = noteSection->get_size();
    std::string str;
    str.assign(noteSectionData, noteSectionData + noteSectionSize);

    /**
     * Step 2, parse some non-msgpack header data
     * First 4 bytes : Size of the Name str (should be AMDGPU\0)
     * Second 4 bytes: Size of the note in msgpack format
     * Third 4 bytes : Type of the note (should be 32)
     * Followed by the Name str
     * Padding until 4 byte aligned
     * Followed by msgpack data
     */ 

    std::string name_szstr=str.substr(0,4);
    uint32_t name_sz = *( (uint32_t*)  name_szstr.c_str());
    std::string note_type = str.substr(8,4);
    std::string name = str.substr(12,name_sz);
    offset = 12 + name_sz;
    while(offset%4) offset++;


    msgpack::object_handle oh; 
    std::map<std::string, msgpack::object > kvmap;
    std::vector<msgpack::object> kernarg_list;
    std::vector<msgpack::object> kernel_list;
    std::map<std::string, msgpack::object> kernarg_list_map;
    std::vector<msgpack::object> arg_list_map;
    msgpack::zone z;

    oh = msgpack::unpack( noteSectionData+offset, noteSectionSize-offset);
    msgpack::object map_root = oh.get();
    map_root.convert(kvmap);
    kvmap["amdhsa.kernels"].convert(kernel_list);
    for ( size_t kid = 0 ; kid < kernel_list.size() ;kid ++){
        kernel_list[kid].convert(kernarg_list_map);
        kernarg_list_map[".args"].convert(arg_list_map);

        std::string kname;
        uint32_t kernarg_segment_size, group_segment_fixed_size, private_segment_fixed_size;
        uint32_t sgpr_count , vgpr_count , agpr_count;
        uint32_t sgpr_spill_count , vgpr_spill_count;

        kernarg_list_map[".name"].convert(kname); 
        kernarg_list_map[".kernarg_segment_size"].convert(kernarg_segment_size); 
        kernarg_list_map[".group_segment_fixed_size"].convert(group_segment_fixed_size);
        kernarg_list_map[".private_segment_fixed_size"].convert(private_segment_fixed_size);
        kernarg_list_map[".sgpr_count"].convert(sgpr_count); 
        kernarg_list_map[".vgpr_count"].convert(vgpr_count); 
        try{
            kernarg_list_map[".agpr_count"].convert(agpr_count); 
        }catch(...){

        }
        kernarg_list_map[".sgpr_spill_count"].convert(sgpr_spill_count); 
        kernarg_list_map[".vgpr_spill_count"].convert(vgpr_spill_count); 
        std::string pname = prettyNameMap[kname];
        if(kernel_data.count(kname)==0){
            per_kernel_data  data;
            kernel_data[pname] = data;
        }
        
        kernel_data[pname].note_sgpr_count = sgpr_count;
        kernel_data[pname].note_vgpr_count = vgpr_count;
#if 0
        cout << " kernel name = " << kname << endl;
        cout << " sgpr , vgpr , agpr usage = " << sgpr_count << " " << vgpr_count << " " << agpr_count << endl;
        cout << " kernarg_segment_size, group_segment_size(LDS) , private_segment_size = " << kernarg_segment_size 
            << " " << group_segment_fixed_size << " " << private_segment_fixed_size << endl;
#endif
    }
}

ELFIO::section *getSection(const std::string &sectionName,
        const ELFIO::elfio &file) {
    for (int i = 0; i < file.sections.size(); ++i) {
        if (file.sections[i]->get_name() == sectionName)
            return file.sections[i];
    }
    return nullptr;
}

char * filename;
bool first = true;

// return ceil(A/B)
uint32_t ceil( uint32_t A , uint32_t B){
    return (A+B-1)/B;
}

void setupPrettyNameMapping(char * binaryPath){
    std::string filePath(binaryPath);
    std::ifstream inputFile(filePath);

    inputFile.seekg(0, std::ios::end);
    size_t length = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);

    char *buffer = new char[length];
    inputFile.read(buffer, length);

    // for loading symtab
    std::string name;
    bool error;
    Symtab symtab(reinterpret_cast<unsigned char *>(buffer), length, name, /*defensive_binary=*/false, error);
    if (error) {
        std::cerr << "error loading " << filePath << '\n';
        exit(1);
    }

    std::vector<Symbol *> symbols;
    symtab.getAllSymbols(symbols);

    for (const Symbol *symbol : symbols) {
        std::string prettyName = symbol->getPrettyName();
        std::string kName = symbol->getMangledName();
        prettyNameMap[kName] = prettyName;
        uglyNameMap[prettyName] = kName;
    }

}
void parseKD(char * binaryPath){

    std::string filePath(binaryPath);
    std::ifstream inputFile(filePath);

    inputFile.seekg(0, std::ios::end);
    size_t length = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);

    char *buffer = new char[length];
    inputFile.read(buffer, length);

    Elf_X *elfHeader = Elf_X::newElf_X(buffer, length);

    // for loading symtab
    std::string name;
    bool error;
    Symtab symtab(reinterpret_cast<unsigned char *>(buffer), length, name,
            /*defensive_binary=*/false, error);
    if (error) {
        std::cerr << "error loading " << filePath << '\n';
        exit(1);
    }

    std::vector<Symbol *> symbols;
    symtab.getAllSymbols(symbols);

    for (const Symbol *symbol : symbols) {
        if (isKernelDescriptor(symbol)) {
            KernelDescriptor kd(symbol, elfHeader);
            uint32_t granulated_sgpr_count = kd.getCOMPUTE_PGM_RSRC1_GranulatedWavefrontSgprCount();
            uint32_t granulated_vgpr_count = kd.getCOMPUTE_PGM_RSRC1_GranulatedWorkitemVgprCount();

            if(granulated_sgpr_count){
                granulated_sgpr_count = granulated_sgpr_count * 8 + 16;
            }
            if(granulated_vgpr_count){
                if(kd.isGfx90aOr940()){
                    //printf("granulated_vgpr_count = %u\n",granulated_vgpr_count);
                    granulated_vgpr_count = granulated_vgpr_count * 8 +  8;
                }else if(kd.isGfx9()){
                    granulated_vgpr_count = granulated_vgpr_count * 4 + 4;
                }
            }

            std::string kname = symbol->getPrettyName();
            kname.erase(kname.length()-3);
            if(kernel_data.count(kname)==0){
                per_kernel_data  data;
                kernel_data[kname] = data;
            }

            kernel_data[kname].kd_sgpr_count = granulated_sgpr_count;
            kernel_data[kname].kd_vgpr_count = granulated_vgpr_count;

            //printf("granulated_sgpr_count = %u , granulated_vgpr_count = %u\n", granulated_sgpr_count, granulated_vgpr_count );
        }
    }

}
void parseBitArray(bitArray &ba, uint32_t & sgpr_count , uint32_t & vgpr_count , uint32_t & agpr_count ){
    const uint32_t MAX_SGPR_COUNT = 102;
    const uint32_t MAX_VGPR_COUNT = 256;
    const uint32_t MAX_AGPR_COUNT = 256;
    bitArray copy_1 = ba;
    copy_1.resize(MAX_SGPR_COUNT);
    bitArray copy_2 = ba;
    copy_2 = copy_2 >> MAX_SGPR_COUNT;
    copy_2.resize(MAX_VGPR_COUNT);
    bitArray copy_3 = ba;
    copy_3 = copy_3 >> (MAX_SGPR_COUNT + MAX_VGPR_COUNT);
    copy_3.resize(MAX_AGPR_COUNT);
    sgpr_count = copy_1.count();
    vgpr_count = copy_2.count();
    agpr_count = copy_3.count();

}

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
    setupPrettyNameMapping(filename);
    parse_note(noteSection);
    parseKD(argv[1]);

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
        cout <<  kit.second.note_vgpr_count << "," << kit.second.kd_vgpr_count << endl;
  }
    return 0;    
}

