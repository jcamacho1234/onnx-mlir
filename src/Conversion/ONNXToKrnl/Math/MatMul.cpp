/*
 * SPDX-License-Identifier: Apache-2.0
 */

//===----------------- Matmul.cpp - Lowering Matmul Op --------------------===//
//
// Copyright 2019-2022 The IBM Research Authors.
//
// =============================================================================
//
// This file lowers the ONNX Matmul Operator to Krnl dialect.
//
//===----------------------------------------------------------------------===//

#include "src/Conversion/ONNXToKrnl/ONNXToKrnlCommon.hpp"
#include "src/Dialect/Krnl/KrnlHelper.hpp"
#include "src/Dialect/ONNX/IndexExpr.hpp"
#include "src/Dialect/ONNX/MLIRDialectBuilder.hpp"
#include "src/Dialect/ONNX/ShapeInference/ONNXShapeHelper.hpp"

using namespace mlir;

#define DEBUG_TRACE 0

struct ONNXMatMulOpLowering : public ConversionPattern {
  ONNXMatMulOpLowering(TypeConverter &typeConverter, MLIRContext *ctx)
      : ConversionPattern(
            typeConverter, mlir::ONNXMatMulOp::getOperationName(), 1, ctx) {}

  // Handle the generic cases, including when there are broadcasts.
  void replaceGenericMatmul(ONNXMatMulOp &matMulOp,
      ONNXMatMulOpAdaptor &operandAdaptor, Type elementType,
      ONNXMatMulOpShapeHelper &shapeHelper, Value alloc, Value fzero,
      ConversionPatternRewriter &rewriter, Location loc) const {

    // Define loops and bounds.
    KrnlBuilder createKrnl(rewriter, loc);
    int outerLoopNum = shapeHelper.dimsForOutput(0).size();
    int totLoopNum = outerLoopNum + 1; // Add reduction inner loop.
    ValueRange loopDef = createKrnl.defineLoops(totLoopNum);
    SmallVector<IndexExpr, 4> loopLbs(totLoopNum, LiteralIndexExpr(0));
    SmallVector<IndexExpr, 4> loopUbs; // All dimsForOutputs, plus reduction.
    SmallVector<Value, 4> outerLoops;  // All but the last loop def.
    for (int i = 0; i < outerLoopNum; ++i) {
      loopUbs.emplace_back(shapeHelper.dimsForOutput(0)[i]);
      outerLoops.emplace_back(loopDef[i]);
    }
    int aRank = shapeHelper.aDims.size();
    int bRank = aRank; // Add for better readability.
    IndexExpr innerUb = shapeHelper.aDims[aRank - 1];
    loopUbs.emplace_back(innerUb);
    SmallVector<Value, 1> innerLoop{loopDef[totLoopNum - 1]}; // Last loop def.

    // Non-reduction loop iterations: output-rank.
    createKrnl.iterateIE(loopDef, outerLoops, loopLbs, loopUbs,
        [&](KrnlBuilder &createKrnl, ValueRange outerIndices) {
          MultiDialectBuilder<KrnlBuilder, MemRefBuilder, MathBuilder> create(
              createKrnl);
          // Single scalar, no need for default alignment.
          Value reductionVal =
              create.mem.alignedAlloca(MemRefType::get({}, elementType));
          create.krnl.store(fzero, reductionVal);
          // Inner loop for reduction.
          create.krnl.iterate({}, innerLoop, {}, {},
              [&](KrnlBuilder &createKrnl, ValueRange innerIndex) {
                MultiDialectBuilder<KrnlBuilder, MathBuilder> create(
                    createKrnl);
                Value k = innerIndex[0];
                SmallVector<Value, 4> aAccessFct, bAccessFct;
                for (int i = 0; i < aRank; ++i) {
                  // Add index if dim is not a padded dimension.
                  if (!shapeHelper.aPadDims[i]) {
                    // For A, reduction index is last
                    if (i == aRank - 1) {
                      aAccessFct.emplace_back(k);
                    } else {
                      aAccessFct.emplace_back(outerIndices[i]);
                    }
                  }
                  if (!shapeHelper.bPadDims[i]) {
                    // For B, reduction index is second to last.
                    if (i == bRank - 2) {
                      bAccessFct.emplace_back(k);
                    } else if (i == outerLoopNum) {
                      // When the rank of A 1D, then the output lost one
                      // dimension. E,g, (5) x (10, 5, 4) -> padded (1, 5) x
                      // (10, 5, 4) = (10, 1, 4). But we drop the "1" so its
                      // really (10, 4). When processing the last dim of the
                      // reduction (i=2 here), we would normally access
                      // output[2] but it does not exist, because we lost a dim
                      // in the output due to 1D A.
                      bAccessFct.emplace_back(outerIndices[i - 1]);
                    } else {
                      bAccessFct.emplace_back(outerIndices[i]);
                    }
                  }
                }
                // Add mat mul operation.
                Value loadedA =
                    create.krnl.load(operandAdaptor.A(), aAccessFct);
                Value loadedB =
                    create.krnl.load(operandAdaptor.B(), bAccessFct);
                Value loadedY = create.krnl.load(reductionVal);
                Value AB = create.math.mul(loadedA, loadedB);
                Value accumulated = create.math.add(loadedY, AB);
                create.krnl.store(accumulated, reductionVal);
              });
          Value accumulated = create.krnl.load(reductionVal);
          create.krnl.store(accumulated, alloc, outerIndices);
        });
  }

