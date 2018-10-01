//===- Dwarf2Btf.cpp ------------------------------------------ *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Dwarf2Btf.h"

namespace llvm {

unsigned char BtfTypeEntry::getDieKind(const DIE & Die) {
  auto Tag = Die.getTag();

  switch (Tag) {
    case dwarf::DW_TAG_base_type:
      if (getBaseTypeEncoding(Die) == BTF_INVALID_ENCODING)
        return BTF_KIND_UNKN;
      return BTF_KIND_INT;
    case dwarf::DW_TAG_const_type:
      return BTF_KIND_CONST;
    case dwarf::DW_TAG_pointer_type:
      return BTF_KIND_PTR;
    case dwarf::DW_TAG_restrict_type:
      return BTF_KIND_RESTRICT;
    case dwarf::DW_TAG_volatile_type:
      return BTF_KIND_VOLATILE;
    case dwarf::DW_TAG_structure_type:
    case dwarf::DW_TAG_class_type:
      if (Die.findAttribute(dwarf::DW_AT_declaration).getType()
          != DIEValue::isNone)
        return BTF_KIND_FWD;
      else
        return BTF_KIND_STRUCT;
    case dwarf::DW_TAG_union_type:
      if (Die.findAttribute(dwarf::DW_AT_declaration).getType()
          != DIEValue::isNone)
        return BTF_KIND_FWD;
      else
        return BTF_KIND_UNION;
    case dwarf::DW_TAG_enumeration_type:
      return BTF_KIND_ENUM;
    case dwarf::DW_TAG_array_type:
      return BTF_KIND_UNKN;
    case dwarf::DW_TAG_subprogram:
      return BTF_KIND_UNKN; // TODO: add BTF_KIND_FUNC;
    case dwarf::DW_TAG_subroutine_type:
      return BTF_KIND_UNKN; // TODO: add BTF_KIND_FUNC_PROTO;
    case dwarf::DW_TAG_compile_unit:
      return BTF_KIND_UNKN;
    case dwarf::DW_TAG_variable:
    {
      auto TypeV = Die.findAttribute(dwarf::DW_AT_type);
      if (TypeV.getType() == DIEValue::isNone)
        return BTF_KIND_UNKN;  // TODO: fix variable with no types?

      auto &TypeDie = TypeV.getDIEEntry().getEntry();
      if (TypeDie.getTag() == dwarf::DW_TAG_array_type)
        return BTF_KIND_ARRAY;
      else
        return BTF_KIND_UNKN;
    }
    case dwarf::DW_TAG_formal_parameter:
    case dwarf::DW_TAG_typedef:  // TODO: add typedef
    case dwarf::DW_TAG_inlined_subroutine:
    case dwarf::DW_TAG_lexical_block:
      break;
    default:
      errs() << "BTF: Unsupported TAG "
             << dwarf::TagString(Die.getTag())
             << "\n";
      break;
  }

  return BTF_KIND_UNKN;
}

std::unique_ptr<BtfTypeEntry> BtfTypeEntry::dieToBtfTypeEntry(const DIE &Die) {
  unsigned char Kind = getDieKind(Die);

  switch (Kind) {
    case BTF_KIND_INT:
      return make_unique<BtfTypeEntryInt>(Die);
    case BTF_KIND_PTR:
    case BTF_KIND_TYPEDEF:
    case BTF_KIND_VOLATILE:
    case BTF_KIND_CONST:
    case BTF_KIND_RESTRICT:
      return make_unique<BtfTypeEntry>(Die);
    case BTF_KIND_ARRAY:
      return make_unique<BtfTypeEntryArray>(Die);
    case BTF_KIND_STRUCT:
    case BTF_KIND_UNION:
      return make_unique<BtfTypeEntryStruct>(Die);
    case BTF_KIND_ENUM:
      return make_unique<BtfTypeEntryEnum>(Die);
    case BTF_KIND_FUNC:
    case BTF_KIND_FUNC_PROTO:
      return make_unique<BtfTypeEntryFunc>(Die);
    default:
      break;
  }
  return nullptr;
}

bool BtfTypeEntry::shouldSkipDie(const DIE &Die) {
  auto Tag = Die.getTag();

  switch (Tag) {
    case dwarf::DW_TAG_const_type:
    case dwarf::DW_TAG_pointer_type:
    case dwarf::DW_TAG_restrict_type:
    case dwarf::DW_TAG_typedef:
    case dwarf::DW_TAG_volatile_type:
    {
      auto TypeV = Die.findAttribute(dwarf::DW_AT_type);
      if (TypeV.getType() == DIEValue::isNone) {
        if (Tag == dwarf::DW_TAG_pointer_type)
          return true;  // TODO: handle void pointer?

        errs() << "Tag " << dwarf::TagString(Tag) << " has no type\n";
        Die.print(errs());
        return true;
      }

      auto &TypeDie = TypeV.getDIEEntry().getEntry();
      return BtfTypeEntry::shouldSkipDie(TypeDie);
    }
    default:
      return getDieKind(Die) == BTF_KIND_UNKN;
  }
  return true;
}
unsigned char BtfTypeEntry::getBaseTypeEncoding(const DIE &Die) {
  auto V = Die.findAttribute(dwarf::DW_AT_encoding);

  if (V.getType() != DIEValue::isInteger)
    return BTF_INVALID_ENCODING;

  switch (V.getDIEInteger().getValue()) {
    case dwarf::DW_ATE_boolean:
      return BTF_INT_BOOL;
    case dwarf::DW_ATE_signed:
      return BTF_INT_SIGNED;
    case dwarf::DW_ATE_signed_char:  // TODO ?: do we need signed char?
      return BTF_INT_CHAR;
    case dwarf::DW_ATE_unsigned:
      return 0;
    case dwarf::DW_ATE_unsigned_char:
      return BTF_INT_CHAR;
    case dwarf::DW_ATE_imaginary_float:
    case dwarf::DW_ATE_packed_decimal:
    case dwarf::DW_ATE_numeric_string:
    case dwarf::DW_ATE_edited:
    case dwarf::DW_ATE_signed_fixed:
    case dwarf::DW_ATE_address:
    case dwarf::DW_ATE_complex_float:
    case dwarf::DW_ATE_float:
    default:
      break;
  }
  return BTF_INVALID_ENCODING;
}

BtfTypeEntry::BtfTypeEntry(const DIE &Die) : Die(Die) {
  unsigned char kind = getDieKind(Die);

  switch (kind) {
    case BTF_KIND_CONST:
    case BTF_KIND_PTR:
    case BTF_KIND_VOLATILE:
    case BTF_KIND_TYPEDEF:
    case BTF_KIND_RESTRICT:
      break;
    default:
      assert("Invalid Die passed into BtfTypeEntry()");
      break;
  }

  BtfType.info = (kind & 0xf) << 24;
}

void BtfTypeEntry::completeData(class BtfContext &BtfContext) {
    auto TypeV = Die.findAttribute(dwarf::DW_AT_type);
    auto &TypeDie = TypeV.getDIEEntry().getEntry();

    // reference types doesn't have name
    BtfType.name_off = 0;
    BtfType.type = BtfContext.getTypeIndex(TypeDie);
}

void BtfTypeEntry::print(raw_ostream &s, BtfContext& BtfContext) {
  s << "printing kind "
    << btf_kind_str[BTF_INFO_KIND(BtfType.info)] << "\n";

  s << "\tname: " << BtfContext.getTypeName(this) << "\n";
  s << "\tname_off: " << BtfType.name_off << "\n";
  s << "\tinfo: " << format("0x%08lx", BtfType.info) << "\n";
  s << "\tsize/type: " << format("0x%08lx", BtfType.size) << "\n";
}

BtfTypeEntryInt::BtfTypeEntryInt(const DIE &Die) : BtfTypeEntry(Die) {
  unsigned char kind = getDieKind(Die);

  switch (kind) {
    case BTF_KIND_INT:
      break;
    default:
      assert("Invalid Die passed into BtfTypeEntryInt()");
      break;
  }

  // handle BTF_INT_ENCODING in IntVal
  auto Encoding = BtfTypeEntry::getBaseTypeEncoding(Die);
  assert((Encoding != BTF_INVALID_ENCODING) &&
         "Invalid Die passed to BtfTypeEntryInt()");
  __u32 IntVal = (Encoding & 0xf) << 24;

  // handle BTF_INT_OFFSET in IntVal
  auto V = Die.findAttribute(dwarf::DW_AT_bit_offset);
  if (V.getType() == DIEValue::isInteger)
    IntVal |= (V.getDIEInteger().getValue() & 0xff) << 16;

  // get btf_type.size
  V = Die.findAttribute(dwarf::DW_AT_byte_size);
  __u32 Size = V.getDIEInteger().getValue() & 0xffffffff;

  // handle BTF_INT_BITS in IntVal
  V = Die.findAttribute(dwarf::DW_AT_bit_size);
  if (V.getType() == DIEValue::isInteger) {
    IntVal |= V.getDIEInteger().getValue() & 0xff;
    IntVal |= (V.getDIEInteger().getValue() & 0xff);
  } else
    IntVal |= (Size << 3) & 0xff;

  BtfType.info = BTF_KIND_INT << 24;
  BtfType.size = Size;
  this->IntVal = IntVal;
}

void BtfTypeEntryInt::completeData(class BtfContext &BtfContext) {
    auto NameV = Die.findAttribute(dwarf::DW_AT_name);
    auto TypeV = Die.findAttribute(dwarf::DW_AT_type);
    auto Str = NameV.getDIEString().getString();

    BtfType.name_off = BtfContext.addString(Str);
}

void BtfTypeEntryInt::print(raw_ostream &s, BtfContext& BtfContext) {
  BtfTypeEntry::print(s, BtfContext);

  s << "\tdesc: " << format("0x%08lx", IntVal) << "\n";
}

BtfTypeEntryEnum::BtfTypeEntryEnum(const DIE &Die) : BtfTypeEntry(Die) {
  // get btf_type.size
  auto V = Die.findAttribute(dwarf::DW_AT_byte_size);
  __u32 Size = V.getDIEInteger().getValue() & 0xffffffff;

  int Vlen = 0;
  for (auto &ChildDie : Die.children())
    if (ChildDie.getTag() == dwarf::DW_TAG_enumerator)
      Vlen++;

  BtfType.info = (BTF_KIND_ENUM << 24) | (Vlen & BTF_MAX_VLEN);
  BtfType.type = Size;
}

void BtfTypeEntryEnum::completeData(class BtfContext &BtfContext) {
  auto TypeV = Die.findAttribute(dwarf::DW_AT_type);
  auto NameV = Die.findAttribute(dwarf::DW_AT_name);

  if (NameV.getType() != DIEValue::isNone) {
    auto Str = NameV.getDIEString().getString();
    BtfType.name_off = BtfContext.addString(Str);
  } else
    BtfType.name_off = 0;

  for (auto &ChildDie : Die.children()) {
    struct btf_enum BtfEnum;
    auto ChildNameV = ChildDie.findAttribute(dwarf::DW_AT_name);
    auto Str = ChildNameV.getDIEString().getString();

    BtfEnum.name_off = BtfContext.addString(Str);
    auto ChildValueV = ChildDie.findAttribute(dwarf::DW_AT_const_value);
    BtfEnum.val = (__s32)(ChildValueV.getDIEInteger().getValue());

    EnumValues.push_back(BtfEnum);
  }
}

void BtfTypeEntryEnum::print(raw_ostream &s, BtfContext& BtfContext) {
  BtfTypeEntry::print(s, BtfContext);

  for (size_t i = 0; i < BTF_INFO_VLEN(BtfType.info); i++) {
    auto &EnumValue = EnumValues[i];
    s << "\tSymbol: " << BtfContext.getStringAtOffset(EnumValue.name_off)
      << " of value " << EnumValue.val
      << "\n";
  }
}

BtfTypeEntryArray::BtfTypeEntryArray(const DIE &Die) :
    BtfTypeEntry(Die),
    ArrayTypeDie(Die.findAttribute(dwarf::DW_AT_type).
                 getDIEEntry().getEntry()) {

  BtfType.info = (BTF_KIND_ARRAY << 24);
  BtfType.size = 0;
}

void BtfTypeEntryArray::completeData(class BtfContext &BtfContext) {
  auto NameV = Die.findAttribute(dwarf::DW_AT_name);
  auto Str = NameV.getDIEString().getString();

  BtfType.name_off = BtfContext.addString(Str);

  auto TypeV = ArrayTypeDie.findAttribute(dwarf::DW_AT_type);
  auto &TypeDie = TypeV.getDIEEntry().getEntry();

  ArrayInfo.type = BtfContext.getTypeIndex(TypeDie);

  for (auto &ChildDie : ArrayTypeDie.children()) {
    if (ChildDie.getTag() == dwarf::DW_TAG_subrange_type) {
      auto CountV = ChildDie.findAttribute(dwarf::DW_AT_count);
      ArrayInfo.nelems =
        (__u32)(CountV.getDIEInteger().getValue());

      TypeV = ChildDie.findAttribute(dwarf::DW_AT_type);
      auto &TypeDie = TypeV.getDIEEntry().getEntry();
      ArrayInfo.index_type = BtfContext.getTypeIndex(TypeDie);
      break;
    }
  }
}

void BtfTypeEntryArray::print(raw_ostream &s, BtfContext& BtfContext) {
  BtfTypeEntry::print(s, BtfContext);
  s << "\tElement type: " << format("0x%08lx", ArrayInfo.type) << "\n";
  s << "\tndex type: " << format("0x%08lx", ArrayInfo.index_type) << "\n";
  s << "\t# of element: " << ArrayInfo.nelems << "\n";
}

BtfTypeEntryStruct::BtfTypeEntryStruct(const DIE &Die) : BtfTypeEntry(Die) {
  // get btf_type.size
  auto V = Die.findAttribute(dwarf::DW_AT_byte_size);
  __u32 Size = V.getDIEInteger().getValue() & 0xffffffff;
  auto Kind = BtfTypeEntry::getDieKind(Die);

  int Vlen = 0;
  for (auto &ChildDie : Die.children())
    if (ChildDie.getTag() == dwarf::DW_TAG_member)
      Vlen++;

  BtfType.size = Size;
  BtfType.info = (Kind << 24) | (Vlen & BTF_MAX_VLEN);
}

void BtfTypeEntryStruct::completeData(class BtfContext &BtfContext) {
  auto NameV = Die.findAttribute(dwarf::DW_AT_name);

  if (NameV.getType() != DIEValue::isNone) {
    auto Str = NameV.getDIEString().getString();
    BtfType.name_off = BtfContext.addString(Str);
  } else
    BtfType.name_off = 0;
  for (auto &ChildDie : Die.children()) {
    if (ChildDie.getTag() != dwarf::DW_TAG_member)
      continue;
    struct btf_member BtfMember;
    auto ChildNameV = ChildDie.findAttribute(dwarf::DW_AT_name);

    if (ChildNameV.getType() != DIEValue::isNone) {
      auto Str = ChildNameV.getDIEString().getString();
      BtfMember.name_off = BtfContext.addString(Str);
    } else
      BtfMember.name_off = 0;

    auto TypeV = ChildDie.findAttribute(dwarf::DW_AT_type);
    auto &TypeDie = TypeV.getDIEEntry().getEntry();
    BtfMember.type = BtfContext.getTypeIndex(TypeDie);

    auto OffsetV = ChildDie.findAttribute(dwarf::DW_AT_bit_offset);
    BtfMember.offset = (OffsetV.getType() == DIEValue::isInteger) ?
      OffsetV.getDIEInteger().getValue() : 0;

    Members.push_back(BtfMember);
  }
}

void BtfTypeEntryStruct::print(raw_ostream &s, BtfContext& BtfContext) {
  BtfTypeEntry::print(s, BtfContext);

  for (size_t i = 0; i < BTF_INFO_VLEN(BtfType.info); i++) {
    auto &Member = Members[i];
    s << "\tMember: " << BtfContext.getStringAtOffset(Member.name_off)
      << " of type: "
      << BtfContext.getTypeName(BtfContext.getMemberTypeEntry(Member))
      << " (" << Member.type << ")\n";
  }
}

BtfTypeEntryFunc::BtfTypeEntryFunc(const DIE &Die) : BtfTypeEntry(Die) {
  auto Kind = BtfTypeEntry::getDieKind(Die);

  int Vlen = 0;
  for (auto &ChildDie : Die.children())
    if (ChildDie.getTag() == dwarf::DW_TAG_formal_parameter)
      Vlen++;

  BtfType.size = 0;
  BtfType.info = (Kind << 24) | (Vlen & BTF_MAX_VLEN);
}

void BtfTypeEntryFunc::completeData(class BtfContext &BtfContext) {
  auto NameV = Die.findAttribute(dwarf::DW_AT_name);
  if (NameV.getType() == DIEValue::isNone) {
    auto TypeV = Die.findAttribute(dwarf::DW_AT_type);
    if (TypeV.getType() == DIEValue::isNone)
      return;
    NameV = TypeV.getDIEEntry().getEntry().findAttribute(dwarf::DW_AT_name);
    if (NameV.getType() == DIEValue::isNone)
      return;
  }
  auto Str = NameV.getDIEString().getString();
  BtfType.name_off = BtfContext.addString(Str);

  for (auto &ChildDie : Die.children()) {
    if (ChildDie.getTag() != dwarf::DW_TAG_formal_parameter)
      continue;

    auto TypeV = ChildDie.findAttribute(dwarf::DW_AT_type);
    auto &TypeDie = TypeV.getDIEEntry().getEntry();
    Parameters.push_back(BtfContext.getTypeIndex(TypeDie));
  }
}

void BtfTypeEntryFunc::print(raw_ostream &s, BtfContext& BtfContext) {
  BtfTypeEntry::print(s, BtfContext);

  for (size_t i = 0; i < BTF_INFO_VLEN(BtfType.info); i++) {
    auto Parameter = Parameters[i];
    s << "\tParameter of type: " << BtfContext.getTypeName(Parameter) << "\n";
  }
}

__u32 BtfContext::getTypeIndex(DIE &Die) {
  DIE *DiePtr = const_cast<DIE*>(&Die);

  if (DieToIdMap.find(DiePtr) == DieToIdMap.end())
    return 0;
  return DieToIdMap[DiePtr] + 1;
}

BtfTypeEntry *BtfContext::getReferredTypeEntry(BtfTypeEntry *TypeEntry) {
  if (TypeEntry->getTypeIndex() == 0)
    return nullptr;

  return TypeEntries[TypeEntry->getTypeIndex() - 1].get();
}

BtfTypeEntry *BtfContext::getMemberTypeEntry(struct btf_member &Member) {
  if (Member.type == 0)
    return nullptr;
  return TypeEntries[Member.type - 1].get();
}

std::string BtfContext::getTypeName(BtfTypeEntry *TypeEntry) {
  if (!TypeEntry)
    return "UNKNOWN";
  int Kind = TypeEntry->getKind();

  switch (Kind) {
    case BTF_KIND_INT:
    case BTF_KIND_STRUCT:
    case BTF_KIND_UNION:
    case BTF_KIND_ARRAY:
    case BTF_KIND_FUNC:
      return StringTable.getStringAtOffset(TypeEntry->getNameOff());
    case BTF_KIND_ENUM:
      return "enum " + StringTable.getStringAtOffset(TypeEntry->getNameOff());
    case BTF_KIND_CONST:
      return "const " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_PTR:
      return "ptr " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_VOLATILE:
      return "volatile " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_TYPEDEF:
      return "typedef " + getTypeName(getReferredTypeEntry(TypeEntry));
    case BTF_KIND_RESTRICT:
      return "restrict " + getTypeName(getReferredTypeEntry(TypeEntry));
    default:
      break;
  }
  return "";
}

std::string BtfContext::getTypeName(__u32 TypeIndex) {
  if (TypeIndex == 0)
    return "";
  return getTypeName(TypeEntries[TypeIndex - 1].get());
}

void BtfContext::addTypeEntry(const DIE &Die) {
  auto Tag = Die.getTag();

  if (Tag == dwarf::DW_TAG_subprogram || Tag == dwarf::DW_TAG_compile_unit)
    for (auto &ChildDie : Die.children())
      addTypeEntry(ChildDie);
  if (BtfTypeEntry::shouldSkipDie(Die))
    return;
  auto Kind = BtfTypeEntry::getDieKind(Die);
  if (Kind != BTF_KIND_UNKN) {
    auto TypeEntry = BtfTypeEntry::dieToBtfTypeEntry(Die);
    if (TypeEntry != nullptr) {
      TypeEntry->setId(TypeEntries.size());
      DieToIdMap[const_cast<DIE*>(&Die)] = TypeEntry->getId();
      TypeEntries.push_back(std::move(TypeEntry));
    }
  }
}

void BtfContext::showAll() {
  for (size_t i = 0; i < TypeEntries.size(); i++) {
    auto TypeEntry = TypeEntries[i].get();
    TypeEntry->print(outs(), *this);
    outs() << "\n\n";
  }

  StringTable.showTable();
}

size_t BtfStringTable::addString(std::string S) {
  size_t Offset = Size;

  OffsetToIdMap[Offset] = Table.size();

  Table.push_back(S);
  Size += S.size() + 1;

  return Offset;
}

void BtfContext::completeData() {
  StringTable.addString("\0");

  for (auto &TypeEntry : TypeEntries)
    TypeEntry->completeData(*this);
}

void BtfContext::buildBtfHeader() {
  Header.magic = 0xeB9F;
  Header.version = BTF_VERSION;
  Header.flags = 0;
  Header.hdr_len = sizeof(struct btf_header);

  Header.type_off = 0;
  Header.type_len = 0;

  for (auto &TypeEntry : TypeEntries)
    Header.type_len += TypeEntry->getSize();

  Header.str_off = Header.type_off + Header.type_len;
  Header.str_len = StringTable.getSize();
}

void BtfContext::addDwarfCU(DwarfUnit *TheU) {
  DIE &CuDie = TheU->getUnitDie();

  assert((CuDie.getTag() == dwarf::DW_TAG_compile_unit) &&
         "Not a compile unit");
  assert(!Finished && "The BtfContext is already finished");

  addTypeEntry(CuDie);
}

void BtfContext::finish() {
  Finished = true;
  completeData();
  buildBtfHeader();
}

void BtfContext::emitBtfSection(AsmPrinter *Asm, MCSection *BtfSection) {
  Asm->OutStreamer->SwitchSection(BtfSection);

  // header
  Asm->emitInt16(Header.magic);
  Asm->emitInt8(Header.version);
  Asm->emitInt8(Header.flags);
  Asm->emitInt32(Header.hdr_len);

  Asm->emitInt32(Header.type_off);
  Asm->emitInt32(Header.type_len);
  Asm->emitInt32(Header.str_off);
  Asm->emitInt32(Header.str_len);

  // types
  for (auto &TypeEntry : TypeEntries)
    TypeEntry->emitData(Asm);

  // strings
  StringTable.dumpTable(Asm);
}

void BtfStringTable::dumpTable(AsmPrinter *Asm) {
  for (auto &S : Table) {
    for (auto C : S)
      Asm->emitInt8(C);
    Asm->emitInt8('\0');
  }
}

}
