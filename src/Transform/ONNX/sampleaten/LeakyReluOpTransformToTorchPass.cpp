/*
 * SPDX-License-Identifier: Apache-2.0
 */

//===------- ONNXLeakyReluOpTransformPass.cpp - ONNX Op Transform ------------------===//
//
// Copyright 2019-2020 The IBM Research Authors.
//
// =============================================================================
//
// This file implements a combined pass that dynamically invoke several
// transformation on ONNX ops.
//
//===----------------------------------------------------------------------===//

#include <fstream>
#include <iostream>
#include <set>

#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/FileUtilities.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/ToolOutputFile.h"

#include "src/Dialect/Krnl/KrnlOps.hpp"
#include "src/Dialect/ONNX/ONNXOps.hpp"
#include "src/Interface/ShapeInferenceOpInterface.hpp"
#include "src/Pass/Passes.hpp"
#include "src/Support/OMOptions.hpp"


#include "mlir/Transforms/DialectConversion.h"
#include "torch-mlir/Dialect/Torch/IR/TorchDialect.h"
#include "torch-mlir/Dialect/Torch/IR/TorchOps.h"
#include "torch-mlir/Dialect/Torch/Transforms/Passes.h"
#include "torch-mlir/Dialect/Torch/Utils/Utils.h"
#include "llvm/ADT/StringExtras.h"

#include "torch-mlir/Dialect/TorchConversion/IR/TorchConversionDialect.h"
#include "torch-mlir/Dialect/TorchConversion/Transforms/BackendTypeConversion.h"
#include "torch-mlir/Dialect/TorchConversion/IR/TorchConversionOps.h"

#ifdef _WIN32
#include <io.h>
#endif

using namespace mlir;
using namespace mlir::torch;
using namespace mlir::torch::Torch;


/**
 * 
 * ONNX LeakyRelu operation 
 *
 * “LeakyRelu takes input data (Tensor) and an argument alpha, and produces one" 
 * "output data (Tensor) where the function `f(x) = alpha * x for x < 0`," 
 * "`f(x) = x for x >= 0`, is applied to the data tensor elementwise."
 *
 * Operands :
 * X            tensor of 16-bit/32-bit/64-bit float values or memref of any type values
 * Output   : 
 * Y            tensor of 16-bit/32-bit/64-bit float values or memref of any type values 
 *
 * Attributes 
 * alpha    32-bit float attribute
 * 
 * AtenLeakyReluOp Arguments as below 
 * -------------------------------
 *
 *  AnyTorchTensorType : $self
 *  AnyTorchScalarType:$negative_slope 
 * 
 * Validation 
 * ----------
 * ./Debug/bin/onnx-mlir --EmitONNXIR --debug ../../../third-party/onnx-mlir/third_party/onnx/onnx/backend/test/data/node/test_leakyrelu/model.onnx 
 * 
 */


namespace {

class DecomposeONNXToAtenLeakyReluOp : public OpRewritePattern<ONNXLeakyReluOp> {
public:
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(ONNXLeakyReluOp op,
                                PatternRewriter &rewriter) const override {

    Location loc = op.getLoc();
    mlir::MLIRContext *context =  op.getContext();
    ONNXLeakyReluOpAdaptor adapter(op);

    Value x = op.X();

    auto alpha = adapter.alphaAttr(); // mlir::FloatAttr
    auto neg_slope = alpha.getValue(); // APSFloat
    auto f3 = FloatAttr::get(alpha.getType(), neg_slope.convertToFloat());
    Value f3v = rewriter.create<ConstantFloatOp>(loc,f3);

    TensorType x_tensor_type  = x.getType().cast<TensorType>();
    TensorType op_tensor_type = op->getResult(0).getType().cast<TensorType>();

    auto xTy      = Torch::ValueTensorType::get(context, x_tensor_type.getShape(), x_tensor_type.getElementType());
    auto xtt  = rewriter.create<torch::TorchConversion::FromBuiltinTensorOp>( loc, xTy, x); 
    auto resultTy = Torch::ValueTensorType::get(op.getContext(), op_tensor_type.getShape(), op_tensor_type.getElementType());

    Value atenleakyrelu = rewriter.create<AtenLeakyReluOp>(loc, resultTy, xtt, f3v); 

    llvm::outs() << "ATENRELU CREATED is " << atenleakyrelu << "\n"; 
    Value result = atenleakyrelu; 

    rewriter.replaceOpWithNewOp<TensorStaticInfoCastOp>(op, op->getResult(0).getType() , result);
    return success();
  }
};

} // namespace 


namespace { 

class ONNXToAtenLeakyReluOpTransformPass 
    : public PassWrapper<ONNXToAtenLeakyReluOpTransformPass, OperationPass<::mlir::FuncOp>> {
     StringRef getArgument() const override { return "onnx-to-aten-leakyrelu-transform"; }
     void runOnOperation() override {

          MLIRContext *context = &getContext();
	  context->getOrLoadDialect<::mlir::torch::Torch::TorchDialect>();
	  context->getOrLoadDialect<::mlir::torch::TorchConversion::TorchConversionDialect>();

	  RewritePatternSet patterns(context);
	  ConversionTarget target(*context);

	  target.addLegalDialect<Torch::TorchDialect>();
	  target.addLegalDialect<::mlir::torch::Torch::TorchDialect>();

	  llvm::outs() << "ONNXToAtenLeakyReluOpTransformPass Before " << "\n"; 

	  patterns.add<DecomposeONNXToAtenLeakyReluOp >(context);

	  llvm::outs() << "ONNXToAtenLeakyReluOpTransformPass After " << "\n"; 

	  if (failed(applyPartialConversion(getOperation(), target,
	      std::move(patterns)))) {
	      return signalPassFailure();
	  }

	  if (onnxOpTransformReport) {
	    llvm::outs() << "ONNXToAtenLeakyReluOpTransform iterated " << 3 
			 << " times, converged "
			 << "\n";
	  }
      }
};


} // end anonymous namespace

/*!
 * Create an instrumentation pass.
 */
std::unique_ptr<Pass> mlir::createONNXToAtenLeakyReluOpTransformPass() {
  return std::make_unique<ONNXToAtenLeakyReluOpTransformPass>();
}
