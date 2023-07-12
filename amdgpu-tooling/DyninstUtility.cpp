#include "DyninstUtility.h"
#include <cassert>
#include <cstring>

void DyninstUtility::reset() {
  ogToNewSectionMap.clear();
  newToOgSectionMap.clear();
}

bool DyninstUtility::cloneObj(const ELFIO::elfio &fromObj,
                              ELFIO::elfio &toObj) {
  if (fromObj.get_type() != ELFIO::ET_REL) {
    std::cerr << "Can only clone relocatable objects\n";
    return false;
  }

  cloneHeader(fromObj, toObj);
  cloneSections(fromObj, toObj);
  correctSectionLinks(fromObj, toObj);

  correctSectionInfoForRelocationSections(fromObj, toObj);
  correctSectionIndexForSymbols(fromObj, toObj);

  return true;
}

bool DyninstUtility::getSymbol(const ELFIO::elfio &elfObj, const unsigned idx,
                               RawElf::Elf64_Sym &symbol) const {

  ELFIO::section *symtab = getSymtabSection(elfObj);
  ELFIO::symbol_section_accessor symtabAccessor(elfObj, symtab);

  if (idx >= symtabAccessor.get_symbols_num())
    return false;

  RawElf::Elf64_Sym *symbols = (RawElf::Elf64_Sym *)(symtab->get_data());
  symbol = symbols[idx];
  return true;
}

bool DyninstUtility::getSymbol(const ELFIO::elfio &elfObj,
                               const std::string &name,
                               RawElf::Elf64_Sym &symbol) const {

  ELFIO::section *symtab = getSymtabSection(elfObj);
  ELFIO::symbol_section_accessor symtabAccessor(elfObj, symtab);

  ELFIO::Elf64_Addr value;
  ELFIO::Elf_Xword size;
  unsigned char bind;
  unsigned char type;
  ELFIO::Elf_Half sectionIndex;
  unsigned char other;

  if (symtabAccessor.get_symbol(name, value, size, bind, type, sectionIndex,
                                other) == false) {
    return false;
  }

#ifndef ELF64_ST_INFO
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t)&0xf))

  symbol.st_info = ELF64_ST_INFO(bind, type);
  symbol.st_other = other;
  symbol.st_shndx = sectionIndex;
  symbol.st_value = value;
  symbol.st_size = size;

#undef ELF64_ST_INFO
#endif

  // Another ELFIO quirk - can't get the st_name field (index of symbol name in
  // .strtab). So we manually go over the bytes in .strtab to get the index.

  ELFIO::section *strtab = getStrtabSection(elfObj);
  const char *strtabData = strtab->get_data();

  ELFIO::Elf_Xword idx = 0;
  for (idx; idx < strtab->get_size(); ++idx) {
    if (std::strncmp(name.c_str(), &strtabData[idx], name.length()) == 0)
      break;
  }
  symbol.st_name = idx;

  return true;
}

ELFIO::section *DyninstUtility::getSection(const ELFIO::elfio &elfObj,
                                           const std::string &name) const {
  for (int i = 0; i < elfObj.sections.size(); ++i) {
    if (elfObj.sections[i]->get_name() == name)
      return elfObj.sections[i];
  }
  return nullptr;
}

ELFIO::section *
DyninstUtility::getSymtabSection(const ELFIO::elfio &elfObj) const {
  for (int i = 0; i < elfObj.sections.size(); ++i) {
    auto section = elfObj.sections[i];
    if (section->get_type() == ELFIO::SHT_SYMTAB)
      return section;
  }
  return nullptr;
}

ELFIO::section *
DyninstUtility::getStrtabSection(const ELFIO::elfio &elfObj) const {
  for (int i = 0; i < elfObj.sections.size(); ++i) {
    auto section = elfObj.sections[i];
    if (section->get_type() == ELFIO::SHT_STRTAB)
      return section;
  }
  return nullptr;
}