  // Handle the cases with 2x2 matrices both for A, B, and C without broadcast.
  // Implementation here uses the efficient 1d tiling plus kernel substitution.
  void replace2x2Matmul2d(ONNXMatMulOp &matMulOp,
      ONNXMatMulOpAdaptor &operandAdaptor, Type elementType,
      ONNXMatMulOpShapeHelper &shapeHelper, Value alloc, Value zeroVal,
      ConversionPatternRewriter &rewriter, Location loc) const {

    // Prepare: loop bounds and zero
    Value A(operandAdaptor.A()), B(operandAdaptor.B()), C(alloc);
    MultiDialectBuilder<KrnlBuilder, MemRefBuilder, MathBuilder> create(
        rewriter, loc);
    Value zero = create.math.constantIndex(0);
    Value I = create.mem.dim(C, 0);
    Value J = create.mem.dim(C, 1);
    Value K = create.mem.dim(A, 1);

    // Initialize alloc/C to zero.
    create.krnl.memset(alloc, zeroVal);

    // Compute.
    // Define blocking, with simdization along the j axis.
    int64_t iRegTile(4), jRegTile(8), kRegTile(8);
    // Update tiling for very small sizes known at compile time.
    DimIndexExpr dimI(I), dimJ(J), dimK(K);
    if (dimI.isLiteral()) {
      int64_t constI = dimI.getLiteral();
      if (constI < iRegTile) {
        iRegTile = constI;
        if (DEBUG_TRACE)
          printf("MatMul: Tiling I is reduced to %d\n", (int)iRegTile);
      }
    }
    if (dimJ.isLiteral()) {
      int64_t constJ = dimJ.getLiteral();
      // When jRegTile does not divide J, but 4 would, use 4, unless J is very
      // large, in which case it is better to simdize well the steady state and
      // ignore the last partial block.
      if (constJ % jRegTile != 0 && constJ % 4 == 0 && constJ <= 32) {
        jRegTile = 4;
        if (DEBUG_TRACE)
          printf("MatMul: Tiling J is reduced to %d\n", (int)jRegTile);
      }
    }
    if (dimK.isLiteral()) {
      int64_t constK = dimK.getLiteral();
      if (constK < kRegTile) {
        kRegTile = constK;
        if (DEBUG_TRACE)
          printf("MatMul: Tiling K is reduced to %d\n", (int)kRegTile);
      }
    }

    // I, J, K loop.
    ValueRange origLoop = create.krnl.defineLoops(3);
    Value ii(origLoop[0]), jj(origLoop[1]), kk(origLoop[2]);
    // Define blocked loop and permute.
    ValueRange iRegBlock = create.krnl.block(ii, iRegTile);
    Value ii1(iRegBlock[0]), ii2(iRegBlock[1]);
    ValueRange jRegBlock = create.krnl.block(jj, jRegTile);
    Value jj1(jRegBlock[0]), jj2(jRegBlock[1]);
    ValueRange kRegBlock = create.krnl.block(kk, kRegTile);
    Value kk1(kRegBlock[0]), kk2(kRegBlock[1]);
    create.krnl.permute({ii1, ii2, jj1, jj2, kk1, kk2}, {0, 3, 1, 4, 2, 5});
    create.krnl.iterate({ii, jj, kk}, {ii1, jj1, kk1}, {zero, zero, zero},
        {I, J, K}, [&](KrnlBuilder &createKrnl, ValueRange indices) {
          Value i1(indices[0]), j1(indices[1]), k1(indices[2]);
          createKrnl.matmul(A, {zero, zero}, B, {zero, zero}, C, {zero, zero},
              {ii2, jj2, kk2}, {i1, j1, k1}, {I, J, K},
              {iRegTile, jRegTile, kRegTile}, {}, {}, {},
              /*simd*/ true, /*unroll*/ true, /*overcompute*/ false);
        });
  }

