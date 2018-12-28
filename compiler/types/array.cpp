#include "seq/seq.h"

using namespace seq;
using namespace llvm;

types::ArrayType::ArrayType(Type *baseType) :
    Type("array", BaseType::get()), baseType(baseType)
{
}

Value *types::ArrayType::defaultValue(BasicBlock *block)
{
	LLVMContext& context = block->getContext();
	Value *ptr = ConstantPointerNull::get(PointerType::get(getBaseType(0)->getLLVMType(context), 0));
	Value *len = zeroLLVM(context);
	return make(ptr, len, block);
}

bool types::ArrayType::isAtomic() const
{
	return false;
}

bool types::ArrayType::is(types::Type *type) const
{
	return isGeneric(type) && types::is(getBaseType(0), type->getBaseType(0));
}

void types::ArrayType::initOps()
{
	if (!vtable.magic.empty())
		return;

	vtable.magic = {
		{"__init__", {Int}, this, SEQ_MAGIC_CAPT(self, args, b) {
			Value *ptr = getBaseType(0)->alloc(args[0], b.GetInsertBlock());
			return make(ptr, args[0], b.GetInsertBlock());
		}},

		{"__init__", {PtrType::get(getBaseType(0)), Int}, this, SEQ_MAGIC_CAPT(self, args, b) {
			return make(args[0], args[1], b.GetInsertBlock());
		}},

		{"__copy__", {}, this, SEQ_MAGIC_CAPT(self, args, b) {
			BasicBlock *block = b.GetInsertBlock();
			Module *module = block->getModule();
			LLVMContext& context = module->getContext();

			auto *allocFunc = makeAllocFunc(module, getBaseType(0)->isAtomic());
			Value *ptr = memb(self, "ptr", block);
			Value *len = memb(self, "len", block);
			Value *elemSize = ConstantInt::get(seqIntLLVM(context), size(module));
			Value *numBytes = b.CreateMul(len, elemSize);
			Value *ptrCopy = b.CreateCall(allocFunc, numBytes);
			makeMemCpy(ptrCopy, ptr, numBytes, block);
			ptrCopy = b.CreateBitCast(ptrCopy, membType("ptr")->getLLVMType(context));
			Value *copy = make(ptrCopy, len, block);
			return copy;
		}},

		{"__len__", {}, Int, SEQ_MAGIC_CAPT(self, args, b) {
			return memb(self, "len", b.GetInsertBlock());
		}},

		{"__bool__", {}, Bool, SEQ_MAGIC_CAPT(self, args, b) {
			Value *len = memb(self, "len", b.GetInsertBlock());
			Value *zero = ConstantInt::get(Int->getLLVMType(b.getContext()), 0);
			return b.CreateZExt(b.CreateICmpNE(len, zero), Bool->getLLVMType(b.getContext()));
		}},

		{"__getitem__", {Int}, getBaseType(0), SEQ_MAGIC_CAPT(self, args, b) {
			Value *ptr = memb(self, "ptr", b.GetInsertBlock());
			ptr = b.CreateGEP(ptr, args[0]);
			return b.CreateLoad(ptr);
		}},

		{"__slice__", {Int, Int}, getBaseType(0), SEQ_MAGIC_CAPT(self, args, b) {
			Value *ptr = memb(self, "ptr", b.GetInsertBlock());
			ptr = b.CreateGEP(ptr, args[0]);
			Value *len = b.CreateSub(args[1], args[0]);
			return make(ptr, len, b.GetInsertBlock());
		}},

		{"__slice_left__", {Int}, getBaseType(0), SEQ_MAGIC_CAPT(self, args, b) {
			Value *ptr = memb(self, "ptr", b.GetInsertBlock());
			return make(ptr, args[0], b.GetInsertBlock());
		}},

		{"__slice_right__", {Int}, getBaseType(0), SEQ_MAGIC_CAPT(self, args, b) {
			Value *ptr = memb(self, "ptr", b.GetInsertBlock());
			Value *to = memb(self, "len", b.GetInsertBlock());
			ptr = b.CreateGEP(ptr, args[0]);
			Value *len = b.CreateSub(to, args[0]);
			return make(ptr, len, b.GetInsertBlock());
		}},

		{"__setitem__", {Int, getBaseType(0)}, Void, SEQ_MAGIC_CAPT(self, args, b) {
			Value *ptr = memb(self, "ptr", b.GetInsertBlock());
			ptr = b.CreateGEP(ptr, args[0]);
			b.CreateStore(args[1], ptr);
			return (Value *)nullptr;
		}},
	};
}

void types::ArrayType::initFields()
{
	if (!vtable.fields.empty())
		return;

	vtable.fields = {
		{"len", {0, Int}},
		{"ptr", {1, PtrType::get(getBaseType(0))}}
	};
}

unsigned types::ArrayType::numBaseTypes() const
{
	return 1;
}

types::Type *types::ArrayType::getBaseType(unsigned idx) const
{
	return baseType;
}

Type *types::ArrayType::getLLVMType(LLVMContext& context) const
{
	return StructType::get(seqIntLLVM(context), PointerType::get(getBaseType(0)->getLLVMType(context), 0));
}

size_t types::ArrayType::size(Module *module) const
{
	return module->getDataLayout().getTypeAllocSize(getLLVMType(module->getContext()));
}

Value *types::ArrayType::make(Value *ptr, Value *len, BasicBlock *block)
{
	LLVMContext& context = ptr->getContext();
	Value *self = UndefValue::get(getLLVMType(context));
	self = setMemb(self, "ptr", ptr, block);
	self = setMemb(self, "len", len, block);
	return self;
}

types::ArrayType *types::ArrayType::get(Type *baseType) noexcept
{
	return new ArrayType(baseType);
}

types::ArrayType *types::ArrayType::get() noexcept
{
	return new ArrayType(types::BaseType::get());
}

types::ArrayType *types::ArrayType::clone(Generic *ref)
{
	return get(getBaseType(0)->clone(ref));
}