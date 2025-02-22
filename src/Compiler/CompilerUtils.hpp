/*
 * SPDX-License-Identifier: Apache-2.0
 */

//===-------------------------- CompilerUtils.hpp -------------------------===//
//
// Copyright 2019-2022 The IBM Research Authors.
//
// =============================================================================
//
// Functions for adding passes and processing input files.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/FileUtilities.h"

#include "mlir/Conversion/SCFToStandard/SCFToStandard.h"
#include "mlir/Conversion/VectorToLLVM/ConvertVectorToLLVM.h"
#include "mlir/Conversion/VectorToSCF/VectorToSCF.h"
#include "mlir/InitAllDialects.h"
#include "mlir/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

#include "onnx-mlir/Compiler/OMCompilerTypes.h"

#include "src/Builder/FrontendDialectTransformer.hpp"
#include "src/Dialect/Krnl/KrnlOps.hpp"
#include "src/Pass/Passes.hpp"

extern llvm::cl::OptionCategory OnnxMlirOptions;
extern llvm::cl::opt<std::string> instrumentONNXOps;

void setTargetCPU(const std::string &cpu);
void setTargetTriple(const std::string &triple);

std::string compileModuleToObject(
    const mlir::OwningModuleRef &module, std::string outputBaseName);
std::string compileModuleToSharedLibrary(
    const mlir::OwningModuleRef &module, std::string outputBaseName);
void compileModuleToJniJar(
    const mlir::OwningModuleRef &module, std::string outputBaseName);

void registerDialects(mlir::MLIRContext &context);

void addONNXToMLIRPasses(mlir::PassManager &pm);
void addONNXToKrnlPasses(mlir::PassManager &pm);
void addKrnlToAffinePasses(mlir::PassManager &pm);
void addKrnlToLLVMPasses(mlir::OpPassManager &pm);

void processInputFile(std::string inputFilename, mlir::MLIRContext &context,
    mlir::OwningModuleRef &module, std::string *errorMessage);
void processInputArray(const void *onnxBuffer, int bufferSize,
    mlir::MLIRContext &context, mlir::OwningModuleRef &module);

int compileModule(mlir::OwningModuleRef &module, mlir::MLIRContext &context,
    std::string outputBaseName, onnx_mlir::EmissionTargetType emissionTarget);
