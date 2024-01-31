#ifndef HELPER_HPP
#define HELPER_HPP
#include <msgpack.hpp>
#include <string>
#include <iostream>
#include <sstream>
#include <string>
#include <elfio/elfio.hpp>
#include  <map>

typedef struct {
    uint32_t note_sgpr_count;
    uint32_t note_vgpr_count;
    uint32_t kd_sgpr_count;
    uint32_t kd_vgpr_count;
    uint32_t parse_sgpr_count;
    uint32_t parse_vgpr_count;
}per_kernel_data;

void parse_note(ELFIO::section * noteSection, std::map<std::string, per_kernel_data > &kernel_data, std::map<std::string, std::string> & prettyNameMap);
ELFIO::section *getSection(const std::string &sectionName, const ELFIO::elfio &file);
void setupPrettyNameMapping(char * binaryPath, std::map<std::string, std::string> & prettyNameMap,  std::map<std::string, std::string> & uglyNameMap);
void parseKD(char * binaryPath,std::map<std::string, per_kernel_data > & kernel_data);
void parseBitArray(bitArray &ba, uint32_t & sgpr_count , uint32_t & vgpr_count , uint32_t & agpr_count );


void getMaxRegisterUsed(InstructionDecoder & decoder, ParseAPI::Function * f, int32_t & max_sgpr , int32_t & max_vgpr ,int32_t & max_acc_vgpr);
void parseInstructions( char * binary_path, std::map<std::string, per_kernel_data> & kernel_data );
#endif