void DyninstUtility::replaceSectionContents(ELFIO::elfio &elfObj,
                                            const std::string &sectionName,
                                            const char *newContents,
                                            size_t newSize) {
  auto section = getSection(elfObj, sectionName);
  section->set_data(newContents, newSize);
}

bool DyninstUtility::updateSymbol(ELFIO::elfio &elfObj,
                                  RawElf::Elf64_Sym &symbol) const {
  ELFIO::section *symtab = getSymtabSection(elfObj);
  RawElf::Elf64_Sym *symbols = (RawElf::Elf64_Sym *)symtab->get_data();
  ELFIO::symbol_section_accessor symtabAccessor(elfObj, symtab);

  for (ELFIO::Elf_Xword i = 0; i < symtabAccessor.get_symbols_num(); ++i) {
    if (symbols[i].st_name == symbol.st_name &&
        symbols[i].st_info == symbol.st_info &&
        symbols[i].st_other == symbol.st_other &&
        symbols[i].st_shndx == symbol.st_shndx) {

      symbols[i].st_value = symbol.st_value;
      symbols[i].st_size = symbol.st_size;
      return true;
    }
  }
  return false;
}

bool DyninstUtility::updateRel(ELFIO::elfio &elfObj, ELFIO::section *relSection,
                               const unsigned idx, RawElf::Elf64_Rel &rel) {
  assert(relSection->get_type() == ELFIO::SHT_REL);
  ELFIO::relocation_section_accessor relAccessor(elfObj, relSection);
  RawElf::Elf64_Rel *relocs = (RawElf::Elf64_Rel *)relSection->get_data();

  if (idx >= relAccessor.get_entries_num())
    return false;

  relocs[idx] = rel;
  return true;
}

bool DyninstUtility::updateRela(ELFIO::elfio &elfObj,
                                ELFIO::section *relaSection, const unsigned idx,
                                RawElf::Elf64_Rela &rela) {
  assert(relaSection->get_type() == ELFIO::SHT_RELA);
  ELFIO::relocation_section_accessor relaAccessor(elfObj, relaSection);
  RawElf::Elf64_Rela *relocs = (RawElf::Elf64_Rela *)relaSection->get_data();

  if (idx >= relaAccessor.get_entries_num())
    return false;

  relocs[idx] = rela;
  return true;
}

bool DyninstUtility::shouldClone(const ELFIO::section *section) {
  switch (section->get_type()) {
  case ELFIO::SHT_NULL:
    return false;

  case ELFIO::SHT_STRTAB:
    // Don't clone section header string table, ELFIO will create a new one
    if (section->get_name() == ".shstrtab")
      return false;
    return true;

  default:
    return true;
  }
}

void DyninstUtility::cloneHeader(const ELFIO::elfio &ogObj,
                                 ELFIO::elfio &newObj) {
  newObj.create(ogObj.get_class(), ogObj.get_encoding());
  newObj.set_os_abi(ogObj.get_os_abi());
  newObj.set_abi_version(ogObj.get_abi_version());
  newObj.set_type(ogObj.get_type());
  newObj.set_machine(ogObj.get_machine());
  newObj.set_entry(ogObj.get_entry());
}

void DyninstUtility::cloneSections(const ELFIO::elfio &ogObj,
                                   ELFIO::elfio &newObj) {
  auto ogSections = ogObj.sections;
  for (size_t i = 0; i < ogSections.size(); ++i) {
    ELFIO::section *ogSection = ogSections[i];

    if (!shouldClone(ogSection))
      continue;

    const std::string &name = ogSection->get_name();
    ELFIO::section *newSection = newObj.sections.add(name);
    newSection->set_type(ogSection->get_type());
    newSection->set_flags(ogSection->get_flags());
    newSection->set_info(ogSection->get_info());

    // NOTE: This can be incorrect link, and will be corrected later, after all
    // sections are cloned.
    newSection->set_link(ogSection->get_link());

    newSection->set_addr_align(ogSection->get_addr_align());
    newSection->set_entry_size(ogSection->get_entry_size());
    newSection->set_address(ogSection->get_address());
    newSection->set_size(ogSection->get_size());

    if (const char *contents = ogSection->get_data())
      newSection->set_data(contents, ogSection->get_size());

    ogToNewSectionMap[ogSection] = newSection;
    newToOgSectionMap[newSection] = ogSection;
  }
}

