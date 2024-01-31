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


void parse_note(ELFIO::section * noteSection, std::map<std::string, per_kernel_data > & kernel_data, std::map<std::string, std::string> & prettyNameMap)
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

ELFIO::section *getSection(const std::string &sectionName, const ELFIO::elfio &file) {
    for (int i = 0; i < file.sections.size(); ++i) {
        if (file.sections[i]->get_name() == sectionName)
            return file.sections[i];
    }
    return nullptr;
}


void setupPrettyNameMapping(char * binaryPath, std::map<std::string, std::string> & prettyNameMap,  std::map<std::string, std::string> & uglyNameMap){
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
void parseKD(char * binaryPath,std::map<std::string, per_kernel_data > & kernel_data){

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

            uint32_t sgpr_count = 16;
            if(granulated_sgpr_count){
                if((granulated_sgpr_count %2)){
                    granulated_sgpr_count -= 1;
                    printf("Warning, granulated_sgpr_count = %u, is not even, offset = %lx\n",granulated_sgpr_count,symbol->getOffset());
                    printf("pgm_rsrc1 = %x\n",kd.kdRepr.compute_pgm_rsrc1);
                    printf("new count should be %u\n",granulated_sgpr_count*8+16);
                }
                //
                // sgprs_used 0..112
                // granulated_sgpr_count = 2 * max(0, ceil(sgprs_used / 16) - 1)
                // sgpr_used = ((granulated_sgpr_count / 2) + 1 ) * 16
                // ceil(sgpr_used/16) = ((granulated_sgpr_count >> 1) + 1)
                // if granulated_sgpr_count = 0, sgpr_used = 16
                // if granulated_sgpr_count = 2, sgpr_used = 32
                // if granulated_sgpr_count = 4, sgpr_used = 48
                // ....
                // if granulated_sgpr_count = 12,sgpr_used = 112? 
                // 0 ... 15 share the same sgpr_used = 16
                // 16 ... 31 shared the same sgpr_used = 32
                //
                sgpr_count = granulated_sgpr_count * 8 + 16;
                if(sgpr_count%16){
                    printf("Warning, sgpr_count = %u, is not x16, offset = %lx\n",sgpr_count,symbol->getOffset());
                    printf("pgm_rsrc1 = %x\n",kd.kdRepr.compute_pgm_rsrc1);
                    printf("new count should be %u\n",granulated_sgpr_count*8+16);
                }
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

            kernel_data[kname].kd_sgpr_count = sgpr_count;
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
#define IS_SGPR(x) ( x == Dyninst::amdgpu_gfx940::SGPR || x == Dyninst::amdgpu_gfx908::SGPR || x == Dyninst::amdgpu_gfx90a::SGPR )
#define IS_VGPR(x) ( x == Dyninst::amdgpu_gfx940::VGPR || x == Dyninst::amdgpu_gfx908::VGPR || x == Dyninst::amdgpu_gfx90a::VGPR )
#define IS_ACC_VGPR(x) ( x == Dyninst::amdgpu_gfx908::ACC_VGPR || x == Dyninst::amdgpu_gfx90a::ACC_VGPR || Dyninst::amdgpu_gfx940::ACC_VGPR)
#define MAX(x,y) ((x > y ) ? x : y)


void getMaxRegisterUsed(InstructionDecoder & decoder, ParseAPI::Function * f, int32_t & max_sgpr , int32_t & max_vgpr ,int32_t & max_acc_vgpr){
    uint64_t max_sgpr_addr, max_vgpr_addr ,max_acc_vgpr_addr;
    max_sgpr_addr = max_vgpr_addr = max_acc_vgpr_addr = 0;
    std::map<Dyninst::Address,bool> seen;
    auto blocks = f->blocks();
    auto bit = blocks.begin();
    max_sgpr = max_vgpr = max_acc_vgpr = -1;
    for(; bit != blocks.end(); bit++){
        Block * b = * bit;
        if(seen.find(b->start()) != seen.end()) continue;
        seen[b->start()] = true;
        Dyninst::Address currAddr = b->start();
        Dyninst::Address endAddr = b->end();
        while( currAddr < endAddr){
            Instruction instr = decoder.decode(
                    (unsigned char * ) f->isrc()->getPtrToInstruction(currAddr));    
#ifdef DEBUG
            cout << std::hex << " " << currAddr << " " << instr.format() << endl;
#endif
            std::vector<Dyninst::InstructionAPI::Operand> operands;
            instr.getOperands(operands);
            for( auto & opr : operands){
                //boost::shared_ptr<RegisterAST::Ptr> reg = boost::dynamic_pointer_cast<RegisterAST::Ptr>(opr.getValue());
                auto reg = boost::dynamic_pointer_cast<RegisterAST>( (opr.getValue()));
                if(reg){
                    Dyninst::MachRegister m_Reg = reg->getID();
                    int32_t reg_id = m_Reg & 0xff;
                    uint32_t reg_class = m_Reg.regClass();

                    if( IS_SGPR(reg_class) & (max_sgpr < reg_id) ){
                        max_sgpr = reg_id;
                        max_sgpr_addr = currAddr;
                    }
                    if( IS_VGPR(reg_class) & (max_vgpr < reg_id) ){
                        max_vgpr = reg_id;
                        max_vgpr_addr = currAddr;
                    }
                    if( IS_ACC_VGPR(reg_class) & (max_acc_vgpr < reg_id) ){
                        max_acc_vgpr = reg_id;
                        max_acc_vgpr_addr = currAddr;
                    }
                }
            }
            currAddr += instr.size();
        }
    }
    //Instruction instr = decoder.decode(
    //               (unsigned char * ) f->isrc()->getPtrToInstruction(max_sgpr_addr));    
    //fprintf(stderr," max_sgpr_addr = 0x%lx, %u, %s\n", max_sgpr_addr,max_sgpr,instr.format().c_str());
    //fprintf(stderr," max_vgpr_addr = 0x%lx, %u\n", max_vgpr_addr,max_vgpr);
    //fprintf(stderr," max_acc_vgpr_addr = 0x%lx, %u\n", max_acc_vgpr_addr,max_acc_vgpr);
    max_sgpr += 1;
    max_vgpr += 1;
    max_acc_vgpr += 1;

}

void parseInstructions( char * binary_path, std::map<std::string, per_kernel_data> & kernel_data ){
    SymtabCodeSource * scs = new SymtabCodeSource(binary_path);

    CodeObject * dyn_co = new CodeObject(scs);

    dyn_co->parse();
    auto arch = scs -> getArch();
    InstructionDecoder decoder("",InstructionDecoder::maxInstructionLength,arch);
    const CodeObject::funclist & all = dyn_co->funcs();
    auto fit = all.begin();
    for( ; fit != all.end(); fit++){
        ParseAPI::Function * f = * fit;

        std::string kname = f->name();
        int32_t max_sgpr, max_vgpr , max_acc_vgpr;
        getMaxRegisterUsed(decoder, f , max_sgpr  , max_vgpr, max_acc_vgpr);
        if(kernel_data.count(kname)==0){
            assert(0);
        }else{
            kernel_data[kname].parse_sgpr_count = max_sgpr;
            kernel_data[kname].parse_vgpr_count = max_vgpr;
        } 
        //analyzeLiveness(f);
        if(!(max_sgpr + max_vgpr + max_acc_vgpr))
            continue;
    }
    delete dyn_co;
    delete scs;
}


