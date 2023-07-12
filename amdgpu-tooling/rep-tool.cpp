#include "DyninstUtility.h"
#include "third-party/RawElf.h"

// Final goal -- clone and replace original code with original code + in-place
// instrumentation. This tool exists to develop and test DyninstUtility.

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cout << "exactly 3 arguments to " << argv[0] << " expected\n";
    exit(1);
  }

  const char *ogObjPath = argv[1];
  const char *instrObjPath = argv[2];
  const char *newObjPath = argv[3];

  ELFIO::elfio ogObj;
  ELFIO::elfio instrObj; // The contents of this object are used as placeholder
                         // for accessing blobs of instrumented code. The
                         // offsets and size of instrumented functions must be
                         // tracked when inserting in-place instrumentaton.
  ELFIO::elfio newObj;
  DyninstUtility dyUtil;

  if (!ogObj.load(ogObjPath)) {
    std::cout << "can't find or process ELF file " << ogObjPath << '\n';
    exit(1);
  }

  if (!instrObj.load(instrObjPath)) {
    std::cout << "can't find or process ELF file " << ogObjPath << '\n';
    exit(1);
  }

  // step 1. create a clone of ogObj in newObj.
  dyUtil.cloneObj(ogObj, newObj);

  // step 2. replace newObj's (i.e clone) text section with instrObj's text
  // section.
  ELFIO::section *instrObjTextSection = dyUtil.getSection(instrObj, ".text");
  dyUtil.replaceSectionContents(newObj, ".text",
                                instrObjTextSection->get_data(),
                                instrObjTextSection->get_size());

  // step 3. patch the symtab entries in newObj.
  RawElf::Elf64_Sym instrSymbol;
  RawElf::Elf64_Sym newSymbol;
  dyUtil.getSymbol(newObj, "main", newSymbol);
  dyUtil.getSymbol(instrObj, "main", instrSymbol);

  newSymbol.st_value = instrSymbol.st_value;
  newSymbol.st_size = instrSymbol.st_size;

  dyUtil.updateSymbol(newObj, newSymbol);

  dyUtil.getSymbol(newObj, "incr", newSymbol);
  dyUtil.getSymbol(instrObj, "incr", instrSymbol);

  newSymbol.st_value = instrSymbol.st_value;
  newSymbol.st_size = instrSymbol.st_size;

  dyUtil.updateSymbol(newObj, newSymbol);

  // step 4. patch the relocations in newObj.
  // In this demo case, relocations can be patched by simply updating the entire
  // rela.text section. However, in actual use, r_offset must be updated
  // correctly.
  // FIXME : this kind of cloning doesn't work for X86 object files as of now.
  // Need to see how clones of AMDGPU relocatables behave.

  ELFIO::section *newEhFrame = dyUtil.getSection(newObj, ".eh_frame");
  ELFIO::section *instrEhFrame = dyUtil.getSection(instrObj, ".eh_frame");
  newEhFrame->set_data(instrEhFrame->get_data(), instrEhFrame->get_size());

  ELFIO::section *newRelaEhFrame = dyUtil.getSection(newObj, ".rela.eh_frame");
  ELFIO::section *instrRelaEhFrame =
      dyUtil.getSection(instrObj, ".rela.eh_frame");
  newRelaEhFrame->set_data(instrRelaEhFrame->get_data(),
                           instrRelaEhFrame->get_size());

  ELFIO::section *newRelaText = dyUtil.getSection(newObj, ".rela.text");
  ELFIO::section *instrRelaText = dyUtil.getSection(instrObj, ".rela.text");
  newRelaText->set_data(instrRelaText->get_data(), instrRelaText->get_size());

  newObj.save(newObjPath);
}