void DyninstUtility::correctSectionLinks(const ELFIO::elfio &ogObj,
                                         ELFIO::elfio &newObj) {
  auto ogSections = ogObj.sections;
  auto newSections = newObj.sections;

  // If ogSection's sh_link holds index in ogObj's section header table, we
  // must update newSection's sh_link hold corresponding index in newObj's
  // section header table.
  for (size_t i = 0; i < newSections.size(); ++i) {
    auto *currentNewSection = newSections[i];

    auto iter1 = newToOgSectionMap.find(currentNewSection);
    if (iter1 == newToOgSectionMap.end())
      continue;

    auto *ogSection = iter1->second;
    auto ogLinkSectionIdx = ogSection->get_link();
    auto *ogLinkSection = ogSections[ogLinkSectionIdx];

    auto iter2 = ogToNewSectionMap.find(ogLinkSection);
    if (iter2 == ogToNewSectionMap.end())
      continue;

    auto *newLinkSection = iter2->second;
    auto newLinkSectionIdx = newLinkSection->get_index();
    currentNewSection->set_link(newLinkSectionIdx);
  }
}

void DyninstUtility::correctSectionInfoForRelocationSections(
    const ELFIO::elfio &ogObj, ELFIO::elfio &newObj) {
  auto ogSections = ogObj.sections;
  auto newSections = newObj.sections;

  // If a relocation section ogSection's sh_info holds index in ogObj's section
  // header table, we must update newSection's sh_info hold corresponding index
  // in newObj's section header table.
  for (size_t i = 0; i < newSections.size(); ++i) {
    auto *currentNewSection = newSections[i];
    auto sectionType = currentNewSection->get_type();

    if (sectionType != ELFIO::SHT_REL && sectionType != ELFIO::SHT_RELA)
      continue;

    auto iter1 = newToOgSectionMap.find(currentNewSection);
    if (iter1 == newToOgSectionMap.end())
      continue;

    auto *ogSection = iter1->second;
    auto ogInfoSectionIdx = ogSection->get_info();
    auto *ogInfoSection = ogSections[ogInfoSectionIdx];

    auto iter2 = ogToNewSectionMap.find(ogInfoSection);
    if (iter2 == ogToNewSectionMap.end())
      continue;

    auto *newInfoSection = iter2->second;
    auto newInfoSectionIdx = newInfoSection->get_index();
    currentNewSection->set_info(newInfoSectionIdx);
  }
}

// Since we are cloning relocatables, symbol values are offsets from a
// some section. ELFIO creates a .shstrtab at index 1 which shifts indices
// for other sections. We want to edit the section index for symbols in newObj
// but ELFIO doesn't support editing in-place. So we edit contents of the buffer
// directly using raw ELF structs.
void DyninstUtility::correctSectionIndexForSymbols(const ELFIO::elfio &ogObj,
                                                   ELFIO::elfio &newObj) {
  ELFIO::section *ogSymtab = getSymtabSection(ogObj);
  assert(ogSymtab && "original elf object must contain symbol table");
  ELFIO::symbol_section_accessor ogSymtabAccessor(ogObj, ogSymtab);

  ELFIO::section *newSymtab = getSymtabSection(newObj);
  RawElf::Elf64_Sym *symbols = (RawElf::Elf64_Sym *)(newSymtab->get_data());

  for (ELFIO::Elf_Xword idx = 0; idx < ogSymtabAccessor.get_symbols_num();
       ++idx) {
    ELFIO::Elf64_Half oldIdx = symbols[idx].st_shndx;
    ELFIO::section *oldSection = ogObj.sections[oldIdx];

    auto iter = ogToNewSectionMap.find(oldSection);
    if (iter == ogToNewSectionMap.end()) {
      continue;
    }
    symbols[idx].st_shndx = iter->second->get_index();
  }
}
