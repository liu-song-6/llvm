//===- Dwarf2Btf.h -------------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARF2BTF_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARF2BTF_H

#include <linux/types.h>

#define BTF_MAGIC	0xeB9F
#define BTF_VERSION	1

struct btf_header {
	__u16	magic;
	__u8	version;
	__u8	flags;
	__u32	hdr_len;

	/* All offsets are in bytes relative to the end of this header */
	__u32	type_off;	/* offset of type section	*/
	__u32	type_len;	/* length of type section	*/
	__u32	str_off;	/* offset of string section	*/
	__u32	str_len;	/* length of string section	*/
};

/* Max # of type identifier */
#define BTF_MAX_TYPE	0x0000ffff
/* Max offset into the string section */
#define BTF_MAX_NAME_OFFSET	0x0000ffff
/* Max # of struct/union/enum members or func args */
#define BTF_MAX_VLEN	0xffff

struct btf_type {
	__u32 name_off;
	/* "info" bits arrangement
	 * bits  0-15: vlen (e.g. # of struct's members)
	 * bits 16-23: unused
	 * bits 24-27: kind (e.g. int, ptr, array...etc)
	 * bits 28-31: unused
	 */
	__u32 info;
	/* "size" is used by INT, ENUM, STRUCT and UNION.
	 * "size" tells the size of the type it is describing.
	 *
	 * "type" is used by PTR, TYPEDEF, VOLATILE, CONST and RESTRICT.
	 * "type" is a type_id referring to another type.
	 */
	union {
		__u32 size;
		__u32 type;
	};
};

#define BTF_INFO_KIND(info)	(((info) >> 24) & 0x0f)
#define BTF_INFO_VLEN(info)	((info) & 0xffff)

#define BTF_KIND_UNKN		0	/* Unknown	*/
#define BTF_KIND_INT		1	/* Integer	*/
#define BTF_KIND_PTR		2	/* Pointer	*/
#define BTF_KIND_ARRAY		3	/* Array	*/
#define BTF_KIND_STRUCT		4	/* Struct	*/
#define BTF_KIND_UNION		5	/* Union	*/
#define BTF_KIND_ENUM		6	/* Enumeration	*/
#define BTF_KIND_FWD		7	/* Forward	*/
#define BTF_KIND_TYPEDEF	8	/* Typedef	*/
#define BTF_KIND_VOLATILE	9	/* Volatile	*/
#define BTF_KIND_CONST		10	/* Const	*/
#define BTF_KIND_RESTRICT	11	/* Restrict	*/
#define BTF_KIND_FUNC		12	/* Function	*/
#define BTF_KIND_FUNC_PROTO	13	/* Function Prototype	*/
#define BTF_KIND_MAX		13
#define NR_BTF_KINDS		14

/* For some specific BTF_KIND, "struct btf_type" is immediately
 * followed by extra data.
 */

/* BTF_KIND_INT is followed by a u32 and the following
 * is the 32 bits arrangement:
 */
#define BTF_INT_ENCODING(VAL)	(((VAL) & 0x0f000000) >> 24)
#define BTF_INT_OFFSET(VAL)	(((VAL  & 0x00ff0000)) >> 16)
#define BTF_INT_BITS(VAL)	((VAL)  & 0x000000ff)

/* Attributes stored in the BTF_INT_ENCODING */
#define BTF_INT_SIGNED	(1 << 0)
#define BTF_INT_CHAR	(1 << 1)
#define BTF_INT_BOOL	(1 << 2)

/* BTF_KIND_ENUM is followed by multiple "struct btf_enum".
 * The exact number of btf_enum is stored in the vlen (of the
 * info in "struct btf_type").
 */
struct btf_enum {
	__u32	name_off;
	__s32	val;
};

/* BTF_KIND_ARRAY is followed by one "struct btf_array" */
struct btf_array {
	__u32	type;
	__u32	index_type;
	__u32	nelems;
};

/* BTF_KIND_STRUCT and BTF_KIND_UNION are followed
 * by multiple "struct btf_member".  The exact number
 * of btf_member is stored in the vlen (of the info in
 * "struct btf_type").
 */
struct btf_member {
	__u32	name_off;
	__u32	type;
	__u32	offset;	/* offset in bits */
};

#include "DwarfUnit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/DIE.h"
#include <map>

