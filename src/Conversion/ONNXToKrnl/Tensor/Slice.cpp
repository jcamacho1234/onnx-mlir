/*
 * SPDX-License-Identifier: Apache-2.0
 */

//===----------------Slice.cpp - Lowering Slice Op----------------------=== //
//
// Copyright 2020-2022 The IBM Research Authors.
//
// =============================================================================
//
// This file lowers the ONNX Slice Operator to Krnl dialect.
//
//===----------------------------------------------------------------------===//

#include "src/Conversion/ONNXToKrnl/ONNXToKrnlCommon.hpp"
#include "src/Dialect/ONNX/ShapeInference/ONNXShapeHelper.hpp"

using namespace mlir;

struct ONNXSliceOpLowering : public ConversionPattern {
  ONNXSliceOpLowering(TypeConverter &typeConverter, MLIRContext *ctx)
      : ConversionPattern(
            typeConverter, mlir::ONNXSliceOp::getOperationName(), 1, ctx) {}

  LogicalResult matchAndRewrite(Operation *op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const final {
    ONNXSliceOpAdaptor operandAdaptor(operands);
    ONNXSliceOp sliceOp = llvm::cast<ONNXSliceOp>(op);
    Location loc = op->getLoc();

    ONNXSliceOpShapeHelper shapeHelper(&sliceOp, &rewriter,
        getDenseElementAttributeFromKrnlValue,
        loadDenseElementArrayValueAtIndex);
    auto shapecomputed = shapeHelper.computeShape(operandAdaptor);
    assert(succeeded(shapecomputed));

    auto outputMemRefType = convertToMemRefType(*op->result_type_begin());
    int64_t outputRank = outputMemRefType.getShape().size();
    // Insert an allocation and deallocation for the output of this operation.
    Value alloc = insertAllocAndDeallocSimple(
        rewriter, op, outputMemRefType, loc, shapeHelper.dimsForOutput(0));

    BuildKrnlLoop outputLoops(rewriter, loc, outputRank);
    outputLoops.createDefineOp();
    outputLoops.pushAllBounds(shapeHelper.dimsForOutput(0));
    outputLoops.createIterateOp();
    rewriter.setInsertionPointToStart(outputLoops.getIterateBlock());

    IndexExprScope childScope(&rewriter, shapeHelper.scope);
    // Scope for krnl ops
    KrnlBuilder createKrnl(rewriter, loc);

    // Compute indices for the load and store op.
    // Load: "i * step + start" for all dim.
    // Store: "i" for all dims.
    SmallVector<IndexExpr, 4> loadIndices;
    SmallVector<IndexExpr, 4> storeIndices;
    for (int ii = 0; ii < outputRank; ++ii) {
      Value inductionVal = outputLoops.getInductionVar(ii);
      DimIndexExpr inductionIndex(inductionVal);
      IndexExpr start = SymbolIndexExpr(shapeHelper.starts[ii]);
      IndexExpr step = SymbolIndexExpr(shapeHelper.steps[ii]);
      loadIndices.emplace_back((step * inductionIndex) + start);
      storeIndices.emplace_back(inductionIndex);
    }
    // Load data and store in alloc data.
    Value loadVal = createKrnl.loadIE(operandAdaptor.data(), loadIndices);
    createKrnl.storeIE(loadVal, alloc, storeIndices);

    rewriter.replaceOp(op, alloc);
    return success();
  }
};

void populateLoweringONNXSliceOpPattern(RewritePatternSet &patterns,
    TypeConverter &typeConverter, MLIRContext *ctx) {
  patterns.insert<ONNXSliceOpLowering>(typeConverter, ctx);
}
