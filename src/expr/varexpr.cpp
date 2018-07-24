#include "seq/varexpr.h"

using namespace seq;
using namespace llvm;

VarExpr::VarExpr(Var *var) : var(var)
{
}

Value *VarExpr::codegen(BaseFunc *base, BasicBlock*& block)
{
	IRBuilder<> builder(block);
	return builder.CreateLoad(var->result(nullptr));
}

types::Type *VarExpr::getType() const
{
	return var->getType(nullptr);
}

VarExpr *VarExpr::clone(types::RefType *ref)
{
	return new VarExpr(var->clone(ref));
}

CellExpr::CellExpr(Cell *cell) : cell(cell)
{
}

Value *CellExpr::codegen(BaseFunc *base, BasicBlock*& block)
{
	return cell->load(block);
}

types::Type *CellExpr::getType() const
{
	return cell->getType();
}

CellExpr *CellExpr::clone(types::RefType *ref)
{
	return new CellExpr(cell->clone(ref));
}

FuncExpr::FuncExpr(BaseFunc *func) : func(func)
{
}

Value *FuncExpr::codegen(BaseFunc *base, BasicBlock*& block)
{
	func->codegen(block->getModule());
	return func->getFunc();
}

types::Type *FuncExpr::getType() const
{
	return func->getFuncType();
}

FuncExpr *FuncExpr::clone(types::RefType *ref)
{
	return new FuncExpr(func->clone(ref));
}