namespace llvm {

const char *const btf_kind_str[NR_BTF_KINDS] = {
	[BTF_KIND_UNKN]		= "UNKNOWN",
	[BTF_KIND_INT]		= "INT",
	[BTF_KIND_PTR]		= "PTR",
	[BTF_KIND_ARRAY]	= "ARRAY",
	[BTF_KIND_STRUCT]	= "STRUCT",
	[BTF_KIND_UNION]	= "UNION",
	[BTF_KIND_ENUM]		= "ENUM",
	[BTF_KIND_FWD]		= "FWD",
	[BTF_KIND_TYPEDEF]	= "TYPEDEF",
	[BTF_KIND_VOLATILE]	= "VOLATILE",
	[BTF_KIND_CONST]	= "CONST",
	[BTF_KIND_RESTRICT]	= "RESTRICT",
	[BTF_KIND_FUNC]		= "FUNC",
	[BTF_KIND_FUNC_PROTO]	= "FUNC_PROTO",
};

class BtfContext;

#define BTF_INVALID_ENCODING 0xff

// This is base class of all BTF KIND. It is also used directly
// by the reference kinds:
//   BTF_KIND_CONST,  BTF_KIND_PTR,  BTF_KIND_VOLATILE,
//   BTF_KIND_TYPEDEF, BTF_KIND_RESTRICT, and BTF_KIND_FWD
class BtfTypeEntry {
protected:
  const DIE &Die;
  size_t Id;  /* type index in the BTF list, started from 1 */

  struct btf_type BtfType;

public:
  // return desired BTF_KIND for the Die, return BTF_KIND_UNKN for
  // invalid/unsupported Die
  static unsigned char getDieKind(const DIE &Die);

  // Return proper BTF_INT_ENCODING of a basetype.
  // Return BTF_INVALID_ENCODING for unsupported (float, etc.)
  static unsigned char getBaseTypeEncoding(const DIE &Die);

  // Return whether this Die should be skipped.
  // We current skip:
  //  1. Unsupported data type (float) and references to unsupported types
  //  2. Non-array variable names
  static bool shouldSkipDie(const DIE &Die);

  static std::unique_ptr<BtfTypeEntry> dieToBtfTypeEntry(const DIE &Die);

  BtfTypeEntry(const DIE &Die);

  virtual void completeData(class BtfContext &BtfContext);
  virtual void print(raw_ostream &s, BtfContext& BtfContext);

  unsigned char getKind() { return BTF_INFO_KIND(BtfType.info); }
  void setId(size_t Id) { this->Id = Id; }
  size_t getId() { return Id; }
  void setNameOff(__u32 NameOff) { BtfType.name_off = NameOff; }

  void emitData(AsmPrinter *Asm) {
    Asm->emitInt32(BtfType.name_off);
    Asm->emitInt32(BtfType.info);
    Asm->emitInt32(BtfType.size);
  }
  size_t getSize() { return sizeof(struct btf_type); }

