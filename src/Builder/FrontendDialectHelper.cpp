//===--------------------- FrontendDialectHelper.cpp ----------------------===//
//
// Copyright 2019 The IBM Research Authors.
//
// =============================================================================
//
// Helper methods for handling input ONNX models.
//
//===----------------------------------------------------------------------===//

#include "src/Builder/FrontendDialectHelper.hpp"

namespace onnx_mlir {

void replaceAll(std::string &str, const std::string &from,
                const std::string &to) {
  if (from.empty())
    return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like replacing
                              // 'x' with 'yx'
  }
}

std::string legalize_name(std::string name) {
  std::replace(name.begin(), name.end(), '/', '_');
  std::replace(name.begin(), name.end(), '-', '_');
  replaceAll(name, ":", "_colon_");
  // If tensor name starts with a number, prepend n to make it a legal c++
  // identifier.
  if (name.size() > 0 && isdigit(name.at(0)))
    name.insert(0, 1, 'n');
  return name;
}

mlir::Value OnnxMlirSymbolMapping::GetTensorByOnnxName(
    const std::string &name) {
  assert(onnx_name2onnx_mlir_tensor.find(legalize_name(name)) !=
             onnx_name2onnx_mlir_tensor.end() &&
         "Tensor not found");
  return onnx_name2onnx_mlir_tensor.at(legalize_name(name));
}

void OnnxMlirSymbolMapping::AddMapping(
    const std::string &name, mlir::Value tensor) {
  assert(onnx_name2onnx_mlir_tensor.count(legalize_name(name)) == 0 &&
         "Tensor already exists.");
  onnx_name2onnx_mlir_tensor.emplace(legalize_name(name), tensor);
}

bool OnnxMlirSymbolMapping::ContainKey(std::string name) {
  return onnx_name2onnx_mlir_tensor.count(name) != 0;
}

template <typename T>
struct TransformValueToONNXData {
  static const google::protobuf::RepeatedField<T> data(
      onnx::TensorProto initializer) {
    return google::protobuf::RepeatedField<T>();
  }
};

template <>
struct TransformValueToONNXData<double> {
  static const google::protobuf::RepeatedField<double> data(
      onnx::TensorProto initializer) {
    return initializer.double_data();
  }
};

template <>
struct TransformValueToONNXData<float> {
  static const google::protobuf::RepeatedField<float> data(
      onnx::TensorProto initializer) {
    return initializer.float_data();
  }
};

template <>
struct TransformValueToONNXData<int32_t> {
  static const google::protobuf::RepeatedField<int32_t> data(
      onnx::TensorProto initializer) {
    return initializer.int32_data();
  }
};

template <>
struct TransformValueToONNXData<int64_t> {
  static const google::protobuf::RepeatedField<int64_t> data(
      onnx::TensorProto initializer) {
    return initializer.int64_data();
  }
};

// Helper method for constructing an array attribute from a model input.
template <typename T>
static std::vector<T> CreateArrayAttribute(onnx::TensorProto initializer) {
  size_t size;
  if (initializer.raw_data().size()) {
    // copy & take care of endianness
    std::vector<char> byteInitializer;
    std::copy(initializer.raw_data().begin(), initializer.raw_data().end(),
        back_inserter(byteInitializer));
    size = initializer.raw_data().size() / sizeof(T);
    T *res = reinterpret_cast<T *>(&byteInitializer[0]);
    return std::vector<T>(res, res + size);
  }

  // copy, no need to take care of endianness
  auto data = TransformValueToONNXData<T>::data(initializer);
  size = data.size();
  return std::vector<T>(&data[0], &data[0] + size);
}

void InitializedTensorMapping::AddMapping(
    std::string name, onnx::TensorProto tensor) {
  assert(nameToInitializedTensor.count(name) == 0 &&
         "Tensor initializer already mapped.");
  nameToInitializedTensor.emplace(name, tensor);
}


bool InitializedTensorMapping::ContainKey(std::string name) {
  return nameToInitializedTensor.count(name) != 0;
}

mlir::Value InitializedTensorMapping::EmitInitializerForInputTensor(
    mlir::Location loc, mlir::OpBuilder &builder, std::string name) {
  // Initializer for input.
  onnx::TensorProto initializer = GetInitializedTensor(name);

  // Tensor dimensions.
  llvm::ArrayRef<int64_t> tensorDims(initializer.dims().data(),
      initializer.dims().size());

  // Emit ConstantOp and record the mapping between the input and
  // the constant value.
  // Create value attribute.
  mlir::DenseElementsAttr constantDenseAttribute;
  mlir::Type elementType;
  mlir::ShapedType tensorType;
  switch (initializer.data_type()) {
    case (onnx::TensorProto::FLOAT): {
      const auto& arrayAttrInitializer =
          CreateArrayAttribute<float>(initializer);
      elementType = builder.getF32Type();
      tensorType = mlir::RankedTensorType::get(tensorDims, elementType);
      constantDenseAttribute = mlir::DenseElementsAttr::get(
          tensorType, llvm::makeArrayRef(arrayAttrInitializer));
      break;
    }
    case (onnx::TensorProto::INT32): {
      const auto& arrayAttrInitializer =
          CreateArrayAttribute<int32_t>(initializer);
      elementType = builder.getIntegerType(32);
      tensorType = mlir::RankedTensorType::get(tensorDims, elementType);
      constantDenseAttribute = mlir::DenseElementsAttr::get(
          tensorType, llvm::makeArrayRef(arrayAttrInitializer));
      break;
    }
    case (onnx::TensorProto::INT64): {
      const auto& arrayAttrInitializer =
          CreateArrayAttribute<int64_t>(initializer);
      elementType = builder.getIntegerType(64);
      tensorType = mlir::RankedTensorType::get(tensorDims, elementType);
      constantDenseAttribute = mlir::DenseElementsAttr::get(
          tensorType, llvm::makeArrayRef(arrayAttrInitializer));
      break;
    }
  }

  // Create ConstantOp for dense array.
  return builder.create<mlir::ONNXConstantOp>(
      loc, tensorType, nullptr, constantDenseAttribute);
}

} // namespace onnx_mlir