  // Handle the cases with 2x2 matrices both for A, B, and C without broadcast.
  // Implementation here uses the efficient 2d tiling plus kernel substitution.

  LogicalResult matchAndRewrite(Operation *op, ArrayRef<Value> operands,
      ConversionPatternRewriter &rewriter) const final {

    // Get shape.
    ONNXMatMulOpAdaptor operandAdaptor(operands);
    ONNXMatMulOp matMulOp = llvm::cast<ONNXMatMulOp>(op);
    Location loc = ONNXLoc<ONNXMatMulOp>(op);
    ONNXMatMulOpShapeHelper shapeHelper(&matMulOp, &rewriter,
        getDenseElementAttributeFromKrnlValue,
        loadDenseElementArrayValueAtIndex);
    LogicalResult shapecomputed = shapeHelper.computeShape(operandAdaptor);
    assert(succeeded(shapecomputed));

    // Insert an allocation and deallocation for the output of this operation.
    MemRefType outputMemRefType = convertToMemRefType(*op->result_type_begin());
    Type elementType = outputMemRefType.getElementType();
    Value alloc = insertAllocAndDeallocSimple(
        rewriter, op, outputMemRefType, loc, shapeHelper.dimsForOutput(0));

    // Get the constants: zero.
    Value zero = emitConstantOp(rewriter, loc, elementType, 0);

    Value A(operandAdaptor.A()), B(operandAdaptor.B());
    auto aRank = A.getType().cast<MemRefType>().getShape().size();
    auto bRank = B.getType().cast<MemRefType>().getShape().size();
    if (aRank == 2 && bRank == 2) {
      replace2x2Matmul2d(matMulOp, operandAdaptor, elementType, shapeHelper,
          alloc, zero, rewriter, loc);
    } else {
      replaceGenericMatmul(matMulOp, operandAdaptor, elementType, shapeHelper,
          alloc, zero, rewriter, loc);
    }
    // Done.
    rewriter.replaceOp(op, alloc);
    return success();
  }
};

void populateLoweringONNXMatMulOpPattern(RewritePatternSet &patterns,
    TypeConverter &typeConverter, MLIRContext *ctx) {
  patterns.insert<ONNXMatMulOpLowering>(typeConverter, ctx);
}