  __u32 getTypeIndex() { return BtfType.type; }
  __u32 getNameOff() { return BtfType.name_off; }
};

// BTF_KIND_INT
class BtfTypeEntryInt : public BtfTypeEntry {
  __u32 IntVal;  // encoding, offset, bits

public:
  BtfTypeEntryInt(const DIE &Die);
  void completeData(class BtfContext &BtfContext);
  void emitData(AsmPrinter *Asm) {
    BtfTypeEntry::emitData(Asm);
    Asm->emitInt32(IntVal);
  }
  size_t getSize() { return BtfTypeEntry::getSize() + sizeof(__u32); }
  void print(raw_ostream &s, BtfContext& BtfContext);
};

// BTF_KIND_ENUM
class BtfTypeEntryEnum : public BtfTypeEntry {
  std::vector<struct btf_enum> EnumValues;

public:
  BtfTypeEntryEnum(const DIE &Die);
  void completeData(class BtfContext &BtfContext);
  void emitData(AsmPrinter *Asm) {
    BtfTypeEntry::emitData(Asm);
    for (auto &EnumValue : EnumValues) {
      Asm->emitInt32(EnumValue.name_off);
      Asm->emitInt32(EnumValue.val);
    }
  }
  size_t getSize() {
    return BtfTypeEntry::getSize() +
      BTF_INFO_VLEN(BtfType.info) * sizeof(struct btf_enum);
  }
  void print(raw_ostream &s, BtfContext& BtfContext);
};

// BTF_KIND_ARRAY
class BtfTypeEntryArray : public BtfTypeEntry {
  DIE &ArrayTypeDie;   // use Die for DW_TAG_variable
                       // use ArrayTypeDie for DW_TAG_array_type
  struct btf_array ArrayInfo;

public:
  BtfTypeEntryArray(const DIE &Die);
  void completeData(class BtfContext &BtfContext);
  void emitData(AsmPrinter *Asm) {
    BtfTypeEntry::emitData(Asm);
    Asm->emitInt32(ArrayInfo.type);
    Asm->emitInt32(ArrayInfo.index_type);
    Asm->emitInt32(ArrayInfo.nelems);
  }
  size_t getSize() {
    return BtfTypeEntry::getSize() +  sizeof(struct btf_array);
  }
  void print(raw_ostream &s, BtfContext& BtfContext);
};

// BTF_KIND_STRUCT and BTF_KIND_UNION
class BtfTypeEntryStruct : public BtfTypeEntry {
  std::vector<struct btf_member> Members;

public:
  BtfTypeEntryStruct(const DIE &Die);
  void completeData(class BtfContext &BtfContext);
  void emitData(AsmPrinter *Asm) {
    BtfTypeEntry::emitData(Asm);
    for (auto &Member : Members) {
      Asm->emitInt32(Member.name_off);
      Asm->emitInt32(Member.type);
      Asm->emitInt32(Member.offset);
    }
  }
  size_t getSize() {
    return BtfTypeEntry::getSize() +
      BTF_INFO_VLEN(BtfType.info) * sizeof(struct btf_member);
  }
  void print(raw_ostream &s, BtfContext& BtfContext);
};

class BtfTypeEntryFunc : public BtfTypeEntry {
  std::vector<__u32> Parameters;

public:
  BtfTypeEntryFunc(const DIE &Die);
  void completeData(class BtfContext &BtfContext);
  void emitData(AsmPrinter *Asm) {
    BtfTypeEntry::emitData(Asm);
    for (auto &Parameter : Parameters)
      Asm->emitInt32(Parameter);
  }
  size_t getSize() {
    return BtfTypeEntry::getSize() +
      BTF_INFO_VLEN(BtfType.info) * sizeof(__u32);
  }
  void print(raw_ostream &s, BtfContext& BtfContext);
};

class BtfStringTable {
  std::vector<std::string> Table;
  size_t Size;  // total size in bytes
  std::map<size_t, __u32> OffsetToIdMap;

 public:
  BtfStringTable() : Size(0) {}
  size_t addString(std::string S);
  std::string &getStringAtOffset(size_t Offset) {
    return Table[OffsetToIdMap[Offset]];
  }
  void showTable() {
    for (auto S : Table)
      outs() << S << "\n";
  }
  size_t getSize() { return Size; }
  void dumpTable(AsmPrinter *Asm);
};

class BtfContext {
  struct btf_header Header;
  std::vector<std::unique_ptr<BtfTypeEntry>> TypeEntries;
  std::map<DIE*, size_t> DieToIdMap;
  BtfStringTable StringTable;
  friend class BtfTypeEntry;
  friend class BtfTypeEntryInt;
  friend class BtfTypeEntryEnum;
  friend class BtfTypeEntryArray;
  friend class BtfTypeEntryStruct;
  friend class BtfTypeEntryFunc;
  bool Finished;

public:
  BtfContext() : Finished(false) {}
  void addDwarfCU(DwarfUnit *TheU);
  void finish();
  void showAll();
  void emitBtfSection(AsmPrinter *Asm, MCSection *BtfSection);

protected:
  void addTypeEntry(const DIE &Die);

  bool alreadyAdded(DIE &Die) {
    return DieToIdMap.find(const_cast<DIE*>(&Die)) != DieToIdMap.end();
  }

  __u32 getTypeIndex(DIE &Die);
  std::string getTypeName(BtfTypeEntry *TypeEntry);
  std::string getTypeName(__u32 TypeIndex);

  size_t addString(std::string S) {
    return StringTable.addString(S);
  }

  std::string &getStringAtOffset(__u32 Offset) {
    return StringTable.getStringAtOffset((size_t)Offset);
  }
  BtfTypeEntry *getReferredTypeEntry(BtfTypeEntry *TypeEntry);
  BtfTypeEntry *getMemberTypeEntry(struct btf_member &Member);

 private:
  void completeData();
  void buildBtfHeader();
};

}
#endif
