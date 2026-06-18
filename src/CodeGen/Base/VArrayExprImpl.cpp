// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR Varray creation.
 */

#include "Base/VArrayExprImpl.h"

#include <optional>

#include "CGModule.h"
#include "IRBuilder.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/IR/Value/Value.h"

using namespace Cangjie;
using namespace CodeGen;

namespace {
llvm::Value* GenerateConstantVArray(
    const IRBuilder2& irBuilder, const CHIR::VArray& varray, const std::string& serialized)
{
    auto chirType = varray.GetResult()->GetType();
    CJC_ASSERT_WITH_MSG(chirType->IsVArray(), "Should not reach here.");
    auto varrayChirType = StaticCast<const CHIR::VArrayType*>(chirType);

    auto varrayCGType = CGType::GetOrCreate(irBuilder.GetCGModule(), varrayChirType);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<llvm::Constant*> params;
    for (size_t i = 0; i < varray.GetOperands().size(); ++i) {
        auto value = (irBuilder.GetCGModule() | varray.GetOperand(i))->GetRawValue();
        auto tmp = llvm::dyn_cast<llvm::GlobalVariable>(value);
        params.emplace_back(tmp ? tmp->getInitializer() : llvm::cast<llvm::Constant>(value));
    }
    auto arrayType = llvm::cast<llvm::ArrayType>(varrayCGType->GetLLVMType());
    auto constVal = llvm::ConstantArray::get(arrayType, params);
    return irBuilder.GetCGModule().GetOrCreateGlobalVariable(constVal, serialized, false);
#endif
}
} // namespace

llvm::Value* CodeGen::GenerateVArray(IRBuilder2& irBuilder, const CHIR::VArray& varray)
{
    // let arr1: VArray<Int64, $5> = [1,2,3,4,5]
    auto [isConstantVArray, serialized] = IsConstantVArray(varray);
    if (isConstantVArray) {
        return GenerateConstantVArray(irBuilder, varray, serialized);
    }
    auto chirType = varray.GetResult()->GetType();
    CJC_ASSERT_WITH_MSG(chirType->IsVArray(), "Should not reach here.");
    auto varrayChirType = StaticCast<const CHIR::VArrayType*>(chirType);

    auto varrayCGType = CGType::GetOrCreate(irBuilder.GetCGModule(), varrayChirType);
    auto varrayType = varrayCGType->GetLLVMType();
    auto varrayPtr = irBuilder.CreateEntryAlloca(varrayType, nullptr, "varray");

    for (size_t i = 0; i < varrayChirType->GetSize(); ++i) {
        auto indexName = "varray.idx" + std::to_string(i) + "E";
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        auto elementPtr =
            irBuilder.CreateGEP(varrayType, varrayPtr, {irBuilder.getInt64(0), irBuilder.getInt64(i)}, indexName);
#endif
        auto cGValue = (irBuilder.GetCGModule() | varray.GetOperand(i));
        auto& cgCtx = irBuilder.GetCGContext();
        auto& cgMod = irBuilder.GetCGModule();
        auto elementPtrCGType =
            CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *varrayChirType->GetElementType()));
        (void)irBuilder.CreateStore(*cGValue, CGValue(elementPtr, elementPtrCGType));
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    return varrayPtr;
#endif
}

llvm::Value* CodeGen::GenerateVArrayBuilder(IRBuilder2& irBuilder, const CHIR::VArrayBuilder& varrayBuilder)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto varrayType = StaticCast<CHIR::VArrayType*>(varrayBuilder.GetResult()->GetType());
    auto varrayLen = (cgMod | varrayBuilder.GetSize())->GetRawValue();

    // Discriminate the two VArrayBuilder forms. Only the lambda form has an init function
    // that is BOTH non-null AND an $Auto_Env closure object; every repeat form fails at least
    // one of those, so `isInitedByItem = initFunc-is-const-null || init-func-type-is-not-AutoEnv`.
    // Neither test alone suffices:
    //   * A pure TYPE test (IsAutoEnvBase) misroutes the ordinary `repeat: v` form (e.g. a
    //     scalar value): closure conversion retypes that form's null init-function from a plain
    //     function type to an $Auto_Env class, so it looks like a lambda by type -- then codegen
    //     loads a function pointer from the null closure and calls it per element, faulting on
    //     the null page every iteration and hanging at runtime.
    //   * A pure VALUE test (IsConstantNull) misroutes the `repeat: <function-typed value>` form
    //     (e.g. a null CFunc): the translator routes a function-typed repeat value through the
    //     lambda-shaped builder, putting the value itself (a non-constant TypeCast, not a null
    //     literal) in the init-function slot -- testing only constant-null then takes the lambda
    //     path and dereferences a non-existent init-function class, crashing codegen (SIGSEGV).
    // Its CFunc type is not AutoEnv, so the type term catches it; the scalar case's null value
    // is caught by the value term. Together they classify all three forms correctly.
    auto initFuncVar = DynamicCast<CHIR::LocalVar*>(varrayBuilder.GetInitFunc());
    bool initFuncIsConstNull = initFuncVar != nullptr && initFuncVar->GetExpr()->IsConstantNull();
    bool initFuncIsAutoEnv = DeRef(*varrayBuilder.GetInitFunc()->GetType())->IsAutoEnvBase();
    bool isInitedByItem = initFuncIsConstNull || !initFuncIsAutoEnv;
    if (!isInitedByItem) {
        // VArrayBuilder(size, nullptr, initLambda: Class-$Auto_Env_Base_XXXX)
        auto autoEnvOfInitFunc = varrayBuilder.GetInitFunc();
        auto autoEnvType = DeRef(*autoEnvOfInitFunc->GetType());
        CJC_ASSERT(autoEnvType->IsAutoEnvBase());
        auto cgValue = (cgMod | autoEnvOfInitFunc);
        return irBuilder.VArrayInitedByLambda(varrayLen, *cgValue, *varrayType);
    } else {
        // VArrayBuilder(size, value, initLambda: nullptr)
        auto cgValue = (cgMod | varrayBuilder.GetItem());
        return irBuilder.VArrayInitedByItem(varrayLen, *cgValue, *varrayType);
    }
}
