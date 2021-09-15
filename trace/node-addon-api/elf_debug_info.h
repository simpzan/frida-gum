#pragma once
#include <string>
#include <memory>
#include <vector>
#include <elf/elf++.hh>
#include <dwarf/dwarf++.hh>
#include "utils.h"

class ELF;
class DebugInfo {
  void loadDieR(const dwarf::die &die);
 public:
  DebugInfo(const ELF &elf);
  const dwarf::die die(uint64_t addr) const;
 private:
  const std::vector<dwarf::compilation_unit> &cus_;
  std::map<uint64_t, dwarf::die> dies;
  std::vector<dwarf::die> conflictedDies;
};

struct FunctionSymbol {
  uint32_t address;
  uint32_t size;
  std::string name;
};
class ELF {
 public:
  static std::unique_ptr<ELF> create(const char *file);
  ~ELF() {}
  enum class Arch {
    x86   = 0x03,
    x64   = 0x3E,
    arm32 = 0x28,
    arm64 = 0xB7,
  };
  Arch arch() const { return arch_; }
  std::vector<uint8_t> buildId() const;
  uint64_t vaddr() const;
  std::vector<FunctionSymbol> functionSymbols();

 private:
  std::string path_;
  std::unique_ptr<elf::elf> ef_;
  std::unique_ptr<dwarf::dwarf> dw_;
  Arch arch_;
  friend class DebugInfo;
};

static inline std::string archString(ELF::Arch arch) {
  switch (arch) {
  case ELF::Arch::x86: return "x86";
  case ELF::Arch::x64: return "x64";
  case ELF::Arch::arm32: return "arm32";
  case ELF::Arch::arm64: return "arm64";
  default: return "unknown";
  }
}
std::string decl_file(const dwarf::die &die);
static inline int decl_line(const dwarf::die &die) {
  auto value = die.resolve(dwarf::DW_AT::decl_line);
  if (!value.valid()) return -1;
  return value.as_uconstant();
}
std::string demangle(const char *mangled_name);
